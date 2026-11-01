// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/MasterOutputMonoPolicy.h"
#include "../engine/core/LiveChannelFxGraph.h"
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "VstBusLayout.h"
#include "../engine/core/NativeDryRouteCommand.h"
#include "NativeVerifiedSourcePatch.h"
#include "../engine/device/JuceActivePortSnapshot.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../engine/device/LiveInputPortRegistry.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <vector>

// P1E — user-opt-in, *DRY / NO VST3* physical multichannel routing laboratory.
// Uses the P1D core already on main, not a competing mixer/DSP engine.
// Hardware rate/port identity and DIRECT route authorization are owned by
// LiveAudioEngine. No disk I/O, heap allocation, plugin or device operation
// on process() callback. This is a FAT diagnostic, NOT performance mode.
class LiveDryRoutingLab final {
public:
    using Plan=armix::mixer::PreparedRenderGraph;
    using Block=armix::mixer::PreparedMixerBlock;
    using FxGraph=armix::mixer::LiveChannelFxGraph;
    using Publisher=armix::rt::RealtimeRoutePublisher<FxGraph>;
    struct FxSelection final {
        juce::String id,path,name;
        int uid=0;
    };
    struct CapturedFxState final {
        int lane=0,slot=0;
        juce::String catalogId,instanceId;
        bool bypass=false;
        juce::MemoryBlock binary;
    };
    static constexpr int count=armix::mixer::stripCount;
    static constexpr int auxCount=armix::mixer::auxBusCount;
    static constexpr int fxSlots=count+auxCount;
    static constexpr int maxFxPerLane=FxGraph::maxInsertsPerStrip;
    using FxSelections=std::array<std::array<FxSelection,maxFxPerLane>,fxSlots>;
    struct FxRestore final {
        bool present=false,bypass=false;
        // Borrowed only while synchronous APPLY builds candidate processors.
        // The IO result owns the verified bytes until APPLY returns, so even
        // a 128 MB Show needs no duplicated state buffers on the UI thread.
        const juce::MemoryBlock* binary=nullptr;
    };
    using FxRestores=std::array<std::array<FxRestore,maxFxPerLane>,fxSlots>;
    struct ChannelEdit {
        float linear=1.f, pan=0.f;
        bool mute=false, solo=false, mono=false;
    };

    // JUCE calls aboutToStart after configuring the input/output device.
    // One device session gets a new epoch; stored route cannot survive it.
    void prepareDevice (juce::AudioIODevice* device) noexcept {
        const auto generation=deviceEpoch_.fetch_add(1,std::memory_order_acq_rel)+1;
        userPatched_.store(false,std::memory_order_release);
        registryValid_.store(false,std::memory_order_release);
        topologyFault_.store(false,std::memory_order_release);
        portCount_.store(0,std::memory_order_release);
        callbackInputCount_.store(-1,std::memory_order_release);
        callbackOutputCount_.store(-1,std::memory_order_release);
        if(!device) return;
        // P1F: snapshot both actually negotiated JUCE active masks. This
        // is invoked only during the device lifecycle, never in RT.
        armix::device::JuceActivePortSnapshot snapshot;
        armix::device::JuceActivePortSnapshot::Fault reason;
        if(!armix::device::JuceActivePortSnapshot::capture(
               device,generation,snapshot,reason))return;
        const int inputs=snapshot.inputs.enabledCount;
        const int outputs=snapshot.outputCount;
        if(inputs<=0 || outputs<2) return; // stereo monitoring LAB only
        for(int i=0;i<inputs;++i)
            enabledPorts_[i].store(snapshot.inputs.physicalIndex(i),
                                  std::memory_order_relaxed);
        for(int i=0;i<outputs;++i)
            enabledOutputPorts_[i].store(snapshot.outputPhysicalPort(i),
                                        std::memory_order_relaxed);
        portCount_.store(inputs,std::memory_order_release);
        callbackInputCount_.store(inputs,std::memory_order_release);
        callbackOutputCount_.store(outputs,std::memory_order_release);
        registryValid_.store(true,std::memory_order_release);
    }
    void deviceStopped() noexcept {
        // A stale lab cannot silently resume legacy CH1 on device hotplug.
        userPatched_.store(false,std::memory_order_release);
        deviceEpoch_.fetch_add(1,std::memory_order_acq_rel);
        registryValid_.store(false,std::memory_order_release);
        portCount_.store(0,std::memory_order_release);
        callbackInputCount_.store(-1,std::memory_order_release);
        callbackOutputCount_.store(-1,std::memory_order_release);
        topologyFault_.store(false,std::memory_order_release);
        sendCount_.store(0,std::memory_order_relaxed);
        fxCount_.store(0,std::memory_order_relaxed);
        auxCountActive_.store(0,std::memory_order_relaxed);
        auxSendCount_.store(0,std::memory_order_relaxed);
        fxReused_.store(0,std::memory_order_relaxed);
        // Device lifecycle callback may run off the message thread.
        // acceptedRoute_ is a JUCE var owned by the CONTROL thread and must
        // never be mutated from the device thread. Epoch gates hide stale data.
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksL_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksR_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:auxPeaks_)p.store(0.f,std::memory_order_relaxed);
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
    }

    // R0B B3: One native production topology exists immediately after an
    // explicitly VERIFIED DIRECT device is selected. NONE of its ten strips
    // is mapped to an input, all four AUX returns and sends are OFF.
    // Use the SAME decode/compile/publish transaction as an operator patch:
    // no second DSP, fake default IN-1 mapping or callback initialization.
    // Message/control thread ONLY; all allocations happen outside realtime.
    juce::String startUnpatchedSession(bool directPermitted, bool independentClock,
                       int loadedFx, juce::AudioPluginFormatManager& formats,
                       double sampleRate,int deviceFrames) {
        if(!directPermitted || independentClock)
            return "Production session requires a verified DIRECT audio device";
        if(!registryValid_.load(std::memory_order_acquire) ||
           topologyFault_.load(std::memory_order_acquire))
            return "Verified stereo input/output masks are unavailable";
        if(active() && ready() && hasExplicitRoute())
            return "Clear the active input patch before creating an unpatched session";
        if(active() && ready() && !hasExplicitRoute())return {};
        auto request=std::make_unique<juce::DynamicObject>();
        // New silent sessions use typed v2 from the first callback so the
        // operator can choose MONO/STEREO processing BEFORE a physical IN.
        // Old saved v1 Shows remain readable by NativeDryRouteCommand.
        request->setProperty("schemaVersion",2);
        request->setProperty("deviceEpoch",static_cast<juce::int64>(epoch()));
        request->setProperty("revision",static_cast<juce::int64>(revision()));
        juce::Array<juce::var> channels;
        for(int i=0;i<count;++i) {
            auto row=std::make_unique<juce::DynamicObject>();
            row->setProperty("mode","NONE");
            row->setProperty("sourceKind","NONE");
            row->setProperty("sourceFormat","NONE");
            row->setProperty("channelFormat","STEREO");
            row->setProperty("left",-1);
            row->setProperty("right",-1);
            row->setProperty("gain",0.25);
            row->setProperty("pan",0.0);
            row->setProperty("mute",false);
            row->setProperty("solo",false);
            row->setProperty("main",true);
            channels.add(juce::var(row.release()));
        }
        request->setProperty("channels",juce::var(channels));
        const FxSelections none{};
        // Same publisher/graph as a real user APPLY, but no authorization.
        // Never briefly advertise a synthetic route as LIVE.
        return apply(juce::var(request.release()),directPermitted,
                     independentClock,loadedFx,none,formats,
                     sampleRate,deviceFrames,false);
    }

    juce::String apply (const juce::var& command, bool directPermitted,
                        bool independentClock, int loadedFx,
                        const FxSelections& selections,
                        juce::AudioPluginFormatManager& formats,
                        double sampleRate,int deviceFrames,bool explicitSelection=true,
                        const FxRestores* restores=nullptr) {
        // Called by the native message thread only. Not callable from RT.
        if(!directPermitted || independentClock)
            return "Dry Routing LAB requires one verified DIRECT audio device";
        // Keep the existing multi-insert CH1 rack and vendor Orbit alive.
        // Native CH1 insert and legacy CH1 rack are mutually exclusive;
        // running both would process plugins twice and corrupt user sound.
        if(loadedFx>0 && std::any_of(selections[0].begin(),selections[0].end(),
                [](const auto& fx){return fx.id.isNotEmpty();}))
            return "CH1 has a live FX rack: remove the duplicate CH1 insert from this route";
        if(!command.isObject()) return "Invalid Dry Routing configuration";
        if(restores)for(int lane=0;lane<fxSlots;++lane)
            for(int slot=0;slot<maxFxPerLane;++slot) {
                const bool selected=selections[lane][slot].id.isNotEmpty();
                const auto& state=(*restores)[lane][slot];
                if(selected!=state.present ||
                   (state.present && (!state.binary ||
                       state.binary->getSize()>8U*1024U*1024U)))
                    return "Show FX state incomplete or oversized: lane "+
                           juce::String(lane+1)+" slot "+juce::String(slot+1);
            }
        const auto currentEpoch=deviceEpoch_.load(std::memory_order_acquire);
        if(!registryValid_.load(std::memory_order_acquire))
            return "Audio device has no validated active capture ports";
        if(topologyFault_.load(std::memory_order_acquire))
            return "Device callback channel layout changed; reopen AUDIO SETUP before applying";
        const int countNow=portCount_.load(std::memory_order_acquire);
        if(countNow<=0 || countNow>armix::device::LiveInputPortRegistry::limit)
            return "Invalid active device input channel mask";
        std::array<int,armix::device::LiveInputPortRegistry::limit> physicalPorts{};
        for(int i=0;i<countNow;++i)
            physicalPorts[i]=enabledPorts_[i].load(std::memory_order_relaxed);
        if(deviceEpoch_.load(std::memory_order_acquire)!=currentEpoch ||
           !registryValid_.load(std::memory_order_acquire))
            return "Audio input map changed during routing preparation";
        // MS3A explicit MAIN output destination by *physical* port ID.
        // Resolve against JUCE's validated sparse callback mask. Never use
        // port ID as an array subscript; an 8-output device may be sparse.
        const auto rawMain=command.getProperty("mainOutputs",juce::var());
        int mainLeft=0,mainRight=1;
        if(!rawMain.isVoid()){
            const auto* pins=rawMain.getArray();
            if(!pins||pins->size()!=2||!pins->getReference(0).isInt()||
               !pins->getReference(1).isInt())
                return "MAIN output patch requires two physical port IDs";
            const int physicalLeft=static_cast<int>(pins->getReference(0));
            const int physicalRight=static_cast<int>(pins->getReference(1));
            const int available=callbackOutputCount_.load(std::memory_order_acquire);
            mainLeft=mainRight=-1;
            for(int i=0;i<available;++i){
                const auto pin=enabledOutputPorts_[i].load(std::memory_order_relaxed);
                if(pin==physicalLeft)mainLeft=i;
                if(pin==physicalRight)mainRight=i;
            }
            if(mainLeft<0||mainRight<0||mainLeft==mainRight)
                return "MAIN output patch uses inactive or duplicate physical ports";
        }
        // Optional independent MONITOR mix: resolve physical pins through
        // the actual sparse enabled output mask, disjoint from MAIN.
        const auto rawMonitor=command.getProperty("monitorOutputs",juce::var());
        std::array<float,fxSlots> monitorSends{};
        bool monitorEnabled=false;
        int monitorLeft=-1,monitorRight=-1;
        if(!rawMonitor.isVoid()) {
            const auto* pins=rawMonitor.getArray();
            const auto sendsValue=command.getProperty("monitorSends",juce::var());
            const auto* gains=sendsValue.getArray();
            if(!pins||pins->size()!=2||!gains||gains->size()!=fxSlots ||
               !pins->getReference(0).isInt()||!pins->getReference(1).isInt())
                return "MONITOR requires two physical pins and 14 send gains";
            const int physL=static_cast<int>(pins->getReference(0));
            const int physR=static_cast<int>(pins->getReference(1));
            const int n=callbackOutputCount_.load(std::memory_order_acquire);
            for(int i=0;i<n;++i) {
                const int physical=enabledOutputPorts_[i].load(std::memory_order_relaxed);
                if(physical==physL)monitorLeft=i;
                if(physical==physR)monitorRight=i;
            }
            if(monitorLeft<0||monitorRight<0||monitorLeft==monitorRight||
               monitorLeft==mainLeft||monitorLeft==mainRight||
               monitorRight==mainLeft||monitorRight==mainRight)
                return "MONITOR pins unavailable, duplicated or already used by MAIN";
            for(int i=0;i<fxSlots;++i) {
                const auto v=gains->getReference(i);
                if(!(v.isInt()||v.isDouble()))
                    return "MONITOR sends require numeric gain levels";
                const double gain=static_cast<double>(v);
                if(!std::isfinite(gain)||gain<0.0||gain>1.0)
                    return "MONITOR gain must be between 0 and 1";
                monitorSends[i]=static_cast<float>(gain);
            }
            monitorEnabled=true;
        }
        const auto expectedEpoch=static_cast<std::uint64_t>(
            static_cast<juce::int64>(command.getProperty("deviceEpoch",juce::var(0))));
        const auto expectedRevision=static_cast<std::uint64_t>(
            static_cast<juce::int64>(command.getProperty("revision",juce::var(0))));
        if(currentEpoch==0 || expectedEpoch!=currentEpoch)
            return "Audio device changed: reopen ROUTING to remap physical ports";
        armix::mixer::NativeDryRouteCommand decoded;
        juce::String validationError;
        if(!armix::mixer::NativeDryRouteCommand::decode(
               command,decoded,validationError))return validationError;
        const auto& cfg=decoded.channels;
        const auto& sends=decoded.sends;
        // All physical and BUS nodes stay scheduled for realtime SOLO/MUTE;
        // only the runtime parameter plane controls audibility thereafter.
        // This prevents re-instantiating VST3 when a musician moves a fader.
        auto topology=cfg;
        for(auto& strip:topology.stripsAndMaster.strips) {
            strip.mute=false;
            strip.solo=false;
        }
        Plan plan;
        armix::mixer::RenderError error;
        if(!Plan::compile(topology,
                          {physicalPorts.data(),static_cast<std::size_t>(countNow)},
                          sends,plan,error,decoded.aux))
            return "Invalid physical routing (inactive port, format or mapping)";
        const auto revision=expectedRevision+1;
        if(revision==0) return "Routing revision overflow";
        auto next=std::make_unique<Publisher::Snapshot>();
        next->deviceEpoch=currentEpoch;
        next->graphRevision=revision;
        next->prepared=std::make_unique<FxGraph>(plan,Block::hardFrameLimit,
             juce::jlimit(32,Block::hardFrameLimit,deviceFrames));
        if(!next->prepared || next->prepared->capacity()==0)
            return "Cannot allocate routing workspace";
        if(!next->prepared->setMainOutputPair(
              mainLeft,mainRight,callbackOutputCount_.load(std::memory_order_acquire)))
            return "MAIN output patch references invalid callback channels";
        if(monitorEnabled && !next->prepared->setMonitorOutputBus(
             monitorLeft,monitorRight,callbackOutputCount_.load(std::memory_order_acquire),
             monitorSends))
            return "MONITOR output ownership or send plan invalid";
        std::array<ChannelEdit,count> proposed{};
        for(int i=0;i<count;++i) {
            const auto& strip=cfg.stripsAndMaster.strips[i];
            // The runtime control's bool denotes MONO PAN law, not merely
            // the selected plugin processing width. A single physical MONO
            // input projected into a stereo channel must keep equal-power
            // panning, never jump +3 dB when a live fader or PAN changes.
            proposed[i]={strip.faderLinear,strip.pan,strip.mute,strip.solo,
                plan.channels.effectiveFormats[i]==armix::mixer::ChannelFormat::mono ||
                strip.source==armix::mixer::SourceMode::mono};
            if(!next->prepared->setStripControl(i,proposed[i].linear,
                   proposed[i].pan,proposed[i].mono,
                   proposed[i].mute,proposed[i].solo))
                return "Cannot prepare native channel controls";
        }
        // P1J: a structural APPLY owns only the new topology. Retain a
        // same-strip/same-ID prepared VST3 from this device epoch, including
        // its vendor parameter state and reverb/delay tail. A prior snapshot
        // may remain leased by one callback: shared_ptr copy is CONTROL only,
        // and FxInstance has an atomic try-enter to guard against overlap.
        // Never reuse across a device epoch or changed device quantum.
        const auto* prior=publisher_.controlPrepared(currentEpoch,expectedRevision);
        int insertCount=0,reusedCount=0;
        for(int lane=0;lane<fxSlots;++lane) {
            // FX ownership is independent of assigned hardware PCM.
            // Disconnected lanes remain silent in the already-compiled
            // render graph, but can retain the real prepared VST3 instance.
            const juce::String label=lane<count
                ?"CH "+juce::String(lane+1)
                :"AUX "+juce::String(lane-count+1);
            bool hole=false;
            std::array<bool,maxFxPerLane> consumed{};
            for(int slot=0;slot<maxFxPerLane;++slot) {
                const auto& req=selections[lane][slot];
                if(req.id.isEmpty()){hole=true;continue;}
                if(hole)return label+": FX chain must be contiguous; remove empty slots";
                bool reused=false;
                if(prior && !restores){
                    // Restoring a Show MUST create independent prepared VST
                    // instances; never call setStateInformation on a live one.
                    // Keep exact position first for ordinary graph edits.
                    // Reorder searches other old positions only if necessary.
                    for(int pass=0;pass<2&&!reused;++pass)
                        for(int old=0;old<prior->insertsOn(lane);++old) {
                            if(consumed[old] || (pass==0)!=(old==slot) ||
                               prior->insertId(lane,old)!=req.id)continue;
                            if(next->prepared->reuseAt(lane,slot,*prior,old,req.id)){
                                consumed[old]=true;reused=true;++insertCount;++reusedCount;break;
                            }
                        }
                }
                if(reused)continue; // DO NOT restart unchanged vendor/Orbit/tail.
                if(!(sampleRate>0.0) || deviceFrames<=0)
                    return "Audio device sample rate or buffer size is invalid";
                auto* fmt=formats.getFormat(0);
                if(!fmt || !fmt->fileMightContainThisPluginType(req.path))
                    return label+": VST3 module unavailable; rescan catalog";
                juce::OwnedArray<juce::PluginDescription> descriptions;
                fmt->findAllTypesForFile(descriptions,req.path);
                juce::PluginDescription* chosen=nullptr;
                for(auto* item:descriptions)
                    if(item && item->uniqueId==req.uid && !item->isInstrument)
                        {chosen=item;break;}
                if(!chosen)
                    return label+": selected effect identity is unavailable or instrument";
                juce::String pluginError;
                auto instance=formats.createPluginInstance(*chosen,sampleRate,
                    juce::jmax(32,deviceFrames),pluginError);
                if(!instance)
                    return label+": slot "+juce::String(slot+1)+
                        " VST3 instantiation failed: "+pluginError;
                const auto layoutError=armix::vst::prepareStereoEffectLayout(*instance);
                if(layoutError.isNotEmpty())return label+": "+layoutError;
                try {
                    instance->prepareToPlay(sampleRate,juce::jmax(32,deviceFrames));
                } catch(const std::exception& ex) {
                    return label+": prepare failed: "+juce::String(ex.what());
                } catch(...) {return label+": prepare failed";}
                if(restores) {
                    const auto& saved=(*restores)[lane][slot];
                    try {
                        instance->setStateInformation(saved.binary->getData(),
                            static_cast<int>(saved.binary->getSize()));
                    } catch(const std::exception& ex) {
                        return label+": vendor VST3 restore failed: "+juce::String(ex.what());
                    } catch(...) {return label+": vendor VST3 restore failed";}
                }
                if(instance->getLatencySamples()!=0)
                    return label+": VST3 slot "+juce::String(slot+1)+
                        " reports latency; PDC is not yet implemented";
                if(!next->prepared->installAt(lane,slot,std::move(instance),chosen->name,req.id))
                    return label+": invalid prepared insert slot";
                if(restores && (*restores)[lane][slot].bypass &&
                   !next->prepared->setInsertBypass(lane,slot,
                       next->prepared->insertInstanceId(lane,slot),true))
                    return label+": cannot restore plugin bypass";
                ++insertCount;
            }
        }
        const auto epochAtPublish=deviceEpoch_.load(std::memory_order_acquire);
        if(!registryValid_.load(std::memory_order_acquire) ||
           epochAtPublish!=currentEpoch)
            return "Audio device changed before routing publication";
        const auto result=publisher_.publish(std::move(next),expectedRevision,epochAtPublish);
        switch(result) {
            case Publisher::Result::applied:
                publishedEpoch_.store(currentEpoch,std::memory_order_release);
                enabled_.store(true,std::memory_order_release);
                sendCount_.store(sends.count,std::memory_order_relaxed);
                fxCount_.store(insertCount,std::memory_order_relaxed);
                fxReused_.store(reusedCount,std::memory_order_relaxed);
                // Native control state is the routing SSOT. The GUI only
                // displays an acknowledged immutable copy; draft changes
                // never update this until graph publication succeeds.
                acceptedRoute_=explicitSelection?command:juce::var();
                // Retain the REAL prepared (possibly UNPATCHED/SILENT) M4
                // input topology so a single native SOURCE selection can
                // compile the first mono route. No UI draft is authoritative.
                sourceTemplate_=command;
                acceptedFxSelections_=selections;
                // Release-store only AFTER a valid immutable graph publishes.
                // Even if OUTPUT is mistakenly armed, unpatched renders never
                // enter ANY channel/master vendor DSP or produce sound.
                userPatched_.store(explicitSelection,std::memory_order_release);
                channelEdits_=proposed; // control thread SSOT for this route revision
                for(int i=0;i<count;++i)channelFxNames_[i]=selections[i][0].name;
                for(int b=0;b<auxCount;++b)
                    auxFxNames_[b]=selections[count+b][0].name;
                auxCountActive_.store(
                    static_cast<int>(std::count_if(plan.aux.buses.begin(),
                        plan.aux.buses.end(),
                        [](const auto& b){return b.enabled;})),
                    std::memory_order_relaxed);
                auxSendCount_.store(decoded.aux.count,std::memory_order_relaxed);
                return {};
            case Publisher::Result::staleDevice:
                return "Device epoch changed during preparation";
            case Publisher::Result::staleRevision:
                return "Routing changed elsewhere: refresh before applying";
            case Publisher::Result::retirementFull:
                return "Routing edits are too fast; retry after callback retirement";
            default:
                return "Invalid prepared route";
        }
    }

    // M1 direct-source parameter: only same-format physical→physical
    // input on an ALREADY scheduled channel. Route DAG, VST3 lease, FX
    // identity and hardware output ownership are immutable. Epoch/revision
    // and actual enabled sparse port mask are checked on CONTROL thread.
    juce::String switchPhysicalSource(int strip,std::uint64_t expectedEpoch,
            std::uint64_t expectedRevision,const juce::String& mode,
            int physicalLeft,int physicalRight) noexcept {
        if(strip<0||strip>=count||!hasExplicitRoute()||
           expectedEpoch!=epoch()||expectedRevision!=revision()||
           topologyFault_.load(std::memory_order_acquire))
            return "SOURCE SWITCH: stale native device, graph or channel";
        if(mode!="MONO"&&mode!="STEREO")
            return "SOURCE SWITCH: existing MONO/STEREO format required";
        auto channels=acceptedRoute_.getProperty("channels",juce::var());
        auto* rows=channels.getArray();
        if(!rows||rows->size()!=count)
            return "SOURCE SWITCH: missing accepted input routing";
        auto& row=rows->getReference(strip);
        if(row.getProperty("mode",{}).toString()!=mode)
            return "SOURCE SWITCH: channel format/topology change requires MUTE + APPLY";
        const int n=portCount_.load(std::memory_order_acquire);
        int callbackLeft=-1,callbackRight=-1;
        for(int i=0;i<n;++i){
            const int p=enabledPorts_[i].load(std::memory_order_relaxed);
            if(p==physicalLeft)callbackLeft=i;
            if(p==physicalRight)callbackRight=i;
        }
        if(mode=="MONO")callbackRight=callbackLeft;
        if(callbackLeft<0||callbackRight<0||
           (mode=="STEREO"&&(callbackLeft==callbackRight||physicalLeft==physicalRight)))
            return "SOURCE SWITCH: input is not in verified enabled device pins";
        if(expectedEpoch!=epoch()||!registryValid_.load(std::memory_order_acquire))
            return "SOURCE SWITCH: audio device changed during selection";
        const auto format=mode=="MONO"
            ?armix::mixer::SourceMode::mono:armix::mixer::SourceMode::stereo;
        if(!publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {
                return fx.switchPhysicalSource(strip,format,callbackLeft,callbackRight);
            }))
            return "SOURCE SWITCH: native graph rejected same-format input";
        // This is the authoritative native parameter target, not an
        // unacknowledged browser draft. The callback fades PCM over 64 samples.
        if(auto* object=row.getDynamicObject()){
            object->setProperty("left",physicalLeft);
            object->setProperty("right",mode=="STEREO"?physicalRight:-1);
        }
        return {};
    }

    // A source changing from NONE to MONO/STEREO needs a new prepared
    // topology. A source changing physical pin within the same format uses
    // the existing zero-heap atomic source switch above. Both cases are
    // handled through ONE native command and ONE ACK, not GUI APPLY wizard.
    // All heavy preparation/reuse and deep cloning happen on message thread.
    juce::String repatchPhysicalSource(int strip,std::uint64_t expectedEpoch,
            std::uint64_t expectedRevision,const juce::String& mode,
            int physicalLeft,int physicalRight,bool directPermitted,
            bool independentClock,int occupiedCh1Fx,
            juce::AudioPluginFormatManager& formats,double sampleRate,
            int deviceFrames)
    {
        if(!ready() || !registryValid_.load(std::memory_order_acquire) ||
           topologyFault_.load(std::memory_order_acquire) ||
           expectedEpoch!=epoch() || expectedRevision!=revision() ||
           !sourceTemplate_.isObject())
            return "SOURCE: native device, graph or accepted template stale";
        const auto sourceRows=sourceTemplate_.getProperty("channels",juce::var());
        const auto* rows=sourceRows.getArray();
        if(!rows || strip<0 || strip>=count || rows->size()!=count)
            return "SOURCE: target channel unavailable";
        // Preserve the proven live-safe atomic path for an already scheduled
        // channel. Never force a vendor graph rebuild for ordinary pin moves.
        if(hasExplicitRoute() &&
           rows->getReference(strip).getProperty("mode",{}).toString()==mode)
            return switchPhysicalSource(strip,expectedEpoch,expectedRevision,
                                        mode,physicalLeft,physicalRight);
        const int available=portCount_.load(std::memory_order_acquire);
        if(available<=0||available>armix::device::LiveInputPortRegistry::limit)
            return "SOURCE: driver did not verify active capture pins";
        std::array<int,armix::device::LiveInputPortRegistry::limit> pins{};
        for(int i=0;i<available;++i)
            pins[i]=enabledPorts_[i].load(std::memory_order_relaxed);
        juce::var staged;
        const auto error=armix::mixer::NativeVerifiedSourcePatch::prepare(
            sourceTemplate_,strip,mode,physicalLeft,physicalRight,
            {pins.data(),static_cast<std::size_t>(available)},
            expectedEpoch,expectedRevision,staged);
        if(error.isNotEmpty())return error;
        return publishRevisedTopology(staged,expectedEpoch,expectedRevision,
             directPermitted,independentClock,occupiedCh1Fx,formats,
             sampleRate,deviceFrames);
    }

    // One-click channel OUT and format: the existing native M4 routing/FX
    // graph is the sole PCM owner. Prepare OUT/format on the JUCE control
    // thread, then publish with exactly the same epoch/VST3 lease/Show
    // rollback transaction as SOURCE. No device restart or UI-side routing.
    juce::String repatchChannelStructure(int strip,
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            const juce::String& operation,const juce::String& requested,
            bool directPermitted,bool independentClock,int occupiedCh1Fx,
            juce::AudioPluginFormatManager& formats,double sampleRate,
            int deviceFrames) {
        if(!ready()||!registryValid_.load(std::memory_order_acquire)||
           topologyFault_.load(std::memory_order_acquire)||
           epoch()!=expectedEpoch||revision()!=expectedRevision||
           !sourceTemplate_.isObject())
            return "CHANNEL PATCH: native device or graph is stale";
        if(strip<0||strip>=count)
            return "CHANNEL PATCH: invalid native channel";
        // This preserves all source pins, AUX taps, master and monitor
        // destinations, original VST3 vendor state and all other channels.
        auto staged=juce::JSON::parse(juce::JSON::toString(sourceTemplate_,false));
        if(!staged.isObject())return "CHANNEL PATCH: cannot clone accepted graph";
        auto rows=staged.getProperty("channels",juce::var());
        auto* list=rows.getArray();
        if(!list||list->size()!=count||
           !list->getReference(strip).isObject())
            return "CHANNEL PATCH: no accepted channel row";
        auto* row=list->getReference(strip).getDynamicObject();
        if(operation=="SET_PRIMARY_OUT") {
            if(requested!="MAIN"&&requested!="NONE")
                return "CHANNEL PATCH: only MAIN and NONE are installed M4 outputs";
            row->setProperty("main",requested=="MAIN");
        } else if(operation=="SET_PROCESSING_FORMAT") {
            if(requested!="MONO"&&requested!="STEREO")
                return "CHANNEL PATCH: processing format must be MONO or STEREO";
            const auto schema=staged.getProperty("schemaVersion",juce::var());
            if(!schema.isInt() || static_cast<int>(schema)!=2)
                return "CHANNEL PATCH: typed processing format requires native schema v2";
            row->setProperty("channelFormat",requested);
        } else return "CHANNEL PATCH: unsupported graph operation";
        return publishRevisedTopology(staged,expectedEpoch,expectedRevision,
             directPermitted,independentClock,occupiedCh1Fx,formats,
             sampleRate,deviceFrames);
    }

    // Operator one-action AUX send connection: prepare a real parallel
    // edge in the SAME M4 graph that owns the channel VST3/AUX rack.
    // The recipient must already own a genuine prepared effect. An empty
    // AUX summed into MAIN doubles the dry path and is rejected.
    juce::String connectPreparedAuxSend(int source,int bus,
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            armix::mixer::SendTap tap,float gain,float pan,
            bool directPermitted,bool independentClock,int occupiedCh1Fx,
            juce::AudioPluginFormatManager& formats,double sampleRate,
            int deviceFrames) {
        if(!hasExplicitRoute()||!ready()||!registryValid_.load(std::memory_order_acquire)||
           epoch()!=expectedEpoch||revision()!=expectedRevision||
           topologyFault_.load(std::memory_order_acquire)||
           source<0||source>=count||bus<0||bus>=auxCount||
           (tap!=armix::mixer::SendTap::preFader&&
            tap!=armix::mixer::SendTap::postFader)||
           !std::isfinite(gain)||gain<0.f||gain>1.f||
           !std::isfinite(pan)||pan< -1.f||pan>1.f)
            return "AUX CONNECT: invalid source, send or stale native graph";
        const auto* old=publisher_.controlPrepared(expectedEpoch,expectedRevision);
        if(!old||old->insertsOn(count+bus)<=0)
            return "AUX CONNECT: insert a real effect on AUX first; an empty return doubles dry MAIN";
        auto staged=juce::JSON::parse(juce::JSON::toString(sourceTemplate_,false));
        if(!staged.isObject())return "AUX CONNECT: accepted topology unavailable";
        const auto lanes=staged.getProperty("channels",juce::var());
        const auto* sources=lanes.getArray();
        if(!sources||sources->size()!=count||
           sources->getReference(source).getProperty("mode","NONE").toString()=="NONE")
            return "AUX CONNECT: select a verified physical source first";
        auto aux=staged.getProperty("auxes",juce::var());
        if(aux.isVoid()) {
            juce::Array<juce::var> defaults;
            for(int b=0;b<auxCount;++b) {
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("enabled",false);
                row->setProperty("main",true);
                row->setProperty("mute",false);
                row->setProperty("gain",1.0);
                defaults.add(juce::var(row.release()));
            }
            staged.getDynamicObject()->setProperty("auxes",juce::var(defaults));
            aux=staged.getProperty("auxes",juce::var());
        }
        auto* auxes=aux.getArray();
        if(!auxes||auxes->size()!=auxCount)return "AUX CONNECT: corrupt return roster";
        auto* returnRow=auxes->getReference(bus).getDynamicObject();
        if(!returnRow)return "AUX CONNECT: invalid return";
        if(!static_cast<bool>(returnRow->getProperty("enabled"))) {
            returnRow->setProperty("enabled",true);
            returnRow->setProperty("main",true); // one-click parallel effect to MAIN
        }
        auto rawSends=staged.getProperty("sends",juce::var());
        if(rawSends.isVoid()) {
            staged.getDynamicObject()->setProperty("sends",
                juce::var(juce::Array<juce::var>{}));
            rawSends=staged.getProperty("sends",juce::var());
        }
        auto* sendRows=rawSends.getArray();
        if(!sendRows)return "AUX CONNECT: invalid accepted sends";
        juce::DynamicObject* existing=nullptr;
        for(auto& item:*sendRows) {
            if(static_cast<int>(item.getProperty("from",-1))==source &&
               static_cast<int>(item.getProperty("to",-1))==count+bus) {
                existing=item.getDynamicObject();
                if(!existing)return "AUX CONNECT: invalid existing edge";
                break;
            }
        }
        if(!existing) {
            if(sendRows->size()>=armix::mixer::maxChannelSends)
                return "AUX CONNECT: send capacity exhausted";
            auto row=std::make_unique<juce::DynamicObject>();
            existing=row.get();
            sendRows->add(juce::var(row.release()));
        }
        existing->setProperty("from",source);
        existing->setProperty("to",count+bus);
        existing->setProperty("tap",tap==armix::mixer::SendTap::preFader?"PRE":"POST");
        existing->setProperty("gain",static_cast<double>(gain));
        existing->setProperty("pan",static_cast<double>(pan));
        return publishRevisedTopology(staged,expectedEpoch,expectedRevision,
            directPermitted,independentClock,occupiedCh1Fx,formats,
            sampleRate,deviceFrames);
    }

    // Both SOURCE and channel structure edits share one graph compiler,
    // VST3 lease policy and rollback path. Keep fast faders/pan/mute/solo
    // on their separate atomic parameter path without any compilation.
    juce::String publishRevisedTopology(juce::var& staged,
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            bool directPermitted,bool independentClock,int occupiedCh1Fx,
            juce::AudioPluginFormatManager& formats,double sampleRate,
            int deviceFrames) {
        auto stagedRows=staged.getProperty("channels",juce::var());
        auto* editable=stagedRows.getArray();
        if(!editable||editable->size()!=count)
            return "CHANNEL PATCH: malformed native topology";
        for(int i=0;i<count;++i) {
            auto* row=editable->getReference(i).getDynamicObject();
            if(!row)return "CHANNEL PATCH: missing native channel";
            const auto& c=channelEdits_[i];
            row->setProperty("gain",static_cast<double>(c.linear));
            row->setProperty("pan",static_cast<double>(c.pan));
            row->setProperty("mute",c.mute);
            row->setProperty("solo",c.solo);
        }
        if(expectedEpoch!=epoch()||expectedRevision!=revision()||
           !registryValid_.load(std::memory_order_acquire))
            return "CHANNEL PATCH: device changed; previous mix preserved";
        return apply(staged,directPermitted,independentClock,occupiedCh1Fx,
                     acceptedFxSelections_,formats,sampleRate,deviceFrames,true);
    }

    // Native live control path. Only the JUCE message thread writes. No
    // graph recompilation, VST3 instantiation or callback detach.
    // Browser carries the exact acknowledged route token to reject stale
    // events following hotplug, reload or another user's APPLY transaction.
    bool setChannelParameter(int strip,std::uint64_t expectedEpoch,
                             std::uint64_t expectedRevision,
                             const juce::String& name,float value) noexcept {
        if(strip<0||strip>=count || !ready() ||
           expectedEpoch!=epoch() || expectedRevision!=revision())return false;
        auto updated=channelEdits_[strip];
        if(name=="faderDb") {
            if(!std::isfinite(value)||value< -60.f||value>10.f)return false;
            updated.linear=value<= -60.f?0.f:std::pow(10.f,value/20.f);
        }else if(name=="pan") {
            if(!std::isfinite(value)||value< -100.f||value>100.f)return false;
            updated.pan=value*.01f;
        }else if(name=="mute"||name=="solo") {
            if(value!=0.f && value!=1.f)return false;
            if(name=="mute")updated.mute=value>0.5f;
            else updated.solo=value>0.5f;
        }else return false;
        if(!publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {
                return fx.setStripControl(strip,updated.linear,updated.pan,
                    updated.mono,updated.mute,updated.solo);
            }))return false;
        channelEdits_[strip]=updated;
        return true;
    }

    // Real AUX FX RETURN: one existing PRE or POST send has an independent
    // realtime level. The tap/destination/edge identity are immutable here.
    // A single render of source PCM, vendor VST, and AUX return remains
    // authoritative on the installed JUCE callback.
    // The UI may adjust it while physical output is ARMED; no new plugin,
    // graph, callback or device restart occurs.
    bool setAuxSendGain(int source,int bus,std::uint64_t expectedEpoch,
                        std::uint64_t expectedRevision,float gain,
                        armix::mixer::SendTap tap=armix::mixer::SendTap::postFader) noexcept {
        if(source<0||source>=count||bus<0||bus>=auxCount||
           !std::isfinite(gain)||gain<0.f||gain>1.f||
           !ready()||!hasExplicitRoute()||
           expectedEpoch!=epoch()||expectedRevision!=revision())return false;
        auto sends=acceptedRoute_.getProperty("sends",juce::var());
        auto* list=sends.getArray();
        if(!list)return false;
        juce::var* accepted=nullptr;
        for(auto& item:*list) {
            if(static_cast<int>(item.getProperty("from",-1))==source &&
               static_cast<int>(item.getProperty("to",-1))==count+bus &&
               item.getProperty("tap",{}).toString()==
                   (tap==armix::mixer::SendTap::preFader?"PRE":"POST")) {
                accepted=&item;break;
            }
        }
        if(!accepted)return false; // No implicit creation of an AUX edge.
        if(!publisher_.updateActiveControl(expectedEpoch,expectedRevision,
           [&](FxGraph& fx) noexcept {
               return fx.setAuxSendGain(source,bus,tap,gain);
           }))return false;
        accepted->getDynamicObject()->setProperty("gain",static_cast<double>(gain));
        return true;
    }

    // M1: an existing ENABLED AUX FX return has its own realtime master
    // fader, applied after AUX inserts and before both MAIN and MONITOR.
    // Only one GUI/control writer, never plugin/graph changes in the callback.
    bool setAuxBusGain(int bus,std::uint64_t expectedEpoch,
                       std::uint64_t expectedRevision,float gain) noexcept {
        if(bus<0||bus>=auxCount||!std::isfinite(gain)||gain<0.f||gain>4.f||
           !ready()||!hasExplicitRoute()||expectedEpoch!=epoch()||
           expectedRevision!=revision())return false;
        auto auxes=acceptedRoute_.getProperty("auxes",juce::var());
        auto* arr=auxes.getArray();
        if(!arr||bus>=arr->size()||!(*arr)[bus].getProperty("enabled",false))
            return false;
        if(!publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {return fx.setAuxBusGain(bus,gain);}))
            return false;
        if(auto* obj=(*arr)[bus].getDynamicObject())
            obj->setProperty("gain",static_cast<double>(gain));
        return true;
    }

    // Independent physical MONITOR/CUE bus: control only changes one
    // existing send's target. PRE/POST AUX and MAIN VST PCM remain unchanged.
    // If no verified distinct MONITOR output pair exists, reject.
    bool setMonitorSendGain(int source,std::uint64_t expectedEpoch,
                            std::uint64_t expectedRevision,float gain) noexcept {
        if(source<0||source>=fxSlots||!std::isfinite(gain)||
           gain<0.f||gain>1.f||!hasExplicitRoute()||
           expectedEpoch!=epoch()||expectedRevision!=revision())
            return false;
        auto gains=acceptedRoute_.getProperty("monitorSends",juce::var());
        auto* array=gains.getArray();
        const auto* current=publisher_.controlPrepared(expectedEpoch,expectedRevision);
        if(!array||array->size()!=fxSlots||!current||
           !current->monitorEnabled())return false;
        if(!publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {
                return fx.setMonitorSendGain(source,gain);
            }))return false;
        array->set(source,static_cast<double>(gain)); // same accepted topology
        return true;
    }

    // M5 native ChannelId VST3 migration: capture actual current M4 vendor
    // processor LEASES, not catalog names or copied binary state. The writer
    // must hold these control-owned leases within its new published dynamic
    // PCM snapshot until final callback retirement. No callback activity,
    // device reopen, preset restoration or rack instantiation occurs here.
    // The host's special legacy CH1 external chain remains a SEPARATE lease;
    // do not cut over a loaded external CH1 graph without transferring it.
    bool captureDynamicFxLeases(
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            std::span<const armix::mixer::ChannelId> laneIds,
            armix::mixer::DynamicChannelFxLeaseBank& destination,
            juce::String& error) const {
        using Bank=armix::mixer::DynamicChannelFxLeaseBank;
        if(laneIds.size()!=static_cast<std::size_t>(fxSlots) ||
           !hasExplicitRoute() || !ready() ||
           expectedEpoch!=epoch() || expectedRevision!=revision()) {
            error="M5: native epoch/revision or 10CH+4AUX FX roster stale";
            return false;
        }
        const FxGraph* current=publisher_.controlPrepared(expectedEpoch,expectedRevision);
        if(!current) {
            error="M5: no current M4 prepared vendor FX graph";
            return false;
        }
        std::vector<Bank::Assignment> staged;
        staged.reserve(fxSlots);
        for(int lane=0;lane<fxSlots;++lane) {
            Bank::Assignment slot;
            // Mono input is projected into the prepared stereo plugin rack;
            // the actual negotiated M4 VST is always stereo. The native
            // ChannelId PCM renderer owns mono pan law before GROUP sends.
            const auto format=armix::mixer::ProcessingFormat::stereo;
            if(!Bank::captureLegacy(lane,laneIds[static_cast<std::size_t>(lane)],
                                    format,*current,slot)) {
                error="M5: failed to retain M4 VST3 token for lane "+
                      juce::String(lane);
                return false;
            }
            staged.push_back(std::move(slot));
        }
        const auto fault=Bank::prepare(expectedEpoch,expectedRevision,staged,
                                      destination);
        if(fault!=Bank::Fault::none) {
            error="M5: ChannelId VST lease migration rejected, fault "+
                  juce::String(static_cast<int>(fault));
            return false;
        }
        error.clear();
        return true;
    }

    // Live, nonstructural FX power control; all guards bound to a real ACK.
    bool setFxBypass(int lane,int slot,std::uint64_t expectedEpoch,
                     std::uint64_t expectedRevision,const juce::String& token,
                     bool bypass) noexcept {
        if(lane<0||lane>=fxSlots||slot<0||slot>=maxFxPerLane||
           !hasExplicitRoute()||expectedEpoch!=epoch()||
           expectedRevision!=revision()||token.isEmpty())return false;
        return publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {
                return fx.setInsertBypass(lane,slot,token,bypass);
            });
    }

    // Quiescent message-thread identity lease for the vendor's native GUI.
    // Shared lifetime is independent of route reorder, but not authority:
    // an editor request MUST match the current accepted route.
    FxGraph::InstanceLease leaseFxEditor(int lane,int slot,
             std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
             const juce::String& token) const noexcept {
        if(lane<0||lane>=fxSlots||slot<0||slot>=maxFxPerLane||
           !hasExplicitRoute()||expectedEpoch!=epoch()||
           expectedRevision!=revision()||token.isEmpty())return {};
        const auto* fx=publisher_.controlPrepared(expectedEpoch,expectedRevision);
        return fx?fx->leaseInsert(lane,slot,token):FxGraph::InstanceLease{};
    }

    // Transactional FX binary capture, CONTROL only. The caller has muted
    // hardware output before entering. A busy vendor gate rejects the entire
    // capture immediately; there is no retry spin or audio callback I/O.
    juce::String captureRoutedFxStates(std::uint64_t expectedEpoch,
                                      std::uint64_t expectedRevision,
                                      std::vector<CapturedFxState>& out) noexcept {
        if(!hasExplicitRoute()||epoch()!=expectedEpoch||
           revision()!=expectedRevision)
            return "Native route changed before FX state capture";
        const auto* graph=publisher_.controlPrepared(expectedEpoch,expectedRevision);
        if(!graph)return "Accepted FX graph unavailable";
        std::vector<CapturedFxState> prepared;
        std::size_t totalBytes=0;
        try {
            for(int lane=0;lane<fxSlots;++lane)
                for(int slot=0;slot<graph->insertsOn(lane);++slot) {
                    CapturedFxState row;
                    row.lane=lane;row.slot=slot;
                    row.catalogId=graph->insertId(lane,slot);
                    row.instanceId=graph->insertInstanceId(lane,slot);
                    row.bypass=graph->insertBypassed(lane,slot);
                    const auto instance=graph->leaseInsert(lane,slot,row.instanceId);
                    if(!instance||row.catalogId.isEmpty()||
                       !instance->tryCaptureState(row.binary))
                        return "Vendor FX state busy/failed: lane "+
                            juce::String(lane+1)+" slot "+juce::String(slot+1);
                    totalBytes+=row.binary.getSize();
                    if(totalBytes>128U*1024U*1024U)
                        return "Native Show FX state exceeds 128 MB";
                    prepared.push_back(std::move(row));
                }
        } catch(...) {return "Native FX state capture failed";}
        if(epoch()!=expectedEpoch||revision()!=expectedRevision)
            return "Native route changed during FX state capture";
        out=std::move(prepared);
        return {};
    }

    void disable() noexcept {
        userPatched_.store(false,std::memory_order_release);
        enabled_.store(false,std::memory_order_release);
        publishedEpoch_.store(0,std::memory_order_release);
        publisher_.unpublish(); // control thread only
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        sendCount_.store(0,std::memory_order_relaxed);
        fxCount_.store(0,std::memory_order_relaxed);
        fxReused_.store(0,std::memory_order_relaxed);
        acceptedRoute_=juce::var(); // control-thread STOP only
        sourceTemplate_=juce::var();
        acceptedFxSelections_=FxSelections{};
        for(auto& n:auxFxNames_)n.clear();
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksL_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksR_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:auxPeaks_)p.store(0.f,std::memory_order_relaxed);
    }
    void poll() noexcept {publisher_.pollRetirement();} // message thread
    bool active() const noexcept {return enabled_.load(std::memory_order_acquire);}
    float peakLeft() const noexcept {return peakL_.load(std::memory_order_relaxed);}
    float peakRight() const noexcept {return peakR_.load(std::memory_order_relaxed);}
    bool ready() const noexcept {
        return active() && !topologyFault_.load(std::memory_order_acquire)
            && publishedEpoch_.load(std::memory_order_acquire)==epoch();
    }
    // Status / control thread only. An empty prepared graph is ready, but
    // cannot be advertised as a LIVE patched route or a savable Show.
    bool hasExplicitRoute() const noexcept {
        return ready() && acceptedRoute_.isObject();
    }
    std::uint64_t epoch() const noexcept {
        return deviceEpoch_.load(std::memory_order_acquire);
    }
    std::uint64_t revision() const noexcept {return publisher_.lastRevision();} // control only

    // JUCE render callback, with a verified single-clock device. Routing
    // publishes at the BLOCK boundary. Missing/stale ports output SILENCE.
    // A failed run never falls through to an unrelated microphone/plugin path.
    bool process(const float* const* inputs,int inputCount,
                 float* const* outputs,int outputCount,int frames,
                 float masterGain,bool masterMuted,
                 void* externalContext=nullptr,
                 FxGraph::ExternalStripFx externalFx=nullptr,
                 bool masterMono=false,
                 std::array<int,2>* actualOutputPair=nullptr,
                 std::array<int,2>* actualMonitorPair=nullptr) noexcept {
        if(actualOutputPair)*actualOutputPair={{-1,-1}};
        if(actualMonitorPair)*actualMonitorPair={{-1,-1}};
        if(!active())return false;
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksL_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:channelPeaksR_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:auxPeaks_)p.store(0.f,std::memory_order_relaxed);
        // Stale meters must never survive a failed device/epoch mapping.
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        monitorPeakL_.store(0.f,std::memory_order_relaxed);
        monitorPeakR_.store(0.f,std::memory_order_relaxed);
        if(frames<=0)return true;
        // Defer physical output writes until all capture pointers have been
        // read into the preallocated native graph. Clearing outputs here can
        // erase aliased JUCE/driver input PCM before the CH strips consume it.
        // Every failed/unpatched/MUTED route still uses this fail-silent gate.
        const auto silence=[&]() noexcept {
            if(!outputs)return;
            for(int c=0;c<outputCount;++c)
                if(outputs[c])juce::FloatVectorOperations::clear(outputs[c],frames);
        };
        // Do not require callback ports 0/1 to be writable when MAIN is
        // actually routed to another verified pair. Validate the selected
        // destinations against the leased graph below.
        if(!outputs||outputCount<2 ||
           outputCount!=callbackOutputCount_.load(std::memory_order_acquire) ||
           inputCount<0 || inputCount>armix::device::LiveInputPortRegistry::limit) {
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        if(topologyFault_.load(std::memory_order_acquire)) {
            silence();return true;
        }
        if(inputCount!=callbackInputCount_.load(std::memory_order_acquire)) {
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        // A prepared graph does NOT authorize physical inputs: skip ALL
        // channel, AUX and MASTER vendor processing until explicit native ACK.
        // One atomic read; outputs are cleared by the fail-silent gate above.
        if(!userPatched_.load(std::memory_order_acquire)) {
            fadeRemaining_=0;
            lastOutputL_=lastOutputR_=0.f;
            silence();return true;
        }
        const auto generation=epoch();
        auto lease=publisher_.acquire();
        const auto* snapshot=lease.get(generation);
        if(!snapshot||!snapshot->prepared ||
           inputCount!=callbackInputCount_.load(std::memory_order_acquire)) {
            if(inputCount!=callbackInputCount_.load(std::memory_order_acquire))
                topologyFault_.store(true,std::memory_order_release);
            fadeRemaining_=0;
            lastOutputL_=lastOutputR_=0.f;
            silence();return true;
        }
        // Callback destinations come only from the leased immutable graph.
        // If a driver restarts or changes topology, silence all outputs.
        const int outLeft=snapshot->prepared->mainOutputLeft();
        const int outRight=snapshot->prepared->mainOutputRight();
        if(outLeft<0||outRight<0||outLeft==outRight||
           outLeft>=outputCount||outRight>=outputCount||
           !outputs[outLeft]||!outputs[outRight]){
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        float* const mainL=outputs[outLeft];
        float* const mainR=outputs[outRight];
        float* monitorL=nullptr;
        float* monitorR=nullptr;
        if(snapshot->prepared->monitorEnabled()) {
            const int l=snapshot->prepared->monitorOutputLeft();
            const int r=snapshot->prepared->monitorOutputRight();
            if(l<0||r<0||l>=outputCount||r>=outputCount||
               l==r||l==outLeft||l==outRight||r==outLeft||r==outRight||
               !outputs[l]||!outputs[r]) {
                topologyFault_.store(true,std::memory_order_release);
                silence();return true;
            }
            monitorL=outputs[l];monitorR=outputs[r];
            if(actualMonitorPair)*actualMonitorPair={{l,r}};
        }
        // Return the callback indices from THIS leased prepared graph.
        // The post-render FX transition and meters must not re-read a
        // potentially newer route or assume hardware channels 0/1.
        if(actualOutputPair)*actualOutputPair={{outLeft,outRight}};
        if(snapshot->graphRevision!=audioRevision_ || generation!=audioEpoch_) {
            audioRevision_=snapshot->graphRevision;
            audioEpoch_=generation;
            fadeRemaining_=rampFrames;
            fadeFromL_=lastOutputL_;
            fadeFromR_=lastOutputR_;
        }
        // Strict output mute wins over every topology fade. Output buffers
        // are cleared by the fail-silent gate, so skip all muted DSP work.
        // Prime a 256-sample fade FROM ZERO for the first unmuted block.
        // Never interpolate any previously audible sample while muted.
        if(masterMuted) {
            masterMonoPolicy_.primeWhileMuted(masterMono);
            fadeRemaining_=rampFrames;
            fadeFromL_=fadeFromR_=0.f;
            lastOutputL_=lastOutputR_=0.f;
            silence();return true;
        }
        // Large/variable JUCE quantum is chunked using stack pointer views;
        // no heap allocation or new processing object inside callback.
        std::array<const float*,armix::device::LiveInputPortRegistry::limit> shifted{};
        const int max=snapshot->prepared->maxFramesPerPlugin();
        if(max<=0) {
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        float peakL=0.f,peakR=0.f,monitorPeakL=0.f,monitorPeakR=0.f;
        std::array<float,count> perChannelPeaks{};
        std::array<float,count> perChannelPeaksL{},perChannelPeaksR{};
        std::array<float,auxCount> perAuxPeaks{};
        for(int offset=0;offset<frames;offset+=max) {
            const int n=std::min(max,frames-offset);
            for(int c=0;c<inputCount;++c)
                shifted[c]=inputs&&inputs[c]?inputs[c]+offset:nullptr;
            armix::mixer::RenderLevels level;
            if(!snapshot->prepared->process(shifted.data(),inputCount,
                   mainL+offset,mainR+offset,n,&level,
                   externalContext,externalFx,
                   monitorL?monitorL+offset:nullptr,
                   monitorR?monitorR+offset:nullptr)) {
                // A failed DSP pass must never leak partially rendered
                // MAIN/MONITOR PCM into an armed physical output.
                topologyFault_.store(true,std::memory_order_release);
                silence();return true;
            }
            for(int ch=0;ch<count;++ch) {
                perChannelPeaks[ch]=std::max(perChannelPeaks[ch],level.channelPeak[ch]);
                perChannelPeaksL[ch]=std::max(perChannelPeaksL[ch],level.channelPeakL[ch]);
                perChannelPeaksR[ch]=std::max(perChannelPeaksR[ch],level.channelPeakR[ch]);
            }
            for(int b=0;b<auxCount;++b)
                perAuxPeaks[b]=std::max(perAuxPeaks[b],level.auxPeak[b]);
            const float g=std::clamp(masterGain,0.f,2.f);
            for(int i=0;i<n;++i) {
                float l=mainL[offset+i]*g;
                float r=mainR[offset+i]*g;
                // Both physical buses must prime from SILENCE on global
                // output unmute. MONITOR deliberately has no master gain,
                // but shares the same 256-sample startup de-click ramp.
                float busRamp=1.f;
                if(fadeRemaining_>0) {
                    busRamp=1.f-static_cast<float>(fadeRemaining_-1)/
                                          static_cast<float>(rampFrames);
                    l=fadeFromL_+(l-fadeFromL_)*busRamp;
                    r=fadeFromR_+(r-fadeFromR_)*busRamp;
                    --fadeRemaining_;
                }
                // Sample-smoothed, zero-heap dual-mono MASTER output policy.
                masterMonoPolicy_.process(masterMono,l,r);
                mainL[offset+i]=l;
                mainR[offset+i]=r;
                lastOutputL_=l;
                lastOutputR_=r;
                peakL=std::max(peakL,std::abs(l));
                peakR=std::max(peakR,std::abs(r));
                if(monitorL) {
                    const float ml=monitorL[offset+i]*busRamp;
                    const float mr=monitorR[offset+i]*busRamp;
                    monitorL[offset+i]=ml;
                    monitorR[offset+i]=mr;
                    monitorPeakL=std::max(monitorPeakL,std::abs(ml));
                    monitorPeakR=std::max(monitorPeakR,std::abs(mr));
                }
            }
        }
        // All input PCM has been staged/processed by the native graph.
        // Only now silence unselected physical ports; do not clobber an
        // aliased MAIN/MONITOR destination reported twice by a driver.
        for(int c=0;c<outputCount;++c)
            if(outputs[c] && outputs[c]!=mainL && outputs[c]!=mainR &&
               outputs[c]!=monitorL && outputs[c]!=monitorR)
                juce::FloatVectorOperations::clear(outputs[c],frames);
        monitorPeakL_.store(monitorPeakL,std::memory_order_relaxed);
        monitorPeakR_.store(monitorPeakR,std::memory_order_relaxed);
        peakL_.store(peakL,std::memory_order_relaxed);
        peakR_.store(peakR,std::memory_order_relaxed);
        for(int ch=0;ch<count;++ch) {
            channelPeaks_[ch].store(perChannelPeaks[ch],std::memory_order_relaxed);
            channelPeaksL_[ch].store(perChannelPeaksL[ch],std::memory_order_relaxed);
            channelPeaksR_[ch].store(perChannelPeaksR[ch],std::memory_order_relaxed);
        }
        for(int b=0;b<auxCount;++b)
            auxPeaks_[b].store(perAuxPeaks[b],std::memory_order_relaxed);
        return true;
    }

    juce::var status(juce::AudioIODevice* device) const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("topologyFault",topologyFault_.load(std::memory_order_acquire));
        out->setProperty("ready",ready());
        out->setProperty("hasAcceptedRoute",hasExplicitRoute());
        out->setProperty("epoch",static_cast<juce::int64>(epoch()));
        out->setProperty("revision",static_cast<juce::int64>(revision()));
        out->setProperty("peakL",peakL_.load(std::memory_order_relaxed));
        out->setProperty("peakR",peakR_.load(std::memory_order_relaxed));
        out->setProperty("monitorPeakL",monitorPeakL_.load(std::memory_order_relaxed));
        out->setProperty("monitorPeakR",monitorPeakR_.load(std::memory_order_relaxed));
        out->setProperty("sendCount",sendCount_.load(std::memory_order_relaxed));
        out->setProperty("fxCount",fxCount_.load(std::memory_order_relaxed));
        out->setProperty("auxCount",auxCountActive_.load(std::memory_order_relaxed));
        out->setProperty("auxSendCount",auxSendCount_.load(std::memory_order_relaxed));
        juce::Array<juce::var> auxNames,auxPeaks;
        for(int b=0;b<auxCount;++b){
            auxNames.add(auxFxNames_[b]);
            auxPeaks.add(auxPeaks_[b].load(std::memory_order_relaxed));
        }
        out->setProperty("auxFxNames",juce::var(auxNames));
        out->setProperty("auxPeaks",juce::var(auxPeaks));
        out->setProperty("fxReused",fxReused_.load(std::memory_order_relaxed));
        // Control-thread snapshot; the existing channelControls property
        // below is the single authoritative atomic-target readback, while
        // acceptedRoute remains the immutable topology of the same revision.
        if(active() && ready())out->setProperty("acceptedRoute",acceptedRoute_);
        juce::Array<juce::var> liveFxInstances;
        const auto* prepared=hasExplicitRoute()
            ?publisher_.controlPrepared(epoch(),revision()):nullptr;
        for(int lane=0;lane<fxSlots;++lane) {
            juce::Array<juce::var> slots;
            if(prepared)for(int slot=0;slot<prepared->insertsOn(lane);++slot) {
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("instanceId",prepared->insertInstanceId(lane,slot));
                item->setProperty("catalogId",prepared->insertId(lane,slot));
                item->setProperty("bypassed",prepared->insertBypassed(lane,slot));
                slots.add(juce::var(item.release()));
            }
            liveFxInstances.add(juce::var(slots));
        }
        out->setProperty("fxInstances",juce::var(liveFxInstances));
        juce::Array<juce::var> controls,fxNames;
        for(int i=0;i<count;++i) {
            const auto& st=channelEdits_[i];
            auto obj=std::make_unique<juce::DynamicObject>();
            obj->setProperty("db",st.linear>0.f?20.f*std::log10(st.linear):-60.f);
            obj->setProperty("linear",st.linear); // exact, including hard -infinity mute
            obj->setProperty("pan",st.pan*100.f);
            obj->setProperty("mute",st.mute);
            obj->setProperty("solo",st.solo);
            controls.add(juce::var(obj.release()));
            fxNames.add(channelFxNames_[i]);
        }
        out->setProperty("channelControls",juce::var(controls));
        out->setProperty("channelFxNames",juce::var(fxNames));
        juce::Array<juce::var> levels;
        for(const auto& p:channelPeaks_)levels.add(p.load(std::memory_order_relaxed));
        out->setProperty("channelPeaks",juce::var(levels));
        // Native 10-channel source is already post-fader; stereo meters
        // must never duplicate its aggregated max onto both L/R LEDs.
        juce::Array<juce::var> leftLevels,rightLevels;
        for(const auto& p:channelPeaksL_)leftLevels.add(p.load(std::memory_order_relaxed));
        for(const auto& p:channelPeaksR_)rightLevels.add(p.load(std::memory_order_relaxed));
        out->setProperty("channelPeaksL",juce::var(leftLevels));
        out->setProperty("channelPeaksR",juce::var(rightLevels));
        juce::Array<juce::var> ports;
        if(device && registryValid_.load(std::memory_order_acquire)) {
            const auto generation=epoch();
            const int countNow=portCount_.load(std::memory_order_acquire);
            const auto names=device->getInputChannelNames();
            for(int i=0;i<countNow &&
                       i<armix::device::LiveInputPortRegistry::limit;++i){
                const int physical=enabledPorts_[i].load(std::memory_order_relaxed);
                auto p=std::make_unique<juce::DynamicObject>();
                p->setProperty("port",physical);
                p->setProperty("callback",i);
                p->setProperty("label",physical<names.size()
                    ?names[physical]:juce::String("Input ")+juce::String(physical+1));
                ports.add(juce::var(p.release()));
            }
            // Device may restart while a status report is constructed.
            if(epoch()!=generation ||
               !registryValid_.load(std::memory_order_acquire))
                ports.clear();
        }
        out->setProperty("ports",juce::var(ports));
        juce::Array<juce::var> outputPorts;
        if(registryValid_.load(std::memory_order_acquire)) {
            const int outputs=callbackOutputCount_.load(std::memory_order_acquire);
            for(int i=0;i<outputs && i<armix::device::LiveInputPortRegistry::limit;++i)
                outputPorts.add(enabledOutputPorts_[i].load(std::memory_order_relaxed));
        }
        out->setProperty("outputPorts",juce::var(outputPorts));
        juce::Array<juce::var> liveMain;
        const auto* mainGraph=publisher_.controlPrepared(epoch(),revision());
        if(mainGraph&&ready()&&registryValid_.load(std::memory_order_acquire)){
            const int l=mainGraph->mainOutputLeft(),r=mainGraph->mainOutputRight();
            const int n=callbackOutputCount_.load(std::memory_order_acquire);
            if(l>=0&&r>=0&&l<n&&r<n){
                liveMain.add(enabledOutputPorts_[l].load(std::memory_order_relaxed));
                liveMain.add(enabledOutputPorts_[r].load(std::memory_order_relaxed));
            }
        }
        out->setProperty("mainOutputs",juce::var(liveMain));
        juce::Array<juce::var> liveMonitor,liveMonitorSends;
        if(mainGraph && mainGraph->monitorEnabled() &&
           ready() && registryValid_.load(std::memory_order_acquire)) {
            const int l=mainGraph->monitorOutputLeft();
            const int r=mainGraph->monitorOutputRight();
            const int n=callbackOutputCount_.load(std::memory_order_acquire);
            if(l>=0&&r>=0&&l<n&&r<n) {
                liveMonitor.add(enabledOutputPorts_[l].load(std::memory_order_relaxed));
                liveMonitor.add(enabledOutputPorts_[r].load(std::memory_order_relaxed));
                for(float gain:mainGraph->monitorGains())liveMonitorSends.add(gain);
            }
        }
        out->setProperty("monitorOutputs",juce::var(liveMonitor));
        out->setProperty("monitorSends",juce::var(liveMonitorSends));
        out->setProperty("note","Native 10CH+4AUX production mixer; unpatched startup is SILENT. Explicit input assignment/ACK required. Hardware FAT unqualified.");
        return juce::var(out.release());
    }
private:
    Publisher publisher_;
    std::atomic<float> monitorPeakL_{0.f},monitorPeakR_{0.f};
    armix::dsp::MasterOutputMonoPolicy masterMonoPolicy_; // audio thread only
    std::array<ChannelEdit,count> channelEdits_{}; // JUCE message thread only
    std::array<juce::String,count> channelFxNames_{}; // status/control thread only
    std::array<juce::String,auxCount> auxFxNames_{}; // dedicated bus FX
    std::array<std::atomic<float>,auxCount> auxPeaks_{};
    std::atomic<int> auxCountActive_{0},auxSendCount_{0};
    juce::var acceptedRoute_; // native ACK only, no UI preview/localStorage truth
    juce::var sourceTemplate_; // last prepared M4 template, including SILENT/NONE startup
    FxSelections acceptedFxSelections_{}; // CONTROL ONLY: original VST3 identity for FX reuse
    // All data shared between JUCE device lifecycle and GUI/control reads
    // are atomic; the callback never enumerates masks or channel names.
    std::array<std::atomic<int>,armix::device::LiveInputPortRegistry::limit> enabledPorts_{};
    std::array<std::atomic<int>,armix::device::LiveInputPortRegistry::limit> enabledOutputPorts_{};
    std::atomic<int> portCount_{0};
    std::atomic<bool> registryValid_{false};
    std::atomic<bool> topologyFault_{false};
    std::atomic<std::uint64_t> deviceEpoch_{0};
    std::atomic<std::uint64_t> publishedEpoch_{0};
    std::atomic<int> callbackInputCount_{-1};
    std::atomic<int> callbackOutputCount_{-1};
    std::atomic<bool> enabled_{false};
    std::atomic<bool> userPatched_{false}; // RT authorization, not graph readiness
    std::atomic<float> peakL_{0.f},peakR_{0.f};
    std::atomic<int> sendCount_{0};
    std::atomic<int> fxCount_{0};
    std::atomic<int> fxReused_{0};
    std::array<std::atomic<float>,count> channelPeaks_{};
    std::array<std::atomic<float>,count> channelPeaksL_{},channelPeaksR_{};
    // Exclusive render-producer state. Bounded fade starts from the exact
    // previously output sample, preventing a topology switch discontinuity.
    static constexpr int rampFrames=256;
    int fadeRemaining_=0;
    std::uint64_t audioRevision_=0,audioEpoch_=0;
    float fadeFromL_=0.f,fadeFromR_=0.f;
    float lastOutputL_=0.f,lastOutputR_=0.f;
};
