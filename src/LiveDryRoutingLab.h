// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Vst3DescriptionCache.h"
#include "RuntimeVendorCrashJournal.h"
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/MasterOutputMonoPolicy.h"
#include "../engine/core/LiveChannelFxGraph.h"
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/core/NativeMusicTempoMonitor.h"
#include "../engine/device/DeviceSessionTransitionPolicy.h"
#include "../engine/core/NativeFxRackTransaction.h"
#include "../engine/builtin_fx/VocalReverbProcessor.h"
#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include "../engine/builtin_fx/GraphicEq31Processor.h"
#include "../engine/builtin_fx/ChannelCompressorProcessor.h"
#include "../engine/builtin_fx/AutoDuckerProcessor.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#include "../engine/core/UnifiedLegacyProductionAdmission.h"
#include "VstBusLayout.h"
#include "../engine/core/NativeDryRouteCommand.h"
#include "NativeVerifiedSourcePatch.h"
#include "NativeOutputBusPatch.h"
#include "../engine/core/MonitorTimingEdit.h"
#include "NativeTopologyEditSafety.h"
#include "NativeMixerSessionDocument.h"
#include "NativeFxRackArchive.h"
#include "NativeIoDefaults.h"
#include "../engine/device/JuceActivePortSnapshot.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../engine/core/RealtimeRenderPermit.h"
#include "../engine/core/ProductionRenderAudit.h"
#include "../engine/core/LiveOutputRouteGate.h"
#include "../engine/device/LiveInputPortRegistry.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <set>
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
    // CONTROL/UI-thread snapshot of the three independent audio bypass gates.
    // Do not overwrite saved slot/rack flags when global A/B changes.
    struct RetainedFxPowerState final {
        bool valid=false,slotBypassed=false,rackBypassed=false,globalBypassed=false;
        bool effectiveBypassed() const noexcept {
            return valid && (slotBypassed || rackBypassed || globalBypassed);
        }
    };
    struct ChannelEdit {
        float linear=1.f, pan=0.f;
        bool mute=false, solo=false, mono=false;
    };

    // Device callbacks only invalidate admission. CONTROL can later rebind
    // the previously active graph after independent hardware verification.
    void invalidateDeviceAdmission() noexcept {
        channelIdPcmEpoch_.store(0,std::memory_order_release);
    }

    // JUCE calls aboutToStart after configuring the input/output device.
    // One device session gets a new epoch; stored route cannot survive it.
    void prepareDevice (juce::AudioIODevice* device) noexcept {
        invalidateDeviceAdmission();
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
        if(inputs<0 || outputs<2) return; // output-only is a real SILENT native session
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
        invalidateDeviceAdmission();
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
        // Keep M4's historical unpatched/fail-silent behavior. Startup
        // explicitly promotes this same genuine SOURCE NONE graph to PCM.
        return apply(juce::var(request.release()),directPermitted,
                     independentClock,loadedFx,none,formats,
                     sampleRate,deviceFrames,false);
    }

    juce::String apply (const juce::var& command, bool directPermitted,
                        bool independentClock, int loadedFx,
                        const FxSelections& selections,
                        juce::AudioPluginFormatManager& formats,
                        double sampleRate,int deviceFrames,bool explicitSelection=true,
                        const FxRestores* restores=nullptr,
                        const std::function<void()>& beforePublish={}) {
        // E5: the M4 compiler cannot encode an admitted native GROUP/FX RETURN.
        // An M4 APPLY, Show recall or structural SOURCE/AUX edit must not
        // erase native BUS PCM while its ChannelId graph owns the callback.
        // After real device reopen a new epoch can start with a clean roster.
        if(nativeStructureLive_ && nativeAccepted_.deviceEpoch==epoch())
            return "REMOVE native GROUP/FX RETURN buses before replacing the M4 route";
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
        if(countNow<0 || countNow>armix::device::LiveInputPortRegistry::limit)
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
        if(expectedRevision!=revision())
            return "Routing revision changed before preparation; refresh accepted state";
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
        // Immutable pointer to one global RT atomic; prepared before publish.
        next->prepared->bindGlobalRackOverride(&globalFxBypassed_);
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
                armix::startup::RuntimeVendorCrashJournal::Operation vendorGuard(
                    armix::startup::RuntimeVendorCrashJournal::process(),req.path,
                    restores?"SHOW_RESTORE":"ROUTING_ADD");
                if(!vendorGuard)return label+": VST3 recovery journal unavailable or vendor operation busy";
                const auto chosen=armix::vst::DescriptionCache::resolve(*fmt,req.path,req.uid);
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
        // The old accepted graph continues to render while a new vendor
        // instance is scanned, constructed and prepared on CONTROL. Only
        // after ALL fallible preparation and the last device-epoch check
        // may the caller briefly close the physical output at commit.
        // Never call the gate on a failed plugin admission.
        if(beforePublish)beforePublish();
        // New M4 routing must not render against an older ChannelId graph.
        // The output must be disarmed by the caller at the publication edge.
        const auto previousPcmEpoch=channelIdPcmEpoch_.exchange(
            0,std::memory_order_acq_rel);
        const auto result=publisher_.publish(std::move(next),expectedRevision,epochAtPublish);
        // A rejected publication did NOT replace the callback graph. Keep
        // the previous admitted native PCM authority rather than silently
        // falling back to potentially stale M4 controls on this device.
        if(result!=Publisher::Result::applied &&
           previousPcmEpoch==epochAtPublish &&
           deviceEpoch_.load(std::memory_order_acquire)==epochAtPublish)
            channelIdPcmEpoch_.store(previousPcmEpoch,std::memory_order_release);
        switch(result) {
            case Publisher::Result::applied:
                reboundQuantum_.store(Block::hardFrameLimit,std::memory_order_release);
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
                // The freshly accepted M4 graph now owns these exact controls.
                // Dirty native-only gestures have been captured into the new
                // graph or superseded by a deliberate explicit APPLY.
                // Otherwise re-admission would reject forever after one fader
                // gesture and disable all fallback live controls.
                nativeControlDirty_=false;
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


    // Realtime audio must not briefly MUTE for an FX command guaranteed
    // to reject (stale epoch, invalid lane/slot, unsupported native BUS,
    // non-contiguous inserts). Validate BEFORE the guarded graph edit.
    // CONTROL-only, bounded and read-only: no vendor scan/heap/RT effects.
    juce::String fxInsertPreflight(int lane,int slot,
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            const FxSelection& chosen) const {
        if(lane<0||lane>=fxSlots||slot<0||slot>=maxFxPerLane||
           chosen.id.isEmpty()||chosen.path.isEmpty()||chosen.name.isEmpty()||
           !ready()||!hasExplicitRoute()||nativeStructureLive_||
           topologyFault_.load(std::memory_order_acquire)||
           !registryValid_.load(std::memory_order_acquire)||
           expectedEpoch!=epoch()||expectedRevision!=revision()||
           !sourceTemplate_.isObject())
            return "FX INSERT: stale routing/ChannelId authority or invalid catalog slot";
        if(acceptedFxSelections_[lane][slot].id.isNotEmpty())
            return "FX INSERT: this slot already has a native VST3";
        if(slot>0&&acceptedFxSelections_[lane][slot-1].id.isEmpty())
            return "FX INSERT: choose the first empty insert position";
        return {};
    }

    // SINGLE CONTROL-thread control-state inheritance used by BOTH SOURCE/
    // OUT edits and vendor VST3 insertion. An FX add must not recompile an
    // earlier APPLY's fader/PAN/MUTE/SOLO into the already-audible PCM.
    // This copies authoritative M4 controls, or the actual ChannelId roster
    // while ChannelId owns PCM; it never borrows a browser/demo state.
    juce::String inheritLiveControlsForRecompile(juce::var& staged,
                                                 std::uint64_t expectedEpoch) const {
        auto stagedRows=staged.getProperty("channels",juce::var());
        auto* editable=stagedRows.getArray();
        if(!editable||editable->size()!=count)
            return "malformed accepted native channel topology";
        const bool nativeControls=channelIdPhysicalPcmActive();
        if(nativeControls && (nativeStructureLive_ ||
           nativeAccepted_.deviceEpoch!=expectedEpoch))
            return "incompatible ChannelId roster or stale device epoch";
        for(int i=0;i<count;++i) {
            auto* row=editable->getReference(i).getDynamicObject();
            if(!row)return "missing accepted native channel";
            const auto& c=channelEdits_[i];
            const auto* accepted=nativeControls
                ?nativeAccepted_.graph.roster.find(nativeAccepted_.graph.ids.inputs[i])
                :nullptr;
            if(nativeControls && (!accepted ||
                accepted->role!=armix::mixer::ChannelRole::input))
                return "admitted input channel roster is stale";
            const armix::mixer::NativeTopologyEditSafety::MixSnapshot legacy{
                c.linear,c.pan,c.mute,c.solo};
            const armix::mixer::NativeTopologyEditSafety::MixSnapshot native{
                accepted?accepted->faderLinear:c.linear,
                accepted?accepted->pan:c.pan,
                accepted?accepted->mute:c.mute,
                accepted?accepted->solo:c.solo};
            const auto kept=armix::mixer::NativeTopologyEditSafety::preserveAcceptedMix(
                legacy,native,nativeControls);
            row->setProperty("gain",static_cast<double>(kept.linear));
            row->setProperty("pan",static_cast<double>(kept.pan));
            row->setProperty("mute",kept.mute);
            row->setProperty("solo",kept.solo);
        }
        return {};
    }

    // Native /preview/ FX picker: append one catalog-verified stereo VST3 to
    // the SAME accepted ten-channel/4AUX renderer. This is a prepared graph
    // transaction on JUCE CONTROL, never a JS plugin or RT instantiation.
    juce::String insertCatalogFx(int lane,int slot,
            std::uint64_t expectedEpoch,std::uint64_t expectedRevision,
            const FxSelection& chosen,bool directPermitted,
            bool independentClock,int occupiedCh1Fx,
            juce::AudioPluginFormatManager& formats,double sampleRate,
            int deviceFrames,
            const std::function<void()>& beforePublish={}) {
        const auto preflight=fxInsertPreflight(
            lane,slot,expectedEpoch,expectedRevision,chosen);
        if(preflight.isNotEmpty())return preflight;
        auto command=juce::JSON::parse(
            juce::JSON::toString(sourceTemplate_,false));
        if(!command.isObject())
            return "FX INSERT: accepted source template cannot be cloned";
        command.getDynamicObject()->setProperty("deviceEpoch",static_cast<juce::int64>(expectedEpoch));
        command.getDynamicObject()->setProperty("revision",static_cast<juce::int64>(expectedRevision));
        // A vendor plugin prepare happens AFTER live native fader/PAN edits.
        // Recompile from the last accepted operator controls, not the stale
        // pre-fader sourceTemplate_ values. Rejection keeps the old PCM live.
        const auto inherited=inheritLiveControlsForRecompile(
            command,expectedEpoch);
        if(inherited.isNotEmpty())return "FX INSERT: "+inherited;
        auto next=acceptedFxSelections_;
        next[lane][slot]=chosen;
        return apply(command,directPermitted,independentClock,occupiedCh1Fx,
                     next,formats,sampleRate,deviceFrames,true,nullptr,
                     beforePublish);
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
        if(channelIdPhysicalPcmActive()) {
            const auto id=nativeAccepted_.graph.ids.inputs[strip];
            if(!nativePublisher_.setPhysicalSource(
                expectedEpoch,nativeAccepted_.pcmRevision,id,
                callbackLeft,mode=="STEREO"?callbackRight:-1,
                mode=="STEREO")) {
                rejectedNativeMirror();
                return "SOURCE SWITCH: native PCM rejected mirror; refresh/recover routing";
            } else {
                // Physical SOURCE changes are LIVE atomic controls. Persist
                // the actual callback ordinals for the NEXT Group/FX Return
                // structural compile rather than reusing stale APPLY pins.
                auto& patches=nativeAccepted_.graph.request.sources;
                const auto found=std::find_if(patches.begin(),patches.end(),
                    [id](const auto& patch){return patch.id==id;});
                if(found!=patches.end()) {
                    found->inputL=callbackLeft;
                    found->inputR=mode=="STEREO"?callbackRight:-1;
                    if(logicalInputs_.assignedPort(id.value)){
                        (void)logicalInputs_.assign(id.value,0);++logicalInputRevision_;
                    }
                }
            }
        }
        if(auto* object=row.getDynamicObject()){
            object->setProperty("left",physicalLeft);
            object->setProperty("right",mode=="STEREO"?physicalRight:-1);
        }
        return {};
    }

    // CONTROL-only classification for the operator's direct IN dropdown.
    // Fail conservatively to the guarded path for an absent/invalid template.
    bool sourceNeedsPreparedGraph(int strip,const juce::String& mode) const {
        if(!sourceTemplate_.isObject() || !hasExplicitRoute())return true;
        const auto channels=sourceTemplate_.getProperty("channels",juce::var());
        const auto* rows=channels.getArray();
        if(!rows||rows->size()!=count||strip<0||strip>=count)return true;
        return armix::mixer::NativeTopologyEditSafety::structuralSource(
            true,rows->getReference(strip).getProperty("mode",{}).toString()==mode);
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
            int deviceFrames,
            const std::function<void()>& beforePublish={})
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
             sampleRate,deviceFrames,beforePublish);
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
            int deviceFrames,
            const std::function<void()>& beforePublish={}) {
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
             sampleRate,deviceFrames,beforePublish);
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
            int deviceFrames,
            const std::function<void()>& beforePublish={}) {
        const auto inherited=inheritLiveControlsForRecompile(
            staged,expectedEpoch);
        if(inherited.isNotEmpty())return "CHANNEL PATCH: "+inherited;
        if(expectedEpoch!=epoch()||expectedRevision!=revision()||
           !registryValid_.load(std::memory_order_acquire))
            return "CHANNEL PATCH: device changed; previous mix preserved";
        return apply(staged,directPermitted,independentClock,occupiedCh1Fx,
                     acceptedFxSelections_,formats,sampleRate,deviceFrames,true,
                     nullptr,beforePublish);
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
        // The admitted native ChannelId graph is the ONLY active PCM control
        // authority. Reject before changing legacy M4 DSP or claiming ACK.
        if(channelIdPhysicalPcmActive()) {
            if(nativeStructureLive_)return false; // old ordinal is now stale
            const auto id=nativeAccepted_.graph.ids.inputs[strip];
            const bool accepted=(name=="mute"||name=="solo")
                ?setNativeInputMuteSolo(expectedEpoch,nativeAccepted_.pcmRevision,
                                        id,updated.mute,updated.solo)
                :setNativeInputMix(expectedEpoch,nativeAccepted_.pcmRevision,
                                   id,updated.linear,updated.pan);
            if(!accepted)return false;
            channelEdits_[strip]=updated; // CONTROL-thread Show/re-admission baseline
            return true;
        }
        if(nativeStructureLive_ || nativeControlDirty_)return false;
        // Legacy M4 is still the installed PCM owner before native cutover.
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
        if(channelIdPhysicalPcmActive()) {
            const auto id=nativeAccepted_.graph.ids.inputs[source];
            const auto target=nativeAccepted_.graph.ids.aux[bus];
            const bool pre=tap==armix::mixer::SendTap::preFader;
            if(!nativePublisher_.setParallelSendGain(
                  expectedEpoch,nativeAccepted_.pcmRevision,
                  id,target,pre,gain))rejectedNativeMirror();
            else nativeAccepted_.graph.roster.rememberLiveParallelSend(
                  id,target,pre,gain);
        }
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
        if(channelIdPhysicalPcmActive()) {
            const auto id=nativeAccepted_.graph.ids.aux[bus];
            if(!nativePublisher_.setChannelMix(
                  expectedEpoch,nativeAccepted_.pcmRevision,
                  id,gain,0.f))rejectedNativeMirror();
            else nativeAccepted_.graph.roster.rememberLiveMix(id,gain,0.f);
        }
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
        if(channelIdPhysicalPcmActive()) {
            const auto id=source<count?nativeAccepted_.graph.ids.inputs[source]:
                         nativeAccepted_.graph.ids.aux[source-count];
            const auto target=nativeAccepted_.graph.ids.monitor;
            if(!nativePublisher_.setParallelSendGain(
                  expectedEpoch,nativeAccepted_.pcmRevision,
                  id,target,false,gain))rejectedNativeMirror();
            else nativeAccepted_.graph.roster.rememberLiveParallelSend(
                  id,target,false,gain);
        }
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

    // Master rack gate is a single RT-visible atomic per lane; never overwrite
    // stored individual insert bypass flags. Safe under current epoch/revision.
    bool setFxRackBypass(int lane,std::uint64_t expectedEpoch,
                         std::uint64_t expectedRevision,bool bypass) noexcept {
        if(lane<0||lane>=fxSlots||!hasExplicitRoute()||
           expectedEpoch!=epoch()||expectedRevision!=revision())return false;
        return publisher_.updateActiveControl(expectedEpoch,expectedRevision,
            [&](FxGraph& fx) noexcept {return fx.setRackBypass(lane,bypass);});
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


    // E2: ACTUAL hardware callback cutover for the admitted fixed 10CH+4AUX
    // topology, retaining original per-lane JUCE VST3 leases and Show state.
    // This CONTROL-only transaction uses real sparse driver port masks and
    // the same physical destinations already validated by the M4 owner.
    // Occupied original CH1/Orbit is leased as its existing shared Graph,
    // including the exact atomic bypass, FX order, state, tail and editor.
    // The published ChannelId snapshot retains this owner until RT retires.
    bool admitChannelIdFromAcceptedM4(int occupiedExternalCh1Fx,
                                     int deviceFrames,bool outputsDisarmed,
             armix::mixer::DynamicChannelFxLeaseBank::ExternalLease externalCh1={},
             bool admitSilentUnpatched=false) {
        // E5: a native BUS uses topology that M4 cannot represent. Never
        // demote its renderer on an unsupported legacy re-admission.
        if((nativeStructureLive_ || nativeControlDirty_) &&
           nativeAccepted_.deviceEpoch==epoch())
            return false;
        // Otherwise the last ACCEPTED M4 graph is the safe physical fallback
        // if migration rejects. This also handles a successfully replaced M4
        // route whose new ChannelId candidate cannot be prepared.
        channelIdPcmEpoch_.store(0,std::memory_order_release);
        nativeStructureLive_=false;
        try {
        const bool silentStarter=admitSilentUnpatched && !hasExplicitRoute() &&
            sourceTemplate_.isObject() && !userPatched_.load(std::memory_order_acquire);
        if(!outputsDisarmed || occupiedExternalCh1Fx<0 ||
           (occupiedExternalCh1Fx>0)!=externalCh1.populated() ||
           (!hasExplicitRoute()&&!silentStarter)||!ready() ||
           topologyFault_.load(std::memory_order_acquire))
            return false;
        const auto generation=epoch(),m4Revision=revision();
        const auto* old=publisher_.controlPrepared(generation,m4Revision);
        if(!old || deviceFrames<=0) return false;
        const auto& route=silentStarter?sourceTemplate_:acceptedRoute_;
        // This admission never upgrades a draft with unverified capture.
        if(silentStarter) {
            const auto* rows=route.getProperty("channels",juce::var()).getArray();
            if(!rows || rows->size()!=count)return false;
            for(const auto& row:*rows)
                if(row.getProperty("mode",{}).toString()!="NONE")return false;
        }
        armix::mixer::NativeDryRouteCommand decoded;
        juce::String problem;
        if(!armix::mixer::NativeDryRouteCommand::decode(
              route,decoded,problem))return false;
        // A native IN is a callback ordinal, not the numerical physical
        // port label. Preserve original order including sparse ASIO pins.
        const int inputs=portCount_.load(std::memory_order_acquire);
        if(inputs<0||inputs>armix::device::LiveInputPortRegistry::limit)
            return false;
        std::array<int,armix::device::LiveInputPortRegistry::limit> ports{};
        for(int i=0;i<inputs;++i)
            ports[i]=enabledPorts_[i].load(std::memory_order_relaxed);
        // M4 live fader/mute/solo controls are authoritative; their original
        // APPLY values can be superseded by thousands of live GUI gestures.
        for(int i=0;i<count;++i) {
            auto& dst=decoded.channels.stripsAndMaster.strips[i];
            const auto& ctl=channelEdits_[i];
            dst.faderLinear=ctl.linear;dst.pan=ctl.pan;
            dst.mute=ctl.mute;dst.solo=ctl.solo;
        }
        armix::mixer::VerifiedNativeOutputs hw{};
        hw.activeOutputChannels=callbackOutputCount_.load(std::memory_order_acquire);
        hw.mainL=old->mainOutputLeft();hw.mainR=old->mainOutputRight();
        hw.monitorEnabled=old->monitorEnabled();
        if(hw.monitorEnabled) {
            hw.monitorL=old->monitorOutputLeft();
            hw.monitorR=old->monitorOutputRight();
        }
        std::array<float,fxSlots> monitor{};
        if(hw.monitorEnabled) {
            const auto gains=acceptedRoute_.getProperty("monitorSends",juce::var());
            const auto* list=gains.getArray();
            if(!list||list->size()!=fxSlots)return false;
            for(int i=0;i<fxSlots;++i)
                monitor[i]=static_cast<float>(
                    static_cast<double>(list->getReference(i)));
        }
        // The previous publisher belongs to another device generation.
        // Retire it only on CONTROL, never from JUCE's device callback.
        if(nativeAccepted_.deviceEpoch!=generation &&
           nativePublisher_.acceptedRevision()!=0)
            nativePublisher_.unpublish();
        const int frames=juce::jmax(32,juce::jmin(
            deviceFrames,old->maxFramesPerPlugin()));
        auto applied=armix::mixer::UnifiedLegacyProductionAdmission::compileAndCommit(
            decoded.channels,{ports.data(),static_cast<std::size_t>(inputs)},
            decoded.sends,decoded.aux,monitor,*old,
            occupiedExternalCh1Fx>0,hw,
            frames,32u*1024u*1024u,
            nativePublisher_.acceptedRevision(),generation,epoch(),
            true,nativePublisher_,nativeAccepted_,std::move(externalCh1),
            &globalFxBypassed_);
        if(!applied.ok() || generation!=epoch() || m4Revision!=revision())
            return false; // selected M4 PCM owns the safe fallback
        nativeControlDirty_=false; // fresh accepted legacy/native control baseline
        installNativeHardware(generation,nativeAccepted_.hardware,
                              nativeAccepted_.graph.request.maxFrames);
        return true;
        } catch(...) {
            // Failure is on CONTROL; the original M4/Orbit graph remains
            // valid and is the only callback PCM renderer selected.
            return false;
        }
    }
    // The accepted original four AUX/FX RETURNS are fixed M4-owned lanes.
    // Generic roster.removeBus() accepts any FX_RETURN; this physical host
    // must protect their actual stable IDs, not rely on role/display order.
    bool originalM4Aux(armix::mixer::ChannelId id) const noexcept {
        for(const auto aux:nativeAccepted_.graph.ids.aux)
            if(aux.value && aux==id)return true;
        return false;
    }
    // E3: structural native GROUP/FX RETURN transaction on the SAME
    // accepted physical ChannelId publisher. This is the CONTROL writer,
    // not a second graph/audio callback or a simulated Console mutation.
    // An error leaves the last accepted roster, plugin leases and PCM intact.
    struct NativeBusResult final {
        bool accepted=false;
        armix::mixer::ChannelId created{};
        std::uint64_t pcmRevision=0,rosterRevision=0;
        const char* error="NATIVE_GRAPH_NOT_READY";
    };
    // A real VST3 instance is prepared in LiveAudioEngine on CONTROL before
    // this call. ChannelId roster, actual processor lease and the one PCM
    // renderer commit as ONE accepted native revision. Previous sound/owner
    // survives plugin validation, routing or publication failure.
    NativeBusResult attachNativeInsert(
            std::uint64_t expectedEpoch,std::uint64_t expectedPcmRevision,
            std::uint64_t expectedRosterRevision,armix::mixer::ChannelId target,
            const juce::String& catalogId,
            armix::mixer::DynamicChannelFxLeaseBank::Lease processor,
            bool verifiedPreparedLiveInsert=false,bool explicitlyDisarmed=false) {
        using namespace armix::mixer;
        NativeBusResult out{};
        out.pcmRevision=nativeAccepted_.pcmRevision;
        out.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!channelIdPhysicalPcmActive()||!ready()||
           expectedEpoch!=epoch()||nativeAccepted_.deviceEpoch!=expectedEpoch||
           !nativeAccepted_.fxOwners||!processor||!processor->processor||
           expectedPcmRevision!=nativeAccepted_.pcmRevision||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision())
            return out;
        if(!verifiedPreparedLiveInsert) {
            out.error="NATIVE_INSERT_REQUIRES_PREPARED_INTERNAL_FX";return out;
        }
        const auto* current=nativeAccepted_.graph.roster.find(target);
        if(!current || current->role==ChannelRole::monitor) {
            out.error="NATIVE_INSERT_TARGET_UNAVAILABLE";return out;
        }
        if(processor->silenceBypass&&current->role!=ChannelRole::fxReturn){
            out.error="WET_RETURN_REQUIRES_FX_RETURN_CHANNEL";return out;
        }
        // PCM authority, not historical strip ordinal, owns every new FX.
        // An occupied external CH1 chain still needs a combined lease contract.
        if(nativeAccepted_.fxOwners->externalChannel()==target) {
            out.error="EXTERNAL_CH1_INSERT_CHAIN_ALREADY_OWNED";return out;
        }
        if(catalogId.isEmpty()||catalogId.getNumBytesAsUTF8()>256) {
            out.error="INVALID_VST3_CATALOG_ID";return out;
        }
        try {
            auto next=nativeAccepted_.graph.roster;
            // LiveDryRoutingLab also defines a legacy ChannelEdit; explicitly
            // select the ChannelId roster action, not the M4 routing type.
            armix::mixer::ChannelEdit add;
            add.kind=armix::mixer::ChannelEdit::Kind::addInsertReference;
            add.id=target;add.text=catalogId.toStdString();
            const std::array<armix::mixer::ChannelEdit,1> edits{add};
            if(!next.edit(expectedRosterRevision,edits).ok()) {
                out.error="NATIVE_INSERT_CAPACITY_REJECTED";return out;
            }
            // The native compressor is zero latency. Only prime a fresh,
            // unpublished lease; retain existing processors and their histories.
            if(catalogId=="armix.builtin.channel-compressor.v1" &&
               !nativeAccepted_.fxOwners->retainsEditor(processor))
                processor->primeZeroLatencyLiveInsertFade();
            DynamicChannelFxLeaseBank prepared;
            const auto fault=DynamicChannelFxLeaseBank::appendOwned(
                expectedEpoch,expectedPcmRevision+1,*nativeAccepted_.fxOwners,
                target,current->format,std::move(processor),prepared);
            if(fault!=DynamicChannelFxLeaseBank::Fault::none) {
                out.error="NATIVE_VST3_LEASE_PREPARE_REJECTED";return out;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(
                std::move(prepared));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;
            fx.graphRevision=expectedPcmRevision+1;
            const auto published=nativePublisher_.commit(
                next,nativeAccepted_.graph.request,expectedPcmRevision,
                expectedEpoch,epoch(),explicitlyDisarmed,std::move(fx),false,true,nullptr,nullptr,
                catalogId=="armix.builtin.channel-compressor.v1"&&!explicitlyDisarmed);
            if(!published.ok()) {
                out.error=published.fault==DynamicNativeSessionPublisher::Fault::continuityUnavailable?
                    "COMPRESSOR_INSERT_REQUIRES_STABLE_LIVE_GRAPH":"NATIVE_VST3_PCM_COMMIT_REJECTED";return out;
            }
            nativeAccepted_.graph.roster=std::move(next);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            nativeStructureLive_=true; // M4 Show cannot represent new inserts.
            nativeMeterEpoch_.store(0,std::memory_order_release);
            out.accepted=true;out.error="";
            out.pcmRevision=published.revision;
            out.rosterRevision=nativeAccepted_.graph.roster.revision();
            out.created=target;
            return out;
        } catch(...) {
            out.error="NATIVE_VST3_ADMISSION_FAILED";return out;
        }
    }
    // Explicit stopped conversion: capture accepted preset, prepare one
    // replacement lease and both WIREs, then publish once. No audio rebind.
    NativeBusResult convertNativeReverbReturn(std::uint64_t ep,std::uint64_t revision,
            std::uint64_t rosterRevision,armix::mixer::ChannelId source,std::size_t slot,
            const juce::String& token,double rate,int frames,
            std::unique_ptr<juce::AudioProcessor>(*factory)(double,int,const void*,int),
            bool outputsDisarmed) {
        using namespace armix::mixer;
        NativeBusResult out;out.error="DMX_CONVERSION_REQUIRES_DISARMED_OUTPUTS";
        if(!outputsDisarmed||!channelIdPhysicalPcmActive()||!ready()||
           ep!=epoch()||nativeAccepted_.deviceEpoch!=ep||
           nativeAccepted_.pcmRevision!=revision||!nativeAccepted_.fxOwners||
           nativeAccepted_.graph.roster.revision()!=rosterRevision||!factory)return out;
        out.error="DMX_CONVERSION_STALE_OR_UNSUPPORTED_INSERT";
        const auto* old=nativeAccepted_.fxOwners->ownerForControl(source,slot);
        if(!old||!*old||(*old)->instanceId!=token||
           (*old)->catalogId!=armix::builtin_fx::VocalReverbProcessor::catalogIdentity)return out;
        try {
            juce::MemoryBlock bytes;
            if(!(*old)->tryCaptureState(bytes)||bytes.isEmpty()){
                out.error="DMX_CONVERSION_PRESET_PENDING";return out;
            }
            armix::builtin_fx::VocalReverbSettings settings;
            if(!armix::builtin_fx::VocalReverbProcessor::decodeState(bytes.getData(),
                static_cast<int>(bytes.getSize()),settings))return out;
            auto processor=factory(rate,frames,bytes.getData(),static_cast<int>(bytes.getSize()));
            if(!processor){out.error="DMX_CONVERSION_PREPARE_FAILED";return out;}
            auto replacement=std::make_shared<FxGraph::FxInstance>(std::move(processor),
                armix::builtin_fx::VocalReverbProcessor::wetReturnIdentity,
                "Reverb-Sis Return",Block::hardFrameLimit);
            replacement->bypassRequested.store((*old)->bypassRequested.load(std::memory_order_acquire)||
                nativeAccepted_.fxOwners->rackBypassed(source),std::memory_order_release);
            // Match the original calibrated wet multiplier (not its serial
            // dry attenuation/pan). Conversion deliberately restores dry unity.
            const float mix=settings.values[0],x=std::clamp(2.f*mix-1.f,0.f,1.f);
            const float gain=std::clamp(std::sin(1.5707963267948966f*mix)*
                (1.f+.5f*x*x*(3.f-2.f*x))/1.5f,0.f,1.f);
            DynamicChannelRoster roster;DynamicChannelFxLeaseBank prepared;ChannelId created{};
            if(!prepareNativeReverbReturnConversion(ep,revision,rosterRevision,
                nativeAccepted_.graph.roster,*nativeAccepted_.fxOwners,source,slot,token,
                std::move(replacement),gain,roster,prepared,created)){
                out.error="DMX_CONVERSION_REQUIRES_LAST_REVERB_DIRECT_MAIN_INPUT";return out;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(prepared));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=revision+1;
            fx.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,l,r,n);
            };
            fx.latency=+[](void* p,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            const auto committed=nativePublisher_.commit(roster,nativeAccepted_.graph.request,
                revision,ep,epoch(),true,std::move(fx));
            if(!committed.ok()){out.error="DMX_CONVERSION_PCM_REJECTED";return out;}
            nativeAccepted_.graph.roster=std::move(roster);nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=committed.revision;reapplyExternalOutputsSameEpoch();
            nativeStructureLive_=true;nativeMeterEpoch_.store(0,std::memory_order_release);
            out.accepted=true;out.error="";out.created=created;out.pcmRevision=committed.revision;
            out.rosterRevision=nativeAccepted_.graph.roster.revision();return out;
        }catch(...){out.error="DMX_CONVERSION_EXCEPTION";return out;}
    }
    NativeBusResult editNativeFxRack(std::uint64_t ep,std::uint64_t revision,
            std::uint64_t rosterRevision,armix::mixer::ChannelId source,std::size_t slot,
            const juce::String& token,armix::mixer::ChannelId target,std::size_t position) {
        using namespace armix::mixer;
        NativeBusResult reply;reply.error="STALE_NATIVE_FX_RACK";
        if(!channelIdPhysicalPcmActive()||!ready()||ep!=epoch()||
           nativeAccepted_.deviceEpoch!=ep||nativeAccepted_.pcmRevision!=revision||
           !nativeAccepted_.fxOwners)return reply;
        try {
            DynamicChannelRoster roster;DynamicChannelFxLeaseBank prepared;
            if(!prepareNativeFxRackEdit(ep,revision,rosterRevision,
                   nativeAccepted_.graph.roster,*nativeAccepted_.fxOwners,source,
                   slot,token,target,position,roster,prepared)) {
                reply.error="NATIVE_FX_RACK_IDENTITY_OR_POSITION_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(prepared));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=revision+1;
            fx.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,l,r,n);
            };
            fx.latency=+[](void* p,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            const auto committed=nativePublisher_.commit(roster,nativeAccepted_.graph.request,
                revision,ep,epoch(),false,std::move(fx),false,true);
            if(!committed.ok()){reply.error="NATIVE_FX_RACK_PCM_COMMIT_REJECTED";return reply;}
            nativeAccepted_.graph.roster=std::move(roster);nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=committed.revision;
            reapplyExternalOutputsSameEpoch();nativeStructureLive_=true;
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";reply.pcmRevision=committed.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();return reply;
        }catch(...){reply.error="NATIVE_FX_RACK_PREPARE_FAILED";return reply;}
    }
    // Exact current owner + revision; new source set and dependency DAG
    // become visible in ONE native publication. No change to physical pins.
    NativeBusResult setNativeDuckerSources(const FxGraph::InstanceLease& held,
            armix::mixer::ChannelId target,std::size_t slot,const juce::String& token,
            const armix::builtin_fx::AutoDuckerProcessor::Settings& selection) {
        using namespace armix::mixer;
        NativeBusResult reply;reply.error="STALE_AUTO_DUCKER";
        if(!retainedNativeFxPower(held,target,slot,token).valid)return reply;
        const auto location=nativeAccepted_.fxOwners->findEditorLocation(held,token);
        if(!location||location.channel!=target||location.slot!=slot)return reply;
        auto* d=dynamic_cast<armix::builtin_fx::AutoDuckerProcessor*>(held->processor.get());
        if(!d||!selection.validSources()||!d->stateSnapshotQuiescent()){
            reply.error="AUTO_DUCKER_PENDING_OR_INVALID_SOURCES";return reply;
        }
        auto settings=d->acceptedSettings();
        const auto previous=settings;
        settings.sources=selection.sources;settings.count=selection.count;
        for(std::size_t i=0;i<settings.count;++i)
            if(settings.sources[i]==target.value||
               (!nativeAccepted_.graph.roster.find(ChannelId{settings.sources[i]})&&
                !std::binary_search(previous.sources.begin(),previous.sources.begin()+previous.count,settings.sources[i]))){
                reply.error="AUTO_DUCKER_SOURCE_UNAVAILABLE";return reply;
            }
        try{
            const auto ep=epoch(),revision=nativeAccepted_.pcmRevision;
            DynamicChannelFxLeaseBank staged;
            if(!DynamicChannelFxLeaseBank::stageDuckerSources(*nativeAccepted_.fxOwners,
                revision+1,target,slot,held,settings,staged))return reply;
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(staged));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan plan;
            plan.owner=owner;plan.context=owner.get();plan.deviceEpoch=ep;plan.graphRevision=revision+1;
            plan.render=+[](void* p,ChannelId id,float* a,float* b,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,a,b,n);
            };
            plan.latency=+[](void* p,ChannelId id) noexcept{
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            plan.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            const auto result=nativePublisher_.commit(nativeAccepted_.graph.roster,
                nativeAccepted_.graph.request,revision,ep,epoch(),false,std::move(plan),false,true,
                nullptr,nullptr,true);
            if(!result.ok()){
                reply.error=result.graphError.fault==DynamicGraphFault::cycle?
                    "AUTO_DUCKER_ROUTING_CYCLE":
                    result.graphError.fault==DynamicGraphFault::sidechainLatency?
                    "AUTO_DUCKER_PDC_UNSUPPORTED":
                    result.fault==DynamicNativeSessionPublisher::Fault::continuityUnavailable?
                    "AUTO_DUCKER_REQUIRES_STABLE_LIVE_GRAPH":"AUTO_DUCKER_GRAPH_REJECTED";
                return reply;
            }
            d->acknowledgePublishedSources(settings);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=result.revision;
            reapplyExternalOutputsSameEpoch();nativeStructureLive_=true;
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";reply.pcmRevision=result.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        }catch(...){reply.error="AUTO_DUCKER_PREPARE_FAILED";}
        return reply;
    }
    bool rackFileTargetCurrent(std::uint64_t ep,std::uint64_t rev,std::uint64_t roster,
            armix::mixer::ChannelId target) const noexcept {
        if(!channelIdPhysicalPcmActive()||!ready()||ep!=epoch()||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||!nativeAccepted_.fxOwners||
           nativeAccepted_.graph.roster.revision()!=roster||
           nativeAccepted_.fxOwners->externalChannel()==target)return false;
        const auto* row=nativeAccepted_.graph.roster.find(target);
        return row&&row->role!=armix::mixer::ChannelRole::monitor&&
            row->insertCatalogIds.size()==nativeAccepted_.fxOwners->insertCount(target);
    }
    juce::String captureRackFile(std::uint64_t ep,std::uint64_t rev,std::uint64_t roster,
            armix::mixer::ChannelId target,int slot,const juce::String& token,
            armix::storage::FxRackFile& result) {
        if(!rackFileTargetCurrent(ep,rev,roster,target))return "FX rack changed or unsupported";
        try {
            auto owners=nativeAccepted_.fxOwners;
            const auto count=owners->insertCount(target);
            if(slot>=0&&(static_cast<std::size_t>(slot)>=count||token.isEmpty()))
                return "FX slot unavailable";
            std::optional<armix::rt::RealtimeRenderPermit> permit;
            for(int i=0;i<50;++i){
                permit.emplace(rendering_);if(*permit)break;juce::Thread::sleep(1);
            }
            if(!*permit)return "Audio callback busy; retry FX rack";
            armix::storage::FxRackFile staged;
            staged.single=slot>=0;staged.rackBypassed=owners->rackBypassed(target);
            for(std::size_t i=0;i<count;++i){
                if(slot>=0&&i!=static_cast<std::size_t>(slot))continue;
                const auto* owner=owners->ownerForControl(target,i);
                if(!owner||!*owner||(slot>=0&&(*owner)->instanceId!=token))
                    return "FX instance changed";
                armix::storage::FxState fx;
                fx.catalogId=(*owner)->catalogId;fx.name=(*owner)->name;
                fx.bypass=(*owner)->bypassRequested.load(std::memory_order_acquire);
                if(!(*owner)->tryCaptureState(fx.binary))return "FX state pending; retry after edits apply";
                staged.effects.push_back(std::move(fx));
            }
            if(!rackFileTargetCurrent(ep,rev,roster,target)||nativeAccepted_.fxOwners!=owners)
                return "FX rack changed during capture";
            result=std::move(staged);return {};
        }catch(...){return "FX rack capture failed";}
    }
    // Explicit disarmed topology edit only. Removed instances are retired by
    // the existing publisher; no releaseResources or state file IO in RT.
    NativeBusResult replaceRackFile(std::uint64_t ep,std::uint64_t rev,std::uint64_t rosterRev,
            armix::mixer::ChannelId target,std::vector<FxGraph::InstanceLease> fresh,
            bool append,bool rackBypassed,int removeSlot,const juce::String& token,
            bool outputsDisarmed) {
        using namespace armix::mixer;
        NativeBusResult reply;reply.error="FX_RACK_STALE_OR_OUTPUT_ARMED";
        if(!outputsDisarmed||!rackFileTargetCurrent(ep,rev,rosterRev,target))return reply;
        try{
            const auto& prior=*nativeAccepted_.fxOwners;
            if(append||removeSlot>=0){
                const auto count=prior.insertCount(target);
                if(removeSlot>=0&&(static_cast<std::size_t>(removeSlot)>=count||
                    !fresh.empty()||append))return reply;
                std::vector<FxGraph::InstanceLease> retained;
                for(std::size_t i=0;i<count;++i){
                    const auto* lease=prior.ownerForControl(target,i);
                    if(!lease||!*lease)return reply;
                    if(removeSlot>=0&&i==static_cast<std::size_t>(removeSlot)){
                        if((*lease)->instanceId!=token)return reply;
                        continue;
                    }
                    retained.push_back(*lease);
                }
                retained.insert(retained.end(),fresh.begin(),fresh.end());
                fresh=std::move(retained);rackBypassed=prior.rackBypassed(target);
            }
            DynamicChannelRoster roster;DynamicChannelFxLeaseBank prepared;
            if(!prepareNativeFxRackReplacement(ep,rev,rosterRev,nativeAccepted_.graph.roster,
                 prior,target,std::move(fresh),rackBypassed,roster,prepared)){
                reply.error="FX_RACK_PREPARE_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(prepared));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=rev+1;
            fx.render=+[](void* p,ChannelId id,float* left,float* right,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,left,right,n);
            };
            fx.latency=+[](void* p,ChannelId id) noexcept{
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            const auto committed=nativePublisher_.commit(roster,nativeAccepted_.graph.request,
                rev,ep,epoch(),outputsDisarmed,std::move(fx));
            if(!committed.ok()){reply.error="FX_RACK_PCM_COMMIT_REJECTED";return reply;}
            nativeAccepted_.graph.roster=std::move(roster);nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=committed.revision;
            reapplyExternalOutputsSameEpoch();nativeStructureLive_=true;
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";reply.pcmRevision=committed.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();return reply;
        }catch(...){reply.error="FX_RACK_PREPARE_FAILED";return reply;}
    }
    bool retainsFxEditor(std::uint64_t ep,const FxGraph::InstanceLease& owner) const noexcept {
        if(!owner||ep!=epoch()||!ready())return false;
        if(channelIdPhysicalPcmActive())return nativeAccepted_.fxOwners&&
            nativeAccepted_.fxOwners->retainsEditor(owner);
        const auto* prepared=publisher_.controlPrepared(ep,revision());
        if(prepared)for(int lane=0;lane<fxSlots;++lane)
            for(int slot=0;slot<prepared->insertsOn(lane);++slot)
                if(prepared->insertInstanceId(lane,slot)==owner->instanceId)return true;
        return false;
    }
    // The editor may outlive an FX rack MOVE/revision. Resolve the actual
    // CURRENT accepted owner, rather than using epoch/revision frozen when
    // its window opened. A replaced/retired processor is NEVER controlled.
    RetainedFxPowerState retainedNativeFxPower(
            const FxGraph::InstanceLease& held,armix::mixer::ChannelId id,
            std::size_t slot,const juce::String& token) const noexcept {
        RetainedFxPowerState out{};
        if(!held || !id.value || token.isEmpty() ||
           !channelIdPhysicalPcmActive() || !nativeAccepted_.fxOwners ||
           nativeAccepted_.deviceEpoch!=epoch() || !ready())return out;
        // MOVE may change slot or ChannelId while the same editor HWND and
        // DSP processor remain alive. An old ordinal is NOT a new identity.
        if(slot>=maxFxPerLane)return out;
        const auto current=nativeAccepted_.fxOwners->findEditorLocation(held,token);
        if(!current)return out;
        out.valid=true;
        out.slotBypassed=held->bypassRequested.load(std::memory_order_acquire);
        out.rackBypassed=nativeAccepted_.fxOwners->rackBypassed(current.channel);
        out.globalBypassed=globalFxBypassed();
        return out;
    }
    bool bypassRetainedNativeFx(
            const FxGraph::InstanceLease& held,armix::mixer::ChannelId id,
            std::size_t slot,const juce::String& token,bool bypass) noexcept {
        if(!retainedNativeFxPower(held,id,slot,token).valid)return false;
        const auto current=nativeAccepted_.fxOwners->findEditorLocation(held,token);
        if(!current)return false; // deleted/replaced HWND must never control a new FX
        return nativeAccepted_.setFxBypass(nativePublisher_,epoch(),
                 nativeAccepted_.pcmRevision,current.channel,current.slot,token,bypass);
    }
    RetainedFxPowerState retainedRoutedFxPower(
            const FxGraph::InstanceLease& held,int lane,int slot,
            const juce::String& token) const noexcept {
        RetainedFxPowerState out{};
        if(!held || token.isEmpty() || !ready() ||
           channelIdPhysicalPcmActive() || !hasExplicitRoute() ||
           lane<0 || lane>=fxSlots || slot<0 || slot>=maxFxPerLane)
            return out;
        const auto ep=epoch(),rev=revision();
        const auto* current=publisher_.controlPrepared(ep,rev);
        if(!current || held->instanceId!=token)return out;
        // The accepted M4 graph may also move an FX to another lane/slot.
        // Only the SAME retained processor can remain a valid editor owner.
        for(int liveLane=0;liveLane<fxSlots;++liveLane)
            for(int liveSlot=0;liveSlot<current->insertsOn(liveLane);++liveSlot)
                if(current->leaseInsert(liveLane,liveSlot,token)==held) {
                    out.valid=true;
                    out.slotBypassed=held->bypassRequested.load(std::memory_order_acquire);
                    out.rackBypassed=current->rackBypassed(liveLane);
                    out.globalBypassed=globalFxBypassed();
                    return out;
                }
        return out;
    }
    bool bypassRetainedRoutedFx(
            const FxGraph::InstanceLease& held,int lane,int slot,
            const juce::String& token,bool bypass) noexcept {
        if(!retainedRoutedFxPower(held,lane,slot,token).valid)return false;
        const auto ep=epoch(),rev=revision();
        const auto* current=publisher_.controlPrepared(ep,rev);
        if(!current)return false;
        for(int liveLane=0;liveLane<fxSlots;++liveLane)
            for(int liveSlot=0;liveSlot<current->insertsOn(liveLane);++liveSlot)
                if(current->leaseInsert(liveLane,liveSlot,token)==held)
                    return setFxBypass(liveLane,liveSlot,ep,rev,token,bypass);
        return false;
    }
    bool bypassNativeInsert(std::uint64_t epochAtEdit,std::uint64_t pcmRevision,
            armix::mixer::ChannelId target,std::size_t slot,
            const juce::String& token,bool bypass) noexcept {
        return nativeAccepted_.setFxBypass(nativePublisher_,epochAtEdit,
            pcmRevision,target,slot,token,bypass);
    }
    // Native music onset analysis has no secondary audio callback. A source
    // selector is admitted only against the ACTIVE epoch/revision graph.
    bool selectMusicTempoSource(std::uint64_t ep,std::uint64_t rev,
             armix::mixer::ChannelId source,double sampleRate) noexcept {
        if(!channelIdPhysicalPcmActive()||ep!=epoch()||
           rev!=nativeAccepted_.pcmRevision)return false;
        return nativePublisher_.setMusicTempoSource(ep,rev,source,sampleRate);
    }
    armix::mixer::NativeMusicTempoMonitor::State pollMusicTempo() noexcept {
        const auto ep=channelIdPhysicalPcmActive()?epoch():0;
        return nativePublisher_.pollMusicTempo(ep,nativeAccepted_.pcmRevision);
    }
    // CONTROL traversal of the *published* insert owners. No audio-thread
    // graph search or raw unmanaged pointers survive this synchronous call.
    bool hasRuntimeFxFault() const noexcept {
        if(nativeAccepted_.fxOwners)
            for(const auto& channel:nativeAccepted_.graph.roster.channels()) {
                const auto count=nativeAccepted_.fxOwners->insertCount(channel.id);
                for(std::size_t slot=0;slot<count;++slot) {
                    const auto* owner=nativeAccepted_.fxOwners->ownerForControl(channel.id,slot);
                    if(owner&&*owner&&(*owner)->processFaulted())return true;
                }
            }
        const auto* graph=publisher_.controlPrepared(epoch(),revision());
        return graph&&graph->hasRuntimeFxFault();
    }
    template<class Callback>
    void forEachNativeProcessor(Callback&& visit) const {
        if(!channelIdPhysicalPcmActive()||!nativeAccepted_.fxOwners)return;
        for(const auto& ch:nativeAccepted_.graph.roster.channels()) {
            const auto count=nativeAccepted_.fxOwners->insertCount(ch.id);
            for(std::size_t slot=0;slot<count;++slot) {
                const auto* owner=nativeAccepted_.fxOwners->ownerForControl(ch.id,slot);
                if(owner && *owner && (*owner)->processor)
                    visit(ch.id,(*owner)->processor.get());
            }
        }
    }
    bool nativeChannelIsOriginalCh1(armix::mixer::ChannelId target) const noexcept {
        return nativeAccepted_.fxOwners &&
               nativeAccepted_.fxOwners->externalChannel()==target &&
               target==nativeAccepted_.graph.ids.inputs[0];
    }
    // Off-RT diagnostic only: do not infer the active external vendor Graph
    // from the latest graphOwned_ pointer, which can differ during a staged
    // FX handoff. Read the strong, accepted PCM lease under CONTROL instead.
    const armix::mixer::DynamicChannelFxLeaseBank::ExternalLease*
        acceptedOriginalCh1ExternalLeaseForControl() const noexcept
    {
        if (!channelIdPhysicalPcmActive() || !nativeAccepted_.fxOwners)
            return nullptr;
        const auto* lease =
            nativeAccepted_.fxOwners->externalLeaseForControl();
        return lease != nullptr &&
            lease->id == nativeAccepted_.graph.ids.inputs[0]
                ? lease : nullptr;
    }
    // Single global A/B override for M4 and the authoritative ChannelId PCM
    // graph. Never rewrite saved rack/slot bypass flags or rebuild processors.
    // A physical ACK requires an accepted CURRENT epoch/revision.
    bool setGlobalFxBypassed(std::uint64_t expectedEpoch,
                             std::uint64_t expectedRevision,bool bypass) noexcept {
        if(!ready() || expectedEpoch!=epoch() || !expectedRevision)return false;
        if(channelIdPhysicalPcmActive()) {
            if(!nativeAccepted_.fxOwners ||
               nativeAccepted_.deviceEpoch!=expectedEpoch ||
               nativeAccepted_.pcmRevision!=expectedRevision)return false;
        } else {
            if(!hasExplicitRoute() || expectedRevision!=revision() ||
               !publisher_.controlPrepared(expectedEpoch,expectedRevision))return false;
        }
        globalFxBypassed_.store(bypass,std::memory_order_release);
        return true;
    }
    // Pointer is assigned to the original CH1 Graph on CONTROL before
    // publication. Lab lifetime exceeds every accepted audio callback lease.
    const std::atomic<bool>* globalFxBypassFlag() const noexcept {
        return &globalFxBypassed_;
    }
    bool globalFxBypassed() const noexcept {
        return globalFxBypassed_.load(std::memory_order_acquire);
    }
    bool bypassNativeRack(std::uint64_t epochAtEdit,std::uint64_t pcmRevision,
                          armix::mixer::ChannelId target,bool bypass) noexcept {
        return nativeAccepted_.setFxRackBypass(nativePublisher_,epochAtEdit,
                                                pcmRevision,target,bypass);
    }
    armix::mixer::DynamicChannelFxLeaseBank::Lease leaseNativeInsertEditor(
            std::uint64_t epochAtEdit,std::uint64_t pcmRevision,
            armix::mixer::ChannelId target,std::size_t slot,
            const juce::String& token) const noexcept {
        return nativeAccepted_.leaseFxEditor(nativePublisher_,epochAtEdit,
            pcmRevision,target,slot,token);
    }

    // CONTROL: same accepted device/graph and callback-observed exclusive
    // MONITOR pair. The native publisher validates them again on AUDIO.
    std::uint64_t deEzzerCueRoute(std::uint64_t ep,std::uint64_t rev,armix::mixer::ChannelId channel) const noexcept {
        if(!ready()||!channelIdPhysicalPcmActive()||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||!nativeBusOutputObserved()||
           !nativeAccepted_.fxOwners||globalFxBypassed()||nativeAccepted_.fxOwners->rackBypassed(channel))return 0;
        const auto p=nativeHardware_.load(std::memory_order_acquire);
        const int ml=nativePin(p,2),mr=nativePin(p,3),l=nativePin(p,0),r=nativePin(p,1);
        return ml>=0&&mr>=0&&l>=0&&r>=0&&ml!=mr&&ml!=l&&ml!=r&&mr!=l&&mr!=r&&
            nativeFrames(p)<=armix::mixer::NativeFxCueBus::capacity?p:0;
    }
    armix::mixer::NativeFxCueBus& deEzzerCueControl() noexcept {return nativePublisher_.cueControl();}
    NativeBusResult editNativeBus(std::uint64_t expectedEpoch,
                  std::uint64_t expectedPcmRevision,
                  std::uint64_t expectedRosterRevision,
                  armix::mixer::ChannelRole role,
                  std::span<const armix::mixer::ChannelId> selection,
                  const std::string& name,float sendGain,
                  armix::mixer::ChannelId remove={},
                  bool outputsDisarmed=false,
                  const std::function<void()>& beforePublish={},bool connectMain=true,
                  armix::mixer::DynamicChannelFxLeaseBank::Lease wetReturn={}) {
        using namespace armix::mixer;
        NativeBusResult reply{};
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!channelIdPhysicalPcmActive() || !ready() ||
           expectedEpoch!=epoch() || nativeAccepted_.deviceEpoch!=expectedEpoch ||
           !nativeAccepted_.fxOwners ||
           expectedPcmRevision!=nativeAccepted_.pcmRevision ||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision())
            return reply;
        if(!outputsDisarmed) {
            reply.error="OUTPUT_MUST_BE_DISARMED";return reply;
        }
        // Require a prepared, independently owned wet-only processor.
        // Roster, insert reference and processor lease publish together.
        // An empty return must never route a second copy of dry PCM.
        if(!remove.value && role==ChannelRole::fxReturn &&
           (!wetReturn||!wetReturn->processor||!wetReturn->silenceBypass||
            wetReturn->catalogId!=armix::builtin_fx::VocalReverbProcessor::wetReturnIdentity||
            !selection.empty()||!connectMain)) {
            reply.error="FX_RETURN_REQUIRES_NATIVE_VST3_OWNER";return reply;
        }
        // Original AUX lanes become ordinary native ChannelId FX returns
        // after accepted graph admission. Their immutable processor owner
        // must be released with the channel, never left orphaned in the bank.
        try {
            auto next=nativeAccepted_.graph.roster; // CONTROL only
            ChannelEditResult changed;
            if(remove.value) {
                changed=next.removeBusAndRestoreSources(
                    expectedRosterRevision,remove);
            } else if(role==ChannelRole::fxReturn) {
                changed=next.createReverbReturn(expectedRosterRevision,name);
            } else if(selection.empty()) {
                changed=next.createStandaloneBus(
                    expectedRosterRevision,name,role,connectMain);
            } else {
                changed=next.createBusForSelected(
                    expectedRosterRevision,selection,name,role,sendGain);
            }
            if(!changed.ok()) {
                reply.error="INVALID_NATIVE_BUS_EDIT";return reply;
            }
            // The bank is a revision-bound immutable map of actual VST3
            // owners. Reuse every current instance incl. original CH1
            // without duplicating, releasing or instantiating a processor.
            DynamicChannelFxLeaseBank bank;
            const auto leaseFault=remove.value?
                DynamicChannelFxLeaseBank::reuseWithoutChannel(
                    expectedEpoch,expectedPcmRevision+1,
                    *nativeAccepted_.fxOwners,remove,bank):
                role==ChannelRole::fxReturn?
                DynamicChannelFxLeaseBank::appendOwned(
                    expectedEpoch,expectedPcmRevision+1,*nativeAccepted_.fxOwners,
                    changed.createdIds.front(),ProcessingFormat::stereo,std::move(wetReturn),bank):
                DynamicChannelFxLeaseBank::reuseRevision(
                    expectedEpoch,expectedPcmRevision+1,
                    *nativeAccepted_.fxOwners,bank);
            if(leaseFault!=DynamicChannelFxLeaseBank::Fault::none) {
                reply.error="NATIVE_FX_LEASE_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(
                std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;
            fx.graphRevision=expectedPcmRevision+1;
            // Keep old PCM running throughout candidate validation/FX lease
            // reuse, and briefly gate hardware only at native publication.
            if(beforePublish)beforePublish();
            const auto published=nativePublisher_.commit(
                next,nativeAccepted_.graph.request,expectedPcmRevision,
                expectedEpoch,epoch(),true,std::move(fx));
            if(!published.ok()) {
                reply.error="NATIVE_PCM_COMMIT_REJECTED";return reply;
            }
            nativeAccepted_.graph.roster=std::move(next);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            // From now on original fixed M4 cannot express the extra BUS.
            // The native publisher remains the authoritative PCM, and a
            // failed scalar mirror MUST fault silent rather than reselect
            // an incompatible legacy route.
            // Deletion of an original AUX changes the fixed M4 roster even
            // when the resulting graph is smaller. Never demote the sole
            // ChannelId PCM renderer back to stale ordinal/M4 routing.
            nativeStructureLive_=true;
            reply.accepted=true;reply.error="";
            reply.pcmRevision=published.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
            if(!changed.createdIds.empty())reply.created=changed.createdIds[0];
            return reply;
        } catch(...) {
            reply.error="NATIVE_BUS_PREPARE_FAILED";return reply;
        }
    }

    // User-facing M/ST toggle after ChannelId takes over. This changes
    // processing width and pan semantics, NOT physical source pins, OUT
    // routing or the operator's MONITOR state. Reuse vendor leases and
    // publish the complete accepted PCM DAG atomically from CONTROL.
    NativeBusResult editNativeProcessingFormat(
            std::uint64_t expectedEpoch,std::uint64_t expectedPcmRevision,
            std::uint64_t expectedRosterRevision,armix::mixer::ChannelId channel,
            armix::mixer::ProcessingFormat format,bool outputsDisarmed=false,
            const std::function<void()>& beforePublish={}) {
        using namespace armix::mixer;
        NativeBusResult reply{};
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!outputsDisarmed){
            reply.error="PROCESSING_FORMAT_REQUIRES_PUBLICATION_GATE";return reply;
        }
        if(!channelIdPhysicalPcmActive()||!ready()||
           expectedEpoch!=epoch()||nativeAccepted_.deviceEpoch!=expectedEpoch||
           !nativeAccepted_.fxOwners||
           expectedPcmRevision!=nativeAccepted_.pcmRevision||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision()) {
            reply.error="STALE_NATIVE_PROCESSING_GRAPH";return reply;
        }
        const auto* current=nativeAccepted_.graph.roster.find(channel);
        if(!current || current->role!=ChannelRole::input ||
           (format!=ProcessingFormat::mono && format!=ProcessingFormat::stereo) ||
           current->format==format) {
            reply.error="INVALID_NATIVE_PROCESSING_FORMAT";return reply;
        }
        // CH1 may have an externally leased vendor graph. Its width needs
        // an explicit combined VST contract; do not reassign that owner here.
        if(nativeAccepted_.fxOwners->externalChannel()==channel) {
            reply.error="EXTERNAL_VST_LAYOUT_NOT_ADMITTED";return reply;
        }
        try {
            auto roster=nativeAccepted_.graph.roster;
            // LiveDryRoutingLab has an unrelated legacy M4 ChannelEdit.
            // Select the ChannelId roster's native edit ABI explicitly.
            armix::mixer::ChannelEdit edit;
            edit.kind=armix::mixer::ChannelEdit::Kind::setFormat;
            edit.id=channel;
            edit.format=format;
            const std::array<armix::mixer::ChannelEdit,1> edits{edit};
            if(!roster.edit(expectedRosterRevision,edits).ok()) {
                reply.error="NATIVE_PROCESSING_ROSTER_REJECTED";return reply;
            }
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseWithProcessingFormat(
                   expectedEpoch,expectedPcmRevision+1,
                   *nativeAccepted_.fxOwners,channel,format,bank)!=
               DynamicChannelFxLeaseBank::Fault::none) {
                reply.error="NATIVE_PROCESSING_FX_REBIND_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(
                std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;
            fx.graphRevision=expectedPcmRevision+1;
            if(beforePublish)beforePublish(); // no hardware mute during prepare
            const auto published=nativePublisher_.commit(
                roster,nativeAccepted_.graph.request,expectedPcmRevision,
                expectedEpoch,epoch(),true,std::move(fx));
            if(!published.ok()) {
                reply.error="NATIVE_PROCESSING_PCM_COMMIT_REJECTED";return reply;
            }
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            nativeStructureLive_=true; // never restore stale M4 width
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";
            reply.pcmRevision=published.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
            return reply;
        } catch(...) {
            reply.error="NATIVE_PROCESSING_PREPARE_FAILED";return reply;
        }
    }

    // CONTROL-only logical source identity, independent of ASIO pins.
    // Never infer that yesterday's ChannelId is still the attached deck
    // after OPEN mixer / graph reload in the SAME hardware epoch.
    armix::mixer::ChannelId nativePlayerSource(std::uint64_t deckId=1) const noexcept {
        if(!channelIdPhysicalPcmActive())return {};
        for(const auto& p:nativeAccepted_.graph.request.sources)
            if(p.kind==armix::mixer::DynamicSourceKind::playerDeck &&
               p.deckId==deckId &&
               nativeAccepted_.graph.roster.find(p.id))
                return p.id;
        return {};
    }

    // CONTROL-only truthful audio-readback. A loaded player can be PLAYING
    // with source meters active while SOURCE ONLY intentionally cuts MAIN.
    // Follow BUS/GROUP WIRE graph, never infer output from the source name.
    bool playerDeckHasMainPath(std::uint64_t deckId=1) const {
        if(!channelIdPhysicalPcmActive())return false;
        const auto& roster=nativeAccepted_.graph.roster;
        const auto& sources=nativeAccepted_.graph.request.sources;
        const auto patch=std::find_if(sources.begin(),sources.end(),
            [deckId](const armix::mixer::DynamicSourcePatch& p) {
                return p.kind==armix::mixer::DynamicSourceKind::playerDeck &&
                       p.deckId==deckId;
            });
        if(patch==sources.end())return false;
        std::vector<armix::mixer::ChannelId> stack{patch->id},visited;
        while(!stack.empty()) {
            const auto id=stack.back();stack.pop_back();
            if(std::find(visited.begin(),visited.end(),id)!=visited.end())continue;
            visited.push_back(id);
            const auto* row=roster.find(id);
            if(!row || row->mute || row->faderLinear<=0.f)continue;
            if(row->role==armix::mixer::ChannelRole::main)return true;
            for(const auto& send:row->sends)
                if(send.gain>0.f)stack.push_back(send.destination);
        }
        return false;
    }

    // CONTROL only: existing trusted MAIN + one owned PLAYER A channel.
    // Binds in one already-approved ChannelId graph revision, not a Lab,
    // additional audio device or external hardware input patch.
    NativeBusResult ensureNativePlayerA(bool outputsDisarmed,
            bool trustedExistingMain,
            const std::function<void()>& beforePublish={}) {
        return createNativeInputChannel(epoch(),nativeAccepted_.pcmRevision,
            nativeAccepted_.graph.roster.revision(),"MUSIC",
            armix::mixer::ProcessingFormat::stereo,
            outputsDisarmed,beforePublish,1,trustedExistingMain);
    }

    // Product Console: a NEW stable ChannelId input, not a fixed M4 ordinal
    // or an unowned JS strip. Starts unpatched and silent. Structural graph,
    // old vendor FX leases and MAIN routing publish as ONE control transaction.
    NativeBusResult createNativeInputChannel(std::uint64_t expectedEpoch,
            std::uint64_t expectedPcmRevision,std::uint64_t expectedRosterRevision,
            const std::string& name,armix::mixer::ProcessingFormat format,
            bool outputsDisarmed=false,
            const std::function<void()>& beforePublish={},
            std::uint64_t playerDeckId=0,bool trustedExistingMain=false) {
        using namespace armix::mixer;
        NativeBusResult reply{};
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        // Adding a purely internal music source and its owned MAIN wire
        // changes no physical device pins. The host MUST attest the same
        // already-authorized MAIN and matching epoch before taking this path.
        if(!outputsDisarmed && !(playerDeckId && trustedExistingMain)) {
            reply.error="OUTPUT_MUST_BE_DISARMED";return reply;
        }
        if(!channelIdPhysicalPcmActive()||!ready()||
           expectedEpoch!=epoch()||nativeAccepted_.deviceEpoch!=expectedEpoch||
           !nativeAccepted_.fxOwners||
           expectedPcmRevision!=nativeAccepted_.pcmRevision||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision())
            return reply;
        if(name.empty()||name.size()>64 ||
           (format!=ProcessingFormat::mono&&format!=ProcessingFormat::stereo)||
           (playerDeckId && format!=ProcessingFormat::stereo)){
            reply.error="INVALID_NATIVE_CHANNEL";return reply;
        }
        // Repeat PLAY never manufactures a second MUSIC channel or send.
        // An intentionally disconnected SOURCE ONLY remains disconnected.
        if(playerDeckId) {
            const auto& patches=nativeAccepted_.graph.request.sources;
            const auto existing=std::find_if(patches.begin(),patches.end(),
                [&](const DynamicSourcePatch& p) {
                    return p.kind==DynamicSourceKind::playerDeck &&
                           p.deckId==playerDeckId;
                });
            if(existing!=patches.end() &&
               nativeAccepted_.graph.roster.find(existing->id)) {
                reply.created=existing->id;reply.accepted=true;
                reply.error="";return reply;
            }
        }
        try {
            auto roster=nativeAccepted_.graph.roster;
            const auto& existing=roster.channels();
            const auto main=std::find_if(existing.begin(),existing.end(),
                [](const auto& r){return r.role==ChannelRole::main;});
            if(main==existing.end()) {
                reply.error="NATIVE_MAIN_NOT_FOUND";return reply;
            }
            armix::mixer::ChannelEdit add;
            add.kind=armix::mixer::ChannelEdit::Kind::add;add.role=ChannelRole::input;
            add.format=format;add.text=name;
            add.connectMainOnCreate=true; // exact one real POST_FADER MAIN edge
            add.position=static_cast<std::size_t>(main-existing.begin());
            // One accepted roster revision and ONE prepared PCM publication,
            // not create + second independent OUT routing mutation.
            auto created=roster.edit(expectedRosterRevision,
                                     std::span<const armix::mixer::ChannelEdit>(&add,1));
            if(!created.ok()||created.createdIds.size()!=1) {
                reply.error="CHANNEL_MAIN_ADMISSION_REJECTED";return reply;
            }
            auto request=nativeAccepted_.graph.request;
            if(playerDeckId)
                request.sources.push_back({created.createdIds.front(),
                    -1,-1,true,DynamicSourceKind::playerDeck,playerDeckId});
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseRevision(
                    expectedEpoch,expectedPcmRevision+1,
                    *nativeAccepted_.fxOwners,bank)!=
                    DynamicChannelFxLeaseBank::Fault::none) {
                reply.error="NATIVE_FX_LEASE_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;
            fx.graphRevision=expectedPcmRevision+1;
            if(beforePublish)beforePublish();
            const auto published=nativePublisher_.commit(
                roster,request,expectedPcmRevision,
                expectedEpoch,epoch(),outputsDisarmed,std::move(fx),
                playerDeckId!=0 && trustedExistingMain);
            if(!published.ok()) {
                reply.error="CHANNEL_PCM_COMMIT_REJECTED";return reply;
            }
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            nativeStructureLive_=true; // M4 can no longer represent CH11+
            reply.accepted=true;reply.error="";
            reply.created=created.createdIds.front();
            reply.pcmRevision=published.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
            return reply;
        }catch(...){
            reply.error="CHANNEL_NATIVE_PREPARE_FAILED";return reply;
        }
    }

    // One atomic prepared PCM publication for Delete Audio Track. The
    // accepted native source patch, inbound/outbound wires, and VST3 owner
    // references are removed together only after a successful graph commit.
    NativeBusResult removeNativeInputChannel(std::uint64_t expectedEpoch,
            std::uint64_t expectedPcmRevision,std::uint64_t expectedRosterRevision,
            armix::mixer::ChannelId id,bool outputsDisarmed=false,
            const std::function<void()>& beforePublish={},int liveFadeFrames=0,
            const std::function<bool()>& stillValid={}) {
        using namespace armix::mixer;
        NativeBusResult reply{};
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!outputsDisarmed&&liveFadeFrames<=0){reply.error="OUTPUT_MUST_BE_DISARMED";return reply;}
        if(!channelIdPhysicalPcmActive()||!ready()||
           expectedEpoch!=epoch()||nativeAccepted_.deviceEpoch!=expectedEpoch||
           !nativeAccepted_.fxOwners||
           expectedPcmRevision!=nativeAccepted_.pcmRevision||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision()){
            reply.error="STALE_NATIVE_TRACK_DELETE";return reply;
        }
        const auto* target=nativeAccepted_.graph.roster.find(id);
        if(!target||target->role!=ChannelRole::input){
            reply.error="NATIVE_INPUT_ID_UNAVAILABLE";return reply;
        }
        try {
            auto roster=nativeAccepted_.graph.roster;
            const auto edited=roster.removeInputAndDisconnect(expectedRosterRevision,id);
            if(!edited.ok()){
                reply.error="NATIVE_TRACK_DELETE_EDIT_REJECTED";return reply;
            }
            auto request=nativeAccepted_.graph.request;
            auto& inputs=request.sources;
            inputs.erase(std::remove_if(inputs.begin(),inputs.end(),
                [id](const auto& source){return source.id==id;}),
                inputs.end());
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseWithoutChannel(
                   expectedEpoch,expectedPcmRevision+1,
                   *nativeAccepted_.fxOwners,id,bank)!=
                   DynamicChannelFxLeaseBank::Fault::none){
                reply.error="NATIVE_TRACK_FX_RELEASE_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId channel,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(channel,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId channel) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(channel);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;
            fx.graphRevision=expectedPcmRevision+1;
            if(beforePublish)beforePublish();
            const auto published=nativePublisher_.commit(
                roster,request,expectedPcmRevision,
                expectedEpoch,epoch(),outputsDisarmed,std::move(fx),false,false,nullptr,nullptr,
                false,outputsDisarmed?ChannelId{}:id,liveFadeFrames,stillValid);
            if(!published.ok()){
                reply.error=published.fault==DynamicNativeSessionPublisher::Fault::continuityUnavailable?
                    "DELETE_REQUIRES_MUTED_OUTPUTS_PDC_OR_ROUTING":
                    published.fault==DynamicNativeSessionPublisher::Fault::retirementTimeout?
                    "DELETE_FADE_NOT_CONFIRMED_RETRY_MUTED":"NATIVE_TRACK_PCM_COMMIT_REJECTED";return reply;
            }
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            std::erase_if(waitingAsioInputs_,[id](const auto& pin){return pin.id==id;});
            if(logicalInputs_.assignedPort(id.value)){
                (void)logicalInputs_.assign(id.value,0);++logicalInputRevision_;
            }
            nativeStructureLive_=true;
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";
            reply.pcmRevision=published.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
            return reply;
        }catch(...){
            reply.error="NATIVE_TRACK_DELETE_PREPARE_FAILED";return reply;
        }
    }

    // Wire a verified physical capture pin to a dynamically CREATED input.
    // M4-owned CH1–10 keep the original device/VST continuity contract.
    // Prepared topology + lease revision is published once; never patch an
    // active callback's source vector or create an audio JS path.
    NativeBusResult patchNativeInputSource(std::uint64_t expectedEpoch,
            std::uint64_t expectedPcmRevision,std::uint64_t expectedRosterRevision,
            armix::mixer::ChannelId channel,int physicalLeft,
            int physicalRight,bool stereo,bool outputsDisarmed=false,
            const std::function<void()>& beforePublish={},
            bool musicPlayer=false) {
        using namespace armix::mixer;
        NativeBusResult reply{};
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!outputsDisarmed){reply.error="OUTPUT_MUST_BE_DISARMED";return reply;}
        if(!channelIdPhysicalPcmActive()||!ready()||
           expectedEpoch!=epoch()||nativeAccepted_.deviceEpoch!=expectedEpoch||
           !nativeAccepted_.fxOwners||
           expectedPcmRevision!=nativeAccepted_.pcmRevision||
           expectedRosterRevision!=nativeAccepted_.graph.roster.revision())
            return reply;
        const auto* target=nativeAccepted_.graph.roster.find(channel);
        if(!target||target->role!=ChannelRole::input){
            reply.error="NATIVE_INPUT_ID_UNAVAILABLE";return reply;
        }
        // PLAYER A is an internal stereo PCM decoder, NOT a device ordinal.
        // The one accepted ChannelId source transaction replaces (never
        // sums with) the physical source on the requested INPUT channel.
        if(musicPlayer && (physicalLeft!=-1 || physicalRight!=-1 || !stereo)) {
            reply.error="PLAYER_SOURCE_REQUIRES_INTERNAL_STEREO";return reply;
        }
        const bool disconnect=!musicPlayer&&!stereo&&
            physicalLeft==-1&&physicalRight==-1;
        int l=-1,r=-1;
        if(!disconnect && !musicPlayer){
            for(int i=0;i<portCount_.load(std::memory_order_acquire);++i){
                const int physical=enabledPorts_[i].load(std::memory_order_relaxed);
                if(physical==physicalLeft)l=i;
                if(physical==physicalRight)r=i;
            }
            if(l<0||(stereo&&(r<0||r==l))||
               (!stereo&&physicalRight!=-1)){
                reply.error="INPUT_NOT_IN_VERIFIED_HARDWARE_MASK";return reply;
            }
        }
        try {
            auto request=nativeAccepted_.graph.request;
            auto& inputs=request.sources;
            inputs.erase(std::remove_if(inputs.begin(),inputs.end(),
                [channel,musicPlayer](const auto& p) {
                    // The decoder is pulled once per audio callback. Never
                    // leave a second PLAYER A source attached to MUSIC/MAIN.
                    return p.id==channel ||
                        (musicPlayer && p.kind==DynamicSourceKind::playerDeck &&
                         p.deckId==1);
                }),inputs.end());
            if(musicPlayer) {
                inputs.push_back({channel,-1,-1,true,
                    DynamicSourceKind::playerDeck,1});
            } else if(!disconnect) {
                DynamicSourcePatch patch;
                patch.id=channel;patch.inputL=l;patch.inputR=stereo?r:-1;
                patch.stereo=stereo;inputs.push_back(patch);
            }
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseRevision(
                    expectedEpoch,expectedPcmRevision+1,
                    *nativeAccepted_.fxOwners,bank)!=
                    DynamicChannelFxLeaseBank::Fault::none) {
                reply.error="NATIVE_FX_LEASE_REJECTED";return reply;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* left,float* right,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,left,right,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=expectedEpoch;fx.graphRevision=expectedPcmRevision+1;
            if(beforePublish)beforePublish(); // late PCM publication gate
            const auto published=nativePublisher_.commit(
                nativeAccepted_.graph.roster,request,expectedPcmRevision,
                expectedEpoch,epoch(),true,std::move(fx));
            if(!published.ok()) {
                reply.error="NATIVE_INPUT_PCM_COMMIT_REJECTED";return reply;
            }
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            std::erase_if(waitingAsioInputs_,[channel](const auto& pin){return pin.id==channel;});
            // Explicit direct or PLAYER source replaces this channel's
            // logical reference only after the native publication succeeds.
            if(logicalInputs_.assignedPort(channel.value)){
                (void)logicalInputs_.assign(channel.value,0);++logicalInputRevision_;
            }
            nativeStructureLive_=true; // M4 source roster no longer authoritative
            nativeMeterEpoch_.store(0,std::memory_order_release);
            reply.accepted=true;reply.error="";
            reply.pcmRevision=published.revision;
            reply.rosterRevision=nativeAccepted_.graph.roster.revision();
            return reply;
        }catch(...){
            reply.error="NATIVE_SOURCE_PREPARE_FAILED";return reply;
        }
    }
    // Metadata-only edits do not rebuild PCM. A used port remap prepares
    // all ChannelId references and publishes through the existing owner once.
    NativeBusResult editNativeLogicalInput(std::uint64_t ep,std::uint64_t rev,
            std::uint64_t rosterRev,std::uint64_t logicalRev,
            armix::session::LogicalInputProjection::Edit edit,
            const armix::session::HardwareBinding& binding,
            const armix::device::JuceActivePortSnapshot& pins,
            const std::function<void()>& beforePublish={}) {
        using namespace armix::mixer;
        using P=armix::session::LogicalInputProjection;
        using R=armix::device::LogicalInputRegistry;
        NativeBusResult reply;
        reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        reply.error="LOGICAL_INPUT_STALE_OR_UNAVAILABLE";
        if(!channelIdPhysicalPcmActive()||!ready()||!nativeAccepted_.fxOwners||
           ep!=epoch()||pins.epoch!=ep||!matchesSessionDevice(pins)||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||
           rosterRev!=reply.rosterRevision||logicalRev!=logicalInputRevision_||
           logicalInputRevision_==std::numeric_limits<std::uint64_t>::max())return reply;
        try{
            if(edit.operation==P::Operation::add){
                if(edit.port)return reply;
                for(int i=0;i<8&&!edit.port;++i){
                    const auto id=static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
                    if(id&&!logicalInputs_.find(id))edit.port=id;
                }
                if(!edit.port)return reply;
            }
            std::vector<R::Pin> inputPins;
            inputPins.reserve(static_cast<std::size_t>(pins.inputs.enabledCount));
            for(int i=0;i<pins.inputs.enabledCount;++i){
                const auto physical=pins.inputs.physicalIndex(i);
                auto name=binding.inputNames[physical];
                if(armix::device::uniqueNamedPin(binding.inputNames,name)!=physical)name.clear();
                inputPins.push_back({physical,i,name.toStdString()});
            }
            const R::Inventory live{{binding.backend.toStdString(),binding.capture.toStdString(),
                binding.playback.toStdString()},ep,inputPins,true};
            edit.binding.device=live.device;
            armix::session::MixerSessionDocument candidate;
            candidate.roster=nativeAccepted_.graph.roster;
            candidate.request=nativeAccepted_.graph.request;
            candidate.logicalInputs=logicalInputs_;
            candidate.waitingAsioInputs=waitingAsioInputs_;
            bool graphChanged=false;
            if(!P::edit(candidate,edit,live,graphChanged)){
                reply.error="LOGICAL_INPUT_EDIT_REJECTED";return reply;
            }
            if(!matchesSessionDevice(pins))return reply;
            if(graphChanged){
                DynamicChannelFxLeaseBank bank;
                if(DynamicChannelFxLeaseBank::reuseRevision(ep,rev+1,*nativeAccepted_.fxOwners,bank)
                   !=DynamicChannelFxLeaseBank::Fault::none){
                    reply.error="NATIVE_FX_LEASE_REJECTED";return reply;
                }
                auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
                owner->bindGlobalRackOverride(&globalFxBypassed_);
                DynamicNativeSessionPublisher::FxPlan fx;
                fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=rev+1;
                fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept{
                    (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
                };
                fx.latency=+[](void* ctx,ChannelId id) noexcept{
                    return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
                };
                fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                    static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
                };
                if(beforePublish)beforePublish();
                const auto published=nativePublisher_.commit(nativeAccepted_.graph.roster,candidate.request,
                    rev,ep,epoch(),true,std::move(fx));
                if(!published.ok()){reply.error="LOGICAL_INPUT_PCM_COMMIT_REJECTED";return reply;}
                nativeAccepted_.graph.request=std::move(candidate.request);
                waitingAsioInputs_=std::move(candidate.waitingAsioInputs);
                nativeAccepted_.fxOwners=std::move(owner);
                nativeAccepted_.pcmRevision=published.revision;
                nativeMeterEpoch_.store(0,std::memory_order_release);
                reapplyExternalOutputsSameEpoch();
            }
            logicalInputs_=std::move(candidate.logicalInputs);
            ++logicalInputRevision_;
            nativeControlDirty_=true;nativeStructureLive_=true;
            reply.accepted=true;reply.error="";
            reply.pcmRevision=nativeAccepted_.pcmRevision;
            return reply;
        }catch(...){reply.error="LOGICAL_INPUT_PREPARE_FAILED";return reply;}
    }
    std::uint64_t logicalInputRevision() const noexcept {return logicalInputRevision_;}
    std::uint64_t logicalOutputRevision() const noexcept {return logicalOutputRevision_;}
    bool captureIoPortLayout(std::uint64_t ep,std::uint64_t rev,std::uint64_t roster,
            std::uint64_t inRev,std::uint64_t outRev,armix::session::IoPortLayout& out) const {
        if(!channelIdPhysicalPcmActive()||!nativeAccepted_.matches(nativePublisher_,ep,rev)||
           ep!=epoch()||roster!=nativeAccepted_.graph.roster.revision()||
           inRev!=logicalInputRevision_||outRev!=logicalOutputRevision_)return false;
        out=armix::session::IoPortLayout::capture(logicalInputs_,logicalOutputs_);
        return !out.empty();
    }
    bool installIoPortDefaults(const armix::session::IoPortLayout& layout,
            const armix::session::HardwareBinding& binding,
            const armix::device::JuceActivePortSnapshot& pins,bool disarmed){
        if(!disarmed||!canReplacePristineStartupMixer()||!matchesSessionDevice(pins)||
           !logicalInputs_.empty()||!logicalOutputs_.empty()||layout.empty()||!layout.definitionsOnly())return false;
        auto qualified=layout.qualify(binding,pins);
        if(qualified.inputs.ports().size()!=layout.inputs.ports().size()||
           qualified.outputs.ports().size()!=layout.outputs.ports().size()||
           !matchesSessionDevice(pins)||!canReplacePristineStartupMixer())return false;
        logicalInputs_=std::move(qualified.inputs);logicalOutputs_=std::move(qualified.outputs);
        ++logicalInputRevision_;++logicalOutputRevision_;
        // Authored metadata is durable, but no graph/FX/ARM state changes.
        nativeControlDirty_=true;nativeStructureLive_=true;
        return true;
    }
    // CONTROL only. The host proves all hardware output gates disarmed.
    NativeBusResult editNativeLogicalOutput(std::uint64_t ep,std::uint64_t rev,
            std::uint64_t rosterRev,std::uint64_t logicalRev,
            armix::session::LogicalOutputProjection::Edit edit,
            const armix::device::JuceActivePortSnapshot& pins,bool outputsDisarmed){
        using namespace armix::mixer;
        using P=armix::session::LogicalOutputProjection;
        using R=armix::device::LogicalOutputRegistry;
        NativeBusResult reply;reply.pcmRevision=nativeAccepted_.pcmRevision;
        reply.rosterRevision=nativeAccepted_.graph.roster.revision();
        reply.error="LOGICAL_OUTPUT_STALE_OR_UNAVAILABLE";
        if(!outputsDisarmed||!channelIdPhysicalPcmActive()||!ready()||!nativeAccepted_.fxOwners||
           ep!=epoch()||pins.epoch!=ep||pins.typeName!="ASIO"||!matchesSessionDevice(pins)||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||rosterRev!=reply.rosterRevision||
           logicalRev!=logicalOutputRevision_||logicalOutputRevision_==std::numeric_limits<std::uint64_t>::max())return reply;
        try{
            if(edit.operation==P::Operation::add){
                if(edit.port)return reply;
                for(int i=0;i<8&&!edit.port;++i){
                    const auto id=static_cast<std::uint64_t>(juce::Random::getSystemRandom().nextInt64());
                    if(id&&!logicalOutputs_.find(id))edit.port=id;
                }
                if(!edit.port)return reply;
            }
            std::vector<R::Pin> outputPins;
            outputPins.reserve(static_cast<std::size_t>(pins.outputCount));
            for(int i=0;i<pins.outputCount;++i){
                const int physical=pins.outputPhysicalPort(i);
                auto name=pins.outputNames[physical];
                if(armix::device::uniqueNamedPin(pins.outputNames,name)!=physical)name.clear();
                outputPins.push_back({physical,i,name.toStdString()});
            }
            const R::Inventory live{{pins.typeName.toStdString(),pins.deviceName.toStdString()},ep,outputPins,{},true};
            edit.binding.device=live.device;
            armix::session::MixerSessionDocument candidate;
            candidate.roster=nativeAccepted_.graph.roster;candidate.legacyIds=nativeAccepted_.graph.ids;
            candidate.outputs=nativeAccepted_.hardware;candidate.logicalOutputs=logicalOutputs_;
            candidate.externalOutputs=externalOutputAssignments_;
            bool graphChanged=false;
            if(!P::edit(candidate,edit,live,graphChanged)){
                reply.error="LOGICAL_OUTPUT_EDIT_REJECTED";return reply;
            }
            if(!matchesSessionDevice(pins))return reply;
            if(graphChanged){
                const auto& hw=nativeAccepted_.hardware;
                const auto reserved=hw.sinkPlan().reserved();
                const std::span<const int> callbackPins(pins.outputByCallback.data(),static_cast<std::size_t>(pins.outputCount));
                std::vector<DynamicNativeSessionPublisher::InitialExternalTarget> targets;
                std::vector<std::uint64_t> verified;
                targets.reserve(candidate.externalOutputs.size());verified.reserve(candidate.externalOutputs.size());
                for(const auto& p:candidate.externalOutputs){
                    // An unrelated edit cannot refresh DIRECT authorization.
                    if(!candidate.logicalOutputs.assignedPort(p.channel.value)&&
                       (externalOutputEpoch_!=ep||std::find(externalOutputVerified_.begin(),
                         externalOutputVerified_.end(),p.channel.value)==externalOutputVerified_.end()))continue;
                    if(!armix::session::MixerDeviceBindingPolicy::canRestoreExternalOutput(
                        p,pins.deviceName,callbackPins,pins.outputNames,hw))continue;
                    int l=-1,r=-1;
                    for(int i=0;i<pins.outputCount;++i){
                        if(pins.outputPhysicalPort(i)==p.left)l=i;
                        if(pins.outputPhysicalPort(i)==p.right)r=i;
                    }
                    targets.push_back({p.channel,l,r});verified.push_back(p.channel.value);
                }
                DynamicChannelFxLeaseBank bank;
                if(DynamicChannelFxLeaseBank::reuseRevision(ep,rev+1,*nativeAccepted_.fxOwners,bank)
                    !=DynamicChannelFxLeaseBank::Fault::none){
                    reply.error="NATIVE_FX_LEASE_REJECTED";return reply;
                }
                auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
                owner->bindGlobalRackOverride(&globalFxBypassed_);
                DynamicNativeSessionPublisher::FxPlan fx;
                fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=rev+1;
                fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept{
                    (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
                };
                fx.latency=+[](void* ctx,ChannelId id) noexcept{
                    return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
                };
                fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                    static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
                };
                const DynamicNativeSessionPublisher::InitialExternalPlan plan{
                    pins.outputCount,{reserved.data(),hw.sinkPlan().reservedCount()},targets};
                const auto published=nativePublisher_.commit(nativeAccepted_.graph.roster,
                    nativeAccepted_.graph.request,rev,ep,epoch(),true,std::move(fx),false,false,nullptr,&plan);
                if(!published.ok()){reply.error="LOGICAL_OUTPUT_PCM_COMMIT_REJECTED";return reply;}
                nativeAccepted_.fxOwners=std::move(owner);nativeAccepted_.pcmRevision=published.revision;
                externalOutputAssignments_=std::move(candidate.externalOutputs);
                externalOutputVerified_=std::move(verified);externalOutputEpoch_=externalOutputVerified_.empty()?0:ep;
                nativeMeterEpoch_.store(0,std::memory_order_release);
            }
            logicalOutputs_=std::move(candidate.logicalOutputs);++logicalOutputRevision_;
            nativeControlDirty_=true;nativeStructureLive_=true;
            reply.accepted=true;reply.error="";reply.pcmRevision=nativeAccepted_.pcmRevision;
            return reply;
        }catch(...){reply.error="LOGICAL_OUTPUT_PREPARE_FAILED";return reply;}
    }
    // CONTROL-only primary OUT/WIRE transaction: stable ChannelId routing,
    // native graph DAG validation and immutable PCM publication.
    NativeBusResult setNativePrimaryOutput(std::uint64_t ep,
            std::uint64_t expectedPcmRev,std::uint64_t expectedRosterRev,
            armix::mixer::ChannelId src,armix::mixer::ChannelId dst,
            bool outputsDisarmed=false) {
        using namespace armix::mixer;
        NativeBusResult r{};
        r.pcmRevision=nativeAccepted_.pcmRevision;
        r.rosterRevision=nativeAccepted_.graph.roster.revision();
        if(!outputsDisarmed){r.error="OUTPUT_MUST_BE_DISARMED";return r;}
        if(!nativeStructureLive_||!channelIdPhysicalPcmActive()||!ready()||
           ep!=epoch()||nativeAccepted_.deviceEpoch!=ep||
           !nativeAccepted_.fxOwners||
           expectedPcmRev!=nativeAccepted_.pcmRevision||
           expectedRosterRev!=nativeAccepted_.graph.roster.revision())return r;
        const auto* source=nativeAccepted_.graph.roster.find(src);
        const auto* target=nativeAccepted_.graph.roster.find(dst);
        if(!source||(source->role!=ChannelRole::input&&
                       source->role!=ChannelRole::group)||
           (dst.value&&(!target||(target->role!=ChannelRole::main&&
                                  target->role!=ChannelRole::group)))||src==dst){
            r.error="INVALID_NATIVE_WIRE_ENDPOINT";return r;
        }
        for(const auto id:nativeAccepted_.graph.ids.inputs)
            if(id==src){r.error="M4_INPUT_ROUTE_NOT_DYNAMIC";return r;}
        if(originalM4Aux(src)){r.error="M4_AUX_ROUTE_NOT_DYNAMIC";return r;}
        try {
            auto roster=nativeAccepted_.graph.roster;
            armix::mixer::ChannelEdit edit;
            edit.kind=armix::mixer::ChannelEdit::Kind::setPrimaryOutput;
            edit.id=src;edit.target=dst;
            if(!roster.edit(expectedRosterRev,
                           std::span<const armix::mixer::ChannelEdit>(&edit,1)).ok()){
                r.error="NATIVE_WIRE_ROUTE_REJECTED";return r;
            }
            DynamicChannelFxLeaseBank nextBank;
            if(DynamicChannelFxLeaseBank::reuseRevision(
                   ep,expectedPcmRev+1,*nativeAccepted_.fxOwners,nextBank)!=
                   DynamicChannelFxLeaseBank::Fault::none){
                r.error="NATIVE_WIRE_FX_LEASE_REJECTED";return r;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(nextBank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* rr,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,rr,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=ep;fx.graphRevision=expectedPcmRev+1;
            const auto published=nativePublisher_.commit(
                 roster,nativeAccepted_.graph.request,expectedPcmRev,
                 ep,epoch(),true,std::move(fx));
            if(!published.ok()){
                r.error="NATIVE_WIRE_PCM_GRAPH_REJECTED";return r;
            }
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            nativeMeterEpoch_.store(0,std::memory_order_release);
            r.accepted=true;r.error="";
            r.pcmRevision=published.revision;
            r.rosterRevision=nativeAccepted_.graph.roster.revision();
            return r;
        }catch(...){
            r.error="NATIVE_WIRE_PREPARE_FAILED";return r;
        }
    }


    // Real installed ChannelId PCM transaction. No alternate router: compile
    // the accepted roster's SAME send inventory, lease the SAME native VST3
    // processors and publish once to the JUCE callback's existing publisher.
    // M4 cannot represent arbitrary WIRE afterwards; never downgrade it.
    // Novice-first starter: derive sources from the accepted MAIN primary
    // routes, not browser strip indexes. Do not duplicate upstream group
    // paths or overwrite any authored headphone sends.
    NativeBusResult seedIndependentMonitor(std::uint64_t ep,std::uint64_t rev,
                                          std::uint64_t rosterRev) {
        using namespace armix::mixer;
        NativeBusResult rejected;
        rejected.error="Headphone mix is not ready or already configured";
        const auto& graph=nativeAccepted_.graph;
        if(!ready()||!channelIdPhysicalPcmActive()||!nativeAccepted_.hardware.secondaryMain()||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||!nativeBusOutputObserved()||
           graph.roster.revision()!=rosterRev||!graph.ids.monitor.value||!graph.ids.main.value)
            return rejected;
        const auto changes=planHeadphoneStarter(graph.roster,graph.ids.main,graph.ids.monitor);
        if(changes.empty()){
            rejected.error="No eligible MAIN sources or headphone mix already configured";
            return rejected;
        }
        return applyNativeWireTransaction(ep,rev,rosterRev,changes);
    }

    NativeBusResult applyNativeWireTransaction(
            std::uint64_t ep,std::uint64_t expectedPcmRev,
            std::uint64_t expectedRosterRev,
            std::span<const armix::mixer::WireChange> changes) {
        using namespace armix::mixer;
        NativeBusResult r{};
        r.pcmRevision=nativeAccepted_.pcmRevision;
        r.rosterRevision=nativeAccepted_.graph.roster.revision();
        // This method has NO physical request argument. Always publish
        // the previously accepted identical source/clock configuration.
        if(!channelIdPhysicalPcmActive()||!ready()||
           ep!=epoch()||nativeAccepted_.deviceEpoch!=ep||
           !nativeAccepted_.fxOwners||
           expectedPcmRev!=nativeAccepted_.pcmRevision||
           expectedRosterRev!=nativeAccepted_.graph.roster.revision()) {
            r.error="STALE_NATIVE_WIRE_GRAPH";return r;
        }
        if(changes.empty()||changes.size()>256){
            r.error="INVALID_NATIVE_WIRE_BATCH";return r;
        }
        try {
            auto roster=nativeAccepted_.graph.roster;
            const auto changed=roster.wireTransaction(expectedRosterRev,changes);
            if(!changed.ok()){
                r.error=changed.fault==ChannelEditFault::missingEdge
                    ?"NATIVE_WIRE_STALE_EDGE":
                    changed.fault==ChannelEditFault::duplicateSend
                    ?"NATIVE_WIRE_DUPLICATE_ROUTE":"NATIVE_WIRE_TRANSACTION_REJECTED";
                return r;
            }
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseRevision(
                    ep,expectedPcmRev+1,*nativeAccepted_.fxOwners,bank)!=
               DynamicChannelFxLeaseBank::Fault::none){
                r.error="NATIVE_WIRE_FX_LEASE_REJECTED";return r;
            }
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(owner);
            fx.context=owner.get();
            fx.render=+[](void* ctx,ChannelId id,float* l,float* rr,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,rr,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            fx.deviceEpoch=ep;
            fx.graphRevision=expectedPcmRev+1;
            const auto published=nativePublisher_.commit(
                roster,nativeAccepted_.graph.request,expectedPcmRev,
                ep,epoch(),false,std::move(fx),true);
            if(!published.ok()){
                r.error=published.graphError.fault==DynamicGraphFault::cycle
                    ?"NATIVE_WIRE_FEEDBACK_CYCLE":"NATIVE_WIRE_PCM_GRAPH_REJECTED";
                return r;
            }
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=published.revision;
            reapplyExternalOutputsSameEpoch();
            nativeStructureLive_=true; // never fall back to incompatible M4
            nativeMeterEpoch_.store(0,std::memory_order_release);
            r.accepted=true;r.error="";
            r.pcmRevision=published.revision;
            r.rosterRevision=nativeAccepted_.graph.roster.revision();
            return r;
        }catch(...){
            r.error="NATIVE_WIRE_PREPARE_FAILED";return r;
        }
    }

    // E6: Original migrated CH1-10 AND added dynamic INPUTs are driven
    // by the same installed ChannelId publisher, callback and VST3 lease.
    // Identity/epoch/revision gates precede any audible DSP mutation.
    bool setNativeInputMix(std::uint64_t epochValue,std::uint64_t revision,
                           armix::mixer::ChannelId id,float gain,float pan) noexcept {
        if(!channelIdPhysicalPcmActive()||
           nativeAccepted_.deviceEpoch!=epochValue||
           epochValue!=epoch()||revision!=nativeAccepted_.pcmRevision||
           !std::isfinite(gain)||gain<0.f||gain>4.f||
           !std::isfinite(pan)||pan< -1.f||pan>1.f)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel||channel->role!=armix::mixer::ChannelRole::input)return false;
        if(!nativePublisher_.setChannelMix(epochValue,revision,id,gain,pan))
            return false;
        if(!nativeAccepted_.graph.roster.rememberLiveMix(id,gain,pan)) {
            topologyFault_.store(true,std::memory_order_release);
            return false; // never ACK a rendered state missing from readback
        }
        nativeControlDirty_=true;
        return true;
    }
    // Real ChannelId pre-insert digital input trim. Safe 64-sample DSP ramp;
    // no new graph, callback, plugin instance or WIRE edit.
    bool setNativeInputGain(std::uint64_t epochValue,std::uint64_t revision,
                            armix::mixer::ChannelId id,float gain) noexcept {
        if(!channelIdPhysicalPcmActive()||
           nativeAccepted_.deviceEpoch!=epochValue||epochValue!=epoch()||
           revision!=nativeAccepted_.pcmRevision||
           !std::isfinite(gain)||gain<0.f||gain>16.f)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel||channel->role!=armix::mixer::ChannelRole::input)return false;
        if(!nativePublisher_.setChannelInputGain(epochValue,revision,id,gain))return false;
        if(!nativeAccepted_.graph.roster.rememberLiveInputGain(id,gain)) {
            topologyFault_.store(true,std::memory_order_release);
            return false;
        }
        nativeControlDirty_=true;
        return true;
    }
    bool setNativeWireSumGain(std::uint64_t epochValue,std::uint64_t revision,
                            armix::mixer::ChannelId id,float gain) noexcept {
        if(!channelIdPhysicalPcmActive()||
           nativeAccepted_.deviceEpoch!=epochValue||epochValue!=epoch()||
           revision!=nativeAccepted_.pcmRevision||
           !std::isfinite(gain)||gain<0.f||gain>2.f)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel||(channel->role!=armix::mixer::ChannelRole::input && channel->role!=armix::mixer::ChannelRole::group && channel->role!=armix::mixer::ChannelRole::fxReturn))return false;
        if(!nativePublisher_.setChannelWireSumGain(epochValue,revision,id,gain))return false;
        if(!nativeAccepted_.graph.roster.rememberLiveWireSumGain(id,gain)) {
            topologyFault_.store(true,std::memory_order_release);
            return false;
        }
        nativeControlDirty_=true;
        return true;
    }
    bool setNativeInputMuteSolo(std::uint64_t epochValue,std::uint64_t revision,
                                armix::mixer::ChannelId id,
                                bool mute,bool solo) noexcept {
        if(!channelIdPhysicalPcmActive()||
           nativeAccepted_.deviceEpoch!=epochValue||
           epochValue!=epoch()||revision!=nativeAccepted_.pcmRevision)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel||channel->role!=armix::mixer::ChannelRole::input)return false;
        if(!nativePublisher_.setChannelMuteSolo(
                  epochValue,revision,id,mute,solo))return false;
        if(!nativeAccepted_.graph.roster.rememberLiveMuteSolo(id,mute,solo)) {
            topologyFault_.store(true,std::memory_order_release);
            return false;
        }
        nativeControlDirty_=true;
        return true;
    }

    bool nativeBusTopologyLive() const noexcept {
        return nativeStructureLive_ && channelIdPhysicalPcmActive();
    }
    // Live bus controls update only verified ChannelIds and their canonical
    // roster values. Group/FX RETURN faders are actual post-insert PCM,
    // not a browser-only knob or duplicated legacy M4 strip.
    bool setNativeBusMix(std::uint64_t epochAtEdit,
                         std::uint64_t expectedPcmRevision,
                         armix::mixer::ChannelId id,float gain,float pan) noexcept {
        if(!channelIdPhysicalPcmActive() || epochAtEdit!=epoch() ||
           expectedPcmRevision!=nativeAccepted_.pcmRevision)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel || (channel->role!=armix::mixer::ChannelRole::group &&
                        channel->role!=armix::mixer::ChannelRole::fxReturn) ||
           originalM4Aux(id))
            return false; // original AUX 1–4 belong to M4 control state
        if(!nativePublisher_.setChannelMix(
             epochAtEdit,expectedPcmRevision,id,gain,pan))return false;
        return nativeAccepted_.graph.roster.rememberLiveMix(id,gain,pan);
    }

    // Realtime GROUP/FX_RETURN mute and solo have the same verified
    // ChannelId PCM target as INPUT, but NEVER mutate an original M4 AUX.
    bool setNativeBusMuteSolo(std::uint64_t epochAtEdit,
                              std::uint64_t expectedPcmRevision,
                              armix::mixer::ChannelId id,
                              bool mute,bool solo) noexcept {
        if(!channelIdPhysicalPcmActive()||epochAtEdit!=epoch()||
           expectedPcmRevision!=nativeAccepted_.pcmRevision)return false;
        const auto* channel=nativeAccepted_.graph.roster.find(id);
        if(!channel || (channel->role!=armix::mixer::ChannelRole::group &&
                        channel->role!=armix::mixer::ChannelRole::fxReturn) ||
           originalM4Aux(id))return false;
        if(!nativePublisher_.setChannelMuteSolo(
             epochAtEdit,expectedPcmRevision,id,mute,solo))return false;
        if(!nativeAccepted_.graph.roster.rememberLiveMuteSolo(id,mute,solo)) {
            topologyFault_.store(true,std::memory_order_release);
            return false;
        }
        nativeControlDirty_=true;
        return true;
    }

    // EdgeId-keyed live SEND level: no graph compile, VST recreation or
    // callback handoff. Primary outputs retain their unity-gain contract.
    bool setNativeWireGain(std::uint64_t ep,std::uint64_t graphRev,
                           std::uint64_t rosterRev,armix::mixer::ChannelId source,
                           std::uint64_t edgeId,float gain) noexcept {
        if(!channelIdPhysicalPcmActive()||!ready()||ep!=epoch()||
           graphRev!=nativeAccepted_.pcmRevision||
           rosterRev!=nativeAccepted_.graph.roster.revision()||
           !std::isfinite(gain)||gain<0.f||gain>4.f)return false;
        const auto* row=nativeAccepted_.graph.roster.find(source);
        if(!row||!edgeId)return false;
        for(const auto& edge:row->sends)
            if(edge.edgeId==edgeId&&!edge.primaryOutput) {
                if(!nativePublisher_.setWireGain(ep,graphRev,source,edgeId,gain))return false;
                if(!nativeAccepted_.graph.roster.rememberLiveParallelSend(
                       source,edge.destination,edge.preFader,gain))return false;
                nativeControlDirty_=true;
                return true;
            }
        return false;
    }
    // SEND ON/OFF changes only the selected existing EdgeId audio gate.
    // A 64-sample PCM ramp preserves the saved level/tap/PAN and WIRE.
    bool setNativeWireEnabled(std::uint64_t ep,std::uint64_t graphRev,
                              std::uint64_t rosterRev,armix::mixer::ChannelId source,
                              std::uint64_t edgeId,bool enabled) noexcept {
        if(!channelIdPhysicalPcmActive()||!ready()||ep!=epoch()||
           graphRev!=nativeAccepted_.pcmRevision||
           rosterRev!=nativeAccepted_.graph.roster.revision())return false;
        const auto* row=nativeAccepted_.graph.roster.find(source);
        if(!row||!edgeId)return false;
        for(const auto& edge:row->sends)
            if(edge.edgeId==edgeId&&!edge.primaryOutput){
                if(!nativePublisher_.setWireEnabled(ep,graphRev,source,edgeId,enabled))
                    return false;
                if(!nativeAccepted_.graph.roster.rememberLiveSendEnabled(source,edgeId,enabled)){
                    topologyFault_.store(true,std::memory_order_release);
                    return false;
                }
                nativeControlDirty_=true;
                return true;
            }
        return false;
    }
    int nativePcmLatencySamples() const noexcept {
        return channelIdPhysicalPcmActive()?
            nativePublisher_.latencySamples(epoch(),nativeAccepted_.pcmRevision):0;
    }
    armix::mixer::DynamicPreparedMixerBlock::LatencyPlanSummary nativePcmLatencyPlan() const noexcept {
        return channelIdPhysicalPcmActive()?
            nativePublisher_.latencyPlanSummary(epoch(),nativeAccepted_.pcmRevision):
            armix::mixer::DynamicPreparedMixerBlock::LatencyPlanSummary{};
    }
    // CONTROL snapshot: genuine stable ChannelIds from the SAME publisher
    // as physical PCM. No guessed IDs, synthetic UI routes or JS DSP.

    // Cooperative CONTROL capture of the exact detached FX bank. VST3 state
    // calls retain their message-thread affinity; each turn captures at most
    // one processor, without a wait/sleep loop or physical driver query.
    struct RetainedMixerCapture final {
        armix::session::MixerSessionDocument document;
        std::vector<armix::shows::BinaryState> states;
        std::shared_ptr<armix::mixer::DynamicChannelFxLeaseBank> owners;
        std::set<juce::String> hashes;
        std::uint64_t observedEpoch=0,ownerEpoch=0,revision=0;
        std::size_t channel=0,slot=0;
        juce::int64 total=0;
    };
    bool retainedCaptureCurrent(const RetainedMixerCapture& c) const noexcept {
        return c.owners && hasRetainedNativeSession() && epoch()==c.observedEpoch &&
            nativeAccepted_.deviceEpoch==c.ownerEpoch &&
            nativeAccepted_.pcmRevision==c.revision &&
            (!c.document.acceptedRevision || nativeAccepted_.graph.roster.revision()==c.document.acceptedRevision) &&
            nativeAccepted_.fxOwners==c.owners &&
            c.owners->epoch()==c.ownerEpoch && c.owners->revision()==c.revision;
    }
    juce::String beginRetainedMixerCapture(RetainedMixerCapture& capture) {
        try {
            RetainedMixerCapture c;
            c.observedEpoch=epoch();
            c.ownerEpoch=nativeAccepted_.deviceEpoch;
            c.revision=nativeAccepted_.pcmRevision;
            c.owners=nativeAccepted_.fxOwners;
            if(!retainedCaptureCurrent(c))return "No authoritative retained native mixer";
            if(c.owners->externalChannel().value)
                return "External CH1 FX owner cannot yet be serialized independently";
            auto& doc=c.document;
            doc.roster=nativeAccepted_.graph.roster;
            doc.acceptedRevision=doc.roster.revision();
            doc.legacyIds=nativeAccepted_.graph.ids;
            doc.outputs=nativeAccepted_.hardware;
            doc.request=nativeAccepted_.graph.request;
            doc.waitingAsioInputs=waitingAsioInputs_;
            doc.logicalInputs=logicalInputs_;
            doc.logicalOutputs=logicalOutputs_;
            doc.externalOutputs=externalOutputAssignments_;
            for(const auto& channel:doc.roster.channels()) {
                if(c.owners->insertCount(channel.id)>static_cast<std::size_t>(FxGraph::maxInsertsPerStrip))
                    return "Native FX count exceeded per-channel rack";
                if(c.owners->rackBypassed(channel.id))doc.rackBypassed.push_back(channel.id);
            }
            capture=std::move(c);
            return {};
        } catch(...) {return "Retained mixer capture initialization failed";}
    }
    // The prior PCM epoch is no longer admitted. Its queued scalar edits
    // may never get another callback. Only known, bounded native mailboxes
    // can be settled here, under BOTH the graph render permit and processor
    // gate. No processBlock, fake clock, arbitrary VST call or live ACK.
    // Old-epoch completions are drained into the processor's accepted state;
    // the new logical workspace readback is the only new authority.
    template<class Processor>
    static bool settleDetachedMailbox(FxGraph::FxInstance& fx) noexcept {
        auto* p=dynamic_cast<Processor*>(fx.processor.get());
        if(!p)return false;
        typename Processor::Completion completion{};
        (void)p->pollCompletion(completion);
        p->consumeRealtimeEdits();
        (void)p->pollCompletion(completion);
        return true;
    }
    enum class DetachedMailbox { ready,retry,unsupported };
    static DetachedMailbox settleDetachedNativeFx(FxGraph::FxInstance& fx) noexcept {
        armix::rt::RealtimeRenderPermit processorPermit(fx.processing);
        if(!processorPermit)return DetachedMailbox::retry;
        if(fx.processFaulted())return DetachedMailbox::unsupported;
        if(!fx.nativeControl||fx.nativeControl->stateSnapshotQuiescent())
            return DetachedMailbox::ready;
        using namespace armix::builtin_fx;
        const bool supported=settleDetachedMailbox<ChannelEqProcessor>(fx) ||
            settleDetachedMailbox<GraphicEq31Processor>(fx) ||
            settleDetachedMailbox<ChannelCompressorProcessor>(fx) ||
            settleDetachedMailbox<AutoDuckerProcessor>(fx) ||
            settleDetachedMailbox<ChannelHarmonyProcessor>(fx);
        if(!supported)return DetachedMailbox::unsupported;
        return fx.nativeControl->stateSnapshotQuiescent()?
            DetachedMailbox::ready:DetachedMailbox::retry;
    }

    juce::String stepRetainedMixerCapture(RetainedMixerCapture& c,bool& done) {
        done=false;
        if(!retainedCaptureCurrent(c))return "Audio connection changed; retry offline FX";
        try {
            const auto& rows=c.document.roster.channels();
            while(c.channel<rows.size() && c.slot>=c.owners->insertCount(rows[c.channel].id)) {
                ++c.channel;c.slot=0;
            }
            if(c.channel==rows.size()){done=true;return {};}
            armix::rt::RealtimeRenderPermit permit(rendering_);
            if(!permit)return {}; // next CONTROL turn, never sleep
            const auto id=rows[c.channel].id;
            const auto* owner=c.owners->ownerForControl(id,c.slot);
            if(!owner||!*owner||!(*owner)->processor||(*owner)->catalogId.isEmpty())
                return "Native FX lease missing during capture";
            if(!retainedCaptureCurrent(c))return "Audio connection changed; retry offline FX";
            const auto settlement=settleDetachedNativeFx(**owner);
            if(settlement==DetachedMailbox::retry)return {}; // bounded next CONTROL turn
            if(settlement==DetachedMailbox::unsupported)
                return "FX state not safely quiescent: "+(*owner)->catalogId;
            juce::MemoryBlock bytes;
            if(!(*owner)->tryCaptureState(bytes))
                return "VST3 state not quiescent: "+(*owner)->catalogId;
            if(!retainedCaptureCurrent(c))return "Audio connection changed; retry offline FX";
            c.total+=static_cast<juce::int64>(bytes.getSize());
            if(c.total>128LL*1024LL*1024LL)return "Native mixer vendor-state budget exceeded";
            const auto hash=juce::SHA256(bytes).toHexString();
            c.document.inserts.push_back({id,c.slot,(*owner)->catalogId,
                {},hash,(*owner)->name,0,
                (*owner)->bypassRequested.load(std::memory_order_acquire),(*owner)->instanceId});
            if(c.hashes.insert(hash).second) {
                armix::shows::BinaryState state;
                state.sha256=hash;state.bytes=std::move(bytes);
                c.states.push_back(std::move(state));
            }
            ++c.slot; // exactly one vendor read per CONTROL turn
            return {};
        } catch(...) {return "Retained FX state capture failed";}
    }

    // CONTROL-only capture of the currently INSTALLED ChannelId PCM graph.
    // Fail closed if any vendor cannot supply a quiescent binary state.
    juce::String captureNativeMixerSession(
            armix::session::MixerSessionDocument& document,
            std::vector<armix::shows::BinaryState>& blobs,
            bool retained=false) noexcept {
        using namespace armix::mixer;
        const auto observedEpoch=epoch();
        const auto documentEpoch=retained?nativeAccepted_.deviceEpoch:observedEpoch;
        const auto retainedOwners=nativeAccepted_.fxOwners;
        // On physical STOP the callback publisher may already have retired
        // its routing generation, but the exact immutable accepted FX lease
        // bank and authored roster are still retained by the CONTROL owner.
        // This permits a detached binary snapshot, NOT a physical edit.
        const bool offlineSource=retained && hasRetainedNativeSession() &&
            documentEpoch!=0 && nativeAccepted_.pcmRevision!=0 &&
            retainedOwners &&
            retainedOwners->epoch()==documentEpoch &&
            retainedOwners->revision()==nativeAccepted_.pcmRevision;
        if((!retained && (!channelIdPhysicalPcmActive() ||
               !nativeAccepted_.matches(nativePublisher_,documentEpoch,
                                        nativeAccepted_.pcmRevision))) ||
           (retained && !offlineSource))
            return "No authoritative native ChannelId graph";
        if(nativeAccepted_.fxOwners->externalChannel().value)
            return "External CH1 FX owner cannot yet be serialized independently";
        // MAIN and STREAM are muted by the caller. Wait briefly for the
        // current callback to leave the vendor processors before capturing.
        std::optional<armix::rt::RealtimeRenderPermit> renderPermit;
        for(int attempt=0;attempt<50;++attempt) {
            renderPermit.emplace(rendering_);
            if(*renderPermit)break;
            juce::Thread::sleep(1);
        }
        if(!*renderPermit)return "Audio callback did not quiesce for mixer capture";
        try {
            armix::session::MixerSessionDocument draft;
            draft.roster=nativeAccepted_.graph.roster;
            draft.acceptedRevision=draft.roster.revision();
            draft.legacyIds=nativeAccepted_.graph.ids;
            draft.outputs=nativeAccepted_.hardware;
            draft.request=nativeAccepted_.graph.request;
            draft.waitingAsioInputs=waitingAsioInputs_;
            draft.logicalInputs=logicalInputs_;
            draft.logicalOutputs=logicalOutputs_;
            draft.externalOutputs=externalOutputAssignments_;
            std::vector<armix::shows::BinaryState> staged;
            std::set<juce::String> hashes;
            juce::int64 total=0;
            for(const auto& channel:draft.roster.channels()) {
                const auto count=nativeAccepted_.fxOwners->insertCount(channel.id);
                if(count>static_cast<std::size_t>(FxGraph::maxInsertsPerStrip))
                    return "Native FX count exceeded per-channel rack";
                if(nativeAccepted_.fxOwners->rackBypassed(channel.id))
                    draft.rackBypassed.push_back(channel.id);
                for(std::size_t slot=0;slot<count;++slot) {
                    const auto* owner=nativeAccepted_.fxOwners->ownerForControl(channel.id,slot);
                    if(!owner||!*owner||!(*owner)->processor||
                       (*owner)->catalogId.isEmpty())
                        return "Native FX lease missing during capture";
                    juce::MemoryBlock bytes;
                    if(!(*owner)->tryCaptureState(bytes))
                        return "VST3 state not quiescent: "+(*owner)->catalogId;
                    total+=static_cast<juce::int64>(bytes.getSize());
                    if(total>128LL*1024LL*1024LL)
                        return "Native mixer vendor-state budget exceeded";
                    const auto hash=juce::SHA256(bytes).toHexString();
                    draft.inserts.push_back({channel.id,slot,(*owner)->catalogId,
                        {},hash,(*owner)->name,0,
                        (*owner)->bypassRequested.load(std::memory_order_acquire),
                        (*owner)->instanceId});
                    if(hashes.insert(hash).second) {
                        armix::shows::BinaryState entry;
                        entry.sha256=hash;entry.bytes=std::move(bytes);
                        staged.push_back(std::move(entry));
                    }
                }
            }
            if(epoch()!=observedEpoch || nativeAccepted_.fxOwners!=retainedOwners ||
               nativeAccepted_.deviceEpoch!=documentEpoch ||
               (!retained && !nativeAccepted_.matches(nativePublisher_,
                                      documentEpoch,nativeAccepted_.pcmRevision)) ||
               (retained && (!hasRetainedNativeSession() ||
                   retainedOwners->revision()!=nativeAccepted_.pcmRevision)))
                return "Native graph changed during FX snapshot";
            document=std::move(draft);
            blobs=std::move(staged);
            return {};
        } catch(...) {return "Native graph/VST3 state capture failed";}
    }

    // O1 CONTROL: true ASIO physical pin identity -> packed callback ordinal.
    // No OUT reconfiguration is allowed to steal MAIN or MONITOR. Every edit
    // preserves the existing internal WIRE graph and FX lease.
    juce::String setExternalOutput(
            armix::mixer::ChannelId channel,std::uint64_t expectedEpoch,
            std::uint64_t expectedRevision,int physicalLeft,int physicalRight,
            bool confirmed,const armix::device::JuceActivePortSnapshot& pins) {
        using namespace armix::mixer;
        if(!channelIdPhysicalPcmActive() || !ready() ||
           !nativeAccepted_.matches(nativePublisher_,expectedEpoch,expectedRevision) ||
           !matchesSessionDevice(pins) || pins.epoch!=expectedEpoch ||
           pins.typeName!="ASIO")
            return "EXTERNAL OUT: stale ASIO device, epoch or PCM revision";
        const auto* row=nativeAccepted_.graph.roster.find(channel);
        if(!row || (row->role!=ChannelRole::input &&
                    row->role!=ChannelRole::group &&
                    row->role!=ChannelRole::fxReturn))
            return "EXTERNAL OUT: select a live INPUT, GROUP or FX RETURN";
        if(logicalOutputs_.assignedPort(channel.value))
            return "EXTERNAL OUT: channel owns a logical output; edit its logical reference";
        const bool none=physicalLeft==-1 && physicalRight==-1;
        int left=-1,right=-1;
        armix::session::StoredExternalOutput candidate;
        candidate.channel=channel;
        if(!none) {
            if(physicalLeft<0 || physicalLeft%2!=0 ||
               physicalRight!=physicalLeft+1 ||
               physicalRight>=pins.outputNames.size() ||
               armix::device::uniqueNamedPin(pins.outputNames,pins.outputNames[physicalLeft])!=physicalLeft ||
               armix::device::uniqueNamedPin(pins.outputNames,pins.outputNames[physicalRight])!=physicalRight)
                return "EXTERNAL OUT: invalid or unnamed stereo physical pins";
            for(int i=0;i<pins.outputCount;++i) {
                if(pins.outputPhysicalPort(i)==physicalLeft)left=i;
                if(pins.outputPhysicalPort(i)==physicalRight)right=i;
            }
            if(left<0 || right<0 || left==right)
                return "EXTERNAL OUT: selected pair is not active in the ASIO mask";
            const auto& hw=nativeAccepted_.hardware;
            if(left==hw.mainL || left==hw.mainR ||
               right==hw.mainL || right==hw.mainR)
                return "EXTERNAL OUT: this stereo pair belongs to MAIN";
            if(hw.monitorEnabled && (left==hw.monitorL || left==hw.monitorR ||
               right==hw.monitorL || right==hw.monitorR))
                return "EXTERNAL OUT: this stereo pair belongs to MONITOR";
            // New amplifier pins must be confirmed by the operator, even
            // when a UI dropdown was populated from a real device snapshot.
            if(logicalOutputPairReserved(pins.deviceName,physicalLeft,physicalRight))
                return "EXTERNAL OUT: physical pair reserved by logical output";
            if(!confirmed)return "EXTERNAL OUT: confirm the exact physical pair";
            candidate.left=physicalLeft;candidate.right=physicalRight;
            candidate.leftName=pins.outputNames[physicalLeft];
            candidate.rightName=pins.outputNames[physicalRight];
            candidate.playbackDevice=pins.deviceName; // confirmed provenance
        } else if(physicalLeft!=-1 || physicalRight!=-1)
            return "EXTERNAL OUT: malformed NONE pair";
        auto amended=externalOutputAssignments_; // CONTROL only; may allocate
        amended.erase(std::remove_if(amended.begin(),amended.end(),
            [channel](const auto& p){return p.channel==channel;}),amended.end());
        if(!none)amended.push_back(candidate);
        // Per-ChannelId authorization: remapping ONE channel on a new device
        // never re-arms other saved amplifiers at matching old port numbers.
        auto verified=externalOutputEpoch_==expectedEpoch?
            externalOutputVerified_:std::vector<std::uint64_t>{};
        std::erase(verified,channel.value);
        if(!none)verified.push_back(channel.value);
        if(!nativePublisher_.setExternalOutput(expectedEpoch,expectedRevision,
                channel,left,right,pins.outputCount))
            return "EXTERNAL OUT: native graph rejected; previous sound retained";
        externalOutputAssignments_=std::move(amended);
        externalOutputVerified_=std::move(verified);
        externalOutputEpoch_=expectedEpoch;
        return {};
    }

    // The accepted graph can be rebuilt for a WIRE/FX edit in the SAME
    // verified epoch. Reinstall only existing known pins: never auto-arm
    // stale destinations after a device restart or a different ASIO device.
    void reapplyExternalOutputsSameEpoch() noexcept {
        // Prune orphan references without erasing the named output port.
        for(std::size_t i=logicalOutputs_.references().size();i>0;--i){
            const auto id=logicalOutputs_.references()[i-1].channel;
            if(!nativeAccepted_.graph.roster.find({id})&&logicalOutputs_.clear(id))++logicalOutputRevision_;
        }
        // Remove deleted ChannelIds BEFORE saving an updated .smix.
        std::erase_if(externalOutputAssignments_,[&](const auto& p){
            return nativeAccepted_.graph.roster.find(p.channel)==nullptr;
        });
        std::erase_if(externalOutputVerified_,[&](const auto id){
            return nativeAccepted_.graph.roster.find(armix::mixer::ChannelId{id})==nullptr;
        });
        if(externalOutputEpoch_!=nativeAccepted_.deviceEpoch)return;
        for(const auto& p:externalOutputAssignments_) {
            if(std::find(externalOutputVerified_.begin(),
                 externalOutputVerified_.end(),p.channel.value)==
                 externalOutputVerified_.end())continue;
            int left=-1,right=-1;
            const int count=callbackOutputCount_.load(std::memory_order_acquire);
            for(int i=0;i<count;++i) {
                const int physical=enabledOutputPorts_[i].load(std::memory_order_relaxed);
                if(physical==p.left)left=i;
                if(physical==p.right)right=i;
            }
            const auto& hw=nativeAccepted_.hardware;
            if(left<0 || right<0 || left==right ||
               left==hw.mainL || right==hw.mainR || left==hw.mainR ||
               right==hw.mainL || (hw.monitorEnabled &&
               (left==hw.monitorL || left==hw.monitorR ||
                right==hw.monitorL || right==hw.monitorR)))continue;
            (void)nativePublisher_.setExternalOutput(nativeAccepted_.deviceEpoch,
                nativeAccepted_.pcmRevision,p.channel,left,right,count);
        }
    }

    // Explicit two-pin ASIO sink transaction. Never repurpose a live pair.
    // Legacy explicit split may start empty; smart split seeds a new MONITOR atomically.
    juce::String patchDmxSplitOutput(std::uint64_t ep,std::uint64_t rev,
            std::uint64_t rosterRev,bool enable,
            const armix::device::JuceActivePortSnapshot& pins,bool outputsDisarmed,
            bool seedStarter=false) {
        using namespace armix::mixer;
        if(!outputsDisarmed)return "DMX: both outputs must be disarmed";
        if(!ready()||!channelIdPhysicalPcmActive()||pins.epoch!=ep||
           pins.typeName!="ASIO"||pins.outputCount!=2||!matchesSessionDevice(pins)||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||
           !nativeBusOutputObserved()||nativeAccepted_.graph.roster.revision()!=rosterRev)
            return "DMX: stale or unobserved two-output ASIO mapping";
        const auto old=nativeAccepted_.hardware;
        if(!old.valid()||old.activeOutputChannels!=2||old.secondaryMain()==enable)
            return "DMX: requested output mode is unchanged or unavailable";
        // Do not steal a saved amplifier or logical output binding. The
        // operator must explicitly clear those intentions first.
        if(!externalOutputAssignments_.empty()||!logicalOutputs_.references().empty()||
           std::any_of(logicalOutputs_.ports().begin(),logicalOutputs_.ports().end(),
               [](const auto& port){return port.binding.has_value();}))
            return "DMX: clear existing external/logical output bindings first";
        for(int i=0;i<2;++i){
            const int pin=pins.outputPhysicalPort(i);
            if(pin<0||pin>=pins.outputNames.size()||
               armix::device::uniqueNamedPin(pins.outputNames,pins.outputNames[pin])!=pin)
                return "DMX: both ASIO pin names must be verified";
        }
        try {
            auto roster=nativeAccepted_.graph.roster;
            auto monitor=nativeAccepted_.graph.ids.monitor;
            VerifiedNativeOutputs next{};
            if(enable){
                if(old.monitorEnabled)return "DMX: existing physical MONITOR is reserved";
                if(!monitor.value){
                    armix::mixer::ChannelEdit edit;edit.kind=armix::mixer::ChannelEdit::Kind::add;
                    edit.position=roster.channels().size();edit.role=ChannelRole::monitor;
                    edit.format=ProcessingFormat::stereo;edit.text="MONITOR";
                    const auto changed=roster.edit(rosterRev,std::span<const armix::mixer::ChannelEdit>(&edit,1));
                    if(!changed.ok()||changed.createdIds.size()!=1)return "DMX: MONITOR creation rejected";
                    monitor=changed.createdIds.front();
                    if(seedStarter) {
                        const auto changes=planHeadphoneStarter(roster,nativeAccepted_.graph.ids.main,monitor);
                        if(changes.empty()||!roster.wireTransaction(roster.revision(),changes).ok())
                            return "DMX: no eligible MAIN sources for a safe headphone mix";
                    }
                }
                const auto* row=roster.find(monitor);
                if(!row||row->role!=ChannelRole::monitor)return "DMX: invalid MONITOR identity";
                next={2,-1,-1,old.mainL,old.mainR,true};
            }else{
                const auto* row=roster.find(monitor);
                if(!row||row->role!=ChannelRole::monitor)return "DMX: invalid MONITOR identity";
                // Same mix is a physical destination choice, not a delete.
                // Retain the entire headphone graph and FX lease; its sink
                // is intentionally unbound until Separate is selected again.
                next={2,old.monitorL,old.monitorR,-1,-1,false};
            }
            if(!next.valid()||!matchesSessionDevice(pins))return "DMX: device changed";
            DynamicChannelFxLeaseBank bank;
            if(DynamicChannelFxLeaseBank::reuseRevision(ep,rev+1,*nativeAccepted_.fxOwners,bank)
                !=DynamicChannelFxLeaseBank::Fault::none)return "DMX: FX lease rejected";
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>(std::move(bank));
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=rev+1;
            fx.render=+[](void* ctx,ChannelId id,float* l,float* rr,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,rr,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            auto request=nativeAccepted_.graph.request;
            // A dormant MONITOR must not keep independent physical timing.
            // Preserve its authored sends and FX, not the temporary timing policy.
            if(!enable)request.monitorLatencyPolicy=MonitorLatencyPolicy::aligned;
            const auto committed=nativePublisher_.commit(roster,request,
                rev,ep,epoch(),true,std::move(fx));
            if(!committed.ok())return "DMX: PCM graph rejected";
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.graph.roster=std::move(roster);
            nativeAccepted_.graph.ids.monitor=monitor;
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=committed.revision;
            nativeAccepted_.hardware=next;
            nativeStructureLive_=true;nativeControlDirty_=true;
            busPatchEpoch_=ep;
            installNativeHardware(ep,next,nativeAccepted_.graph.request.maxFrames);
            return {};
        }catch(...){return "DMX: output transaction failed";}
    }

    // CONTROL only, explicit operator MUTE checked by LiveAudioEngine.
    // Only the hardware destination changes. Logical MAIN/MONITOR and FX
    // owners, pin masks, callback quantum and graph revision are unchanged.
    juce::String patchNativeBusOutput(std::uint64_t ep,std::uint64_t rev,
            bool monitor,std::array<int,2> expected,std::array<int,2> wanted,
            const juce::String& leftName,const juce::String& rightName,
            const armix::device::JuceActivePortSnapshot& pins) {
        using armix::io::OutputBusPatch;
        if(!ready()||!nativeAccepted_.matches(nativePublisher_,ep,rev)||
           !channelIdPhysicalPcmActive()||pins.epoch!=ep||pins.typeName!="ASIO"||
           !matchesSessionDevice(pins)||!nativeBusOutputObserved())
            return "BUS OUTPUT: stale or unobserved native mapping";
        if(nativeAccepted_.graph.request.monitorLatencyPolicy!=armix::mixer::MonitorLatencyPolicy::aligned)
            return "BUS OUTPUT: restore Aligned MONITOR timing before changing physical pairs";
        if(wanted[0]<0||wanted[1]<0||wanted[0]>=pins.outputNames.size()||
           wanted[1]>=pins.outputNames.size()||leftName.isEmpty()||rightName.isEmpty()||
           armix::device::uniqueNamedPin(pins.outputNames,leftName)!=wanted[0]||
           armix::device::uniqueNamedPin(pins.outputNames,rightName)!=wanted[1])
            return "BUS OUTPUT: physical pin identity changed";
        const auto owner=monitor?nativeAccepted_.graph.ids.monitor:nativeAccepted_.graph.ids.main;
        if(logicalOutputs_.assignedPort(owner.value))
            return "BUS OUTPUT: bus owns a logical output; edit its logical reference";
        if(logicalOutputPairReserved(pins.deviceName,wanted[0],wanted[1]))
            return "BUS OUTPUT: pair reserved by logical output";
        const auto& hw=nativeAccepted_.hardware;
        if(hw.secondaryMain())return "BUS OUTPUT: use explicit DMX mode change before repatching";
        if(!hw.valid()||hw.activeOutputChannels!=pins.outputCount)
            return "BUS OUTPUT: installed hardware unavailable";
        std::vector<int> reserved;
        reserved.reserve(externalOutputAssignments_.size()*2);
        // Preserve even saved/disarmed EXTERNAL intentions on the same
        // driver. No implicit unpatch or amplifier reassignment.
        for(const auto& p:externalOutputAssignments_){
            if(p.playbackDevice!=pins.deviceName)continue;
            reserved.push_back(p.left);reserved.push_back(p.right);
        }
        const auto plan=OutputBusPatch::plan(
            {pins.outputByCallback.data(),static_cast<std::size_t>(pins.outputCount)},
            {hw.mainL,hw.mainR},{hw.monitorL,hw.monitorR},monitor,expected,wanted,reserved);
        if(!plan)return "BUS OUTPUT: stale, inactive or reserved stereo pair";
        auto next=hw;
        next.mainL=plan->main[0];next.mainR=plan->main[1];
        next.monitorL=plan->monitor[0];next.monitorR=plan->monitor[1];
        if(!next.valid()||!matchesSessionDevice(pins))
            return "BUS OUTPUT: device changed during validation";
        nativeAccepted_.hardware=next;
        nativeControlDirty_=true;
        busPatchEpoch_=ep;
        installNativeHardware(ep,next,nativeAccepted_.graph.request.maxFrames);
        return {};
    }
    bool nativeBusOutputObserved() const noexcept {
        return nativeMeterEpoch_.load(std::memory_order_acquire)==epoch()&&
            nativeMeterRevision_.load(std::memory_order_acquire)==nativeAccepted_.pcmRevision&&
            nativeHardwareObserved_.load(std::memory_order_acquire)==
            nativeHardware_.load(std::memory_order_acquire);
    }
    // AUDIO readback of the completed outer callback, never CONTROL roster.
    std::uint64_t renderedNativeRevision() const noexcept {
        return nativeMeterEpoch_.load(std::memory_order_acquire)==epoch()?
            nativeMeterRevision_.load(std::memory_order_acquire):0;
    }
    // Session-local timing policy. No processor preparation, no routing or
    // physical pin changes. Retain exact FX leases and all fan-in PDC.
    juce::String setMonitorTimingPolicy(std::uint64_t ep,std::uint64_t rev,
            std::uint64_t rosterRev,armix::mixer::MonitorLatencyPolicy wanted,
            std::array<int,2> expectedMain,std::array<int,2> expectedMonitor,
            const armix::device::JuceActivePortSnapshot& pins,bool disarmed) {
        using namespace armix::mixer;
        if(!disarmed||!ready()||!channelIdPhysicalPcmActive()||
           !nativeAccepted_.matches(nativePublisher_,ep,rev)||!nativeAccepted_.fxOwners||
           nativeAccepted_.graph.roster.revision()!=rosterRev||
           pins.epoch!=ep||pins.typeName!="ASIO"||!matchesSessionDevice(pins))
            return "MONITOR timing: stale native graph or output armed";
        const auto& hw=nativeAccepted_.hardware;
        const auto& old=nativeAccepted_.graph.request;
        if(!hw.valid()||hw.activeOutputChannels!=pins.outputCount||
           !MonitorTimingEdit::allowed(disarmed,nativeBusOutputObserved(),hw.monitorEnabled,
              old.monitorLatencyPolicy,wanted,
              {pins.outputByCallback.data(),static_cast<std::size_t>(pins.outputCount)},
              {hw.mainL,hw.mainR},{hw.monitorL,hw.monitorR},expectedMain,expectedMonitor))
            return "MONITOR timing: distinct callback-observed MAIN/MONITOR pairs required";
        for(int pin:{expectedMain[0],expectedMain[1],expectedMonitor[0],expectedMonitor[1]}){
            if(pin<0||pin>=pins.outputNames.size()||pins.outputNames[pin].isEmpty()||
               armix::device::uniqueNamedPin(pins.outputNames,pins.outputNames[pin])!=pin)
                return "MONITOR timing: unique physical pin labels required";
        }
        try{
            auto request=old;request.monitorLatencyPolicy=wanted;
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>();
            if(DynamicChannelFxLeaseBank::reuseRevision(ep,rev+1,*nativeAccepted_.fxOwners,*owner)
                !=DynamicChannelFxLeaseBank::Fault::none)return "MONITOR timing: FX lease rejected";
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();fx.deviceEpoch=ep;fx.graphRevision=rev+1;
            fx.render=+[](void* p,ChannelId id,float* left,float* right,int n) noexcept{
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,left,right,n);
            };
            fx.latency=+[](void* p,ChannelId id) noexcept{
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            if(!matchesSessionDevice(pins)||!nativeAccepted_.matches(nativePublisher_,ep,rev))
                return "MONITOR timing: device changed";
            const auto committed=nativePublisher_.commit(nativeAccepted_.graph.roster,request,
                rev,ep,epoch(),true,std::move(fx));
            if(!committed.ok())return "MONITOR timing: PCM publication rejected";
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.fxOwners=std::move(owner);
            nativeAccepted_.pcmRevision=committed.revision;
            reapplyExternalOutputsSameEpoch();
            nativeControlDirty_=true;nativeStructureLive_=true;
            busPatchEpoch_=ep; // MAIN/STREAM must wait for callback readback
            nativeMeterEpoch_.store(0,std::memory_order_release);
            return {};
        }catch(...){return "MONITOR timing: preparation failed; previous policy retained";}
    }
    bool nativeBusPatchAwaitingCallback() const noexcept {
        return busPatchEpoch_!=0&&busPatchEpoch_==epoch()&&
            channelIdPhysicalPcmActive()&&!nativeBusOutputObserved();
    }
    // AUDIO: capture the destination before the outer output-arm decision.
    // A block with an old arm decision must never read a newly assigned pair.
    struct NativeOutputLease {std::uint64_t epoch=0,packed=0;};
    NativeOutputLease captureNativeOutputLease() const noexcept {
        const auto ep=channelIdPcmEpoch_.load(std::memory_order_acquire);
        const auto packed=nativeHardware_.load(std::memory_order_acquire);
        return ep!=0&&ep==channelIdPcmEpoch_.load(std::memory_order_acquire)
            ?NativeOutputLease{ep,packed}:NativeOutputLease{};
    }

    bool describeRetainedAsioSession(armix::session::MixerSessionDocument& document) const {
        if(!hasRetainedNativeSession() ||
           !nativeAccepted_.matches(nativePublisher_,nativeAccepted_.deviceEpoch,
                                    nativeAccepted_.pcmRevision))return false;
        document.roster=nativeAccepted_.graph.roster;
        document.request=nativeAccepted_.graph.request;
        document.legacyIds=nativeAccepted_.graph.ids;
        document.outputs=nativeAccepted_.hardware;
        document.waitingAsioInputs=waitingAsioInputs_;
        document.logicalInputs=logicalInputs_;
        armix::session::LogicalInputProjection::strip(document);
        document.externalOutputs=externalOutputAssignments_;
        document.logicalOutputs=logicalOutputs_;
        armix::session::LogicalOutputProjection::strip(document);
        return true;
    }

    bool rebindRetainedAsioSession(armix::session::MixerSessionDocument document,
            const armix::device::JuceActivePortSnapshot& pins,int deviceFrames) {
        const auto next=nativeGraphRevisionForRestore();
        const auto previous=document.request.maxFrames;
        // Existing processors can still use their prepared quantum. Larger
        // device blocks are split by the same render path used before reset.
        document.request.maxFrames=armix::device::retainedMixerQuantum(previous,
            deviceFrames,Block::hardFrameLimit,false,true);
        if(!next || !document.request.maxFrames || !hasRetainedNativeSession())return false;
        auto bank=std::make_shared<armix::mixer::DynamicChannelFxLeaseBank>();
        if(armix::mixer::DynamicChannelFxLeaseBank::rebindSameFormat(
               pins.epoch,next,*nativeAccepted_.fxOwners,*bank)!=
           armix::mixer::DynamicChannelFxLeaseBank::Fault::none)return false;
        return restoreNativeMixerSession(std::move(document),std::move(bank),pins,true).isEmpty();
    }

    // Match CONTROL's live device readback to the masks registered for the
    // actual callback epoch. Neither a saved document nor an M4 graph
    // attests which physical pins the current callback can address.
    bool matchesSessionDevice(const armix::device::JuceActivePortSnapshot& pins) const noexcept {
        if(!registryValid_.load(std::memory_order_acquire) ||
           topologyFault_.load(std::memory_order_acquire) ||
           !pins.matchesCallback(epoch(),callbackInputCount_.load(std::memory_order_acquire),
                                  callbackOutputCount_.load(std::memory_order_acquire)))
            return false;
        for(int i=0;i<pins.inputs.enabledCount;++i)
            if(pins.inputs.physicalIndex(i)!=enabledPorts_[i].load(std::memory_order_relaxed))
                return false;
        for(int i=0;i<pins.outputCount;++i)
            if(pins.outputPhysicalPort(i)!=enabledOutputPorts_[i].load(std::memory_order_relaxed))
                return false;
        return pins.epoch==epoch() && registryValid_.load(std::memory_order_acquire);
    }

    // The saved roster and freshly prepared FX publish through the same
    // ChannelId owner used by ordinary native edits.
    juce::String restoreNativeMixerSession(
            armix::session::MixerSessionDocument document,
            std::shared_ptr<armix::mixer::DynamicChannelFxLeaseBank> bank,
            const armix::device::JuceActivePortSnapshot& pins,
            bool outputsDisarmed,bool replaceActive=false) {
        using namespace armix::mixer;
        if(!outputsDisarmed || (!replaceActive && channelIdPhysicalPcmActive()) || !bank)
            return "Restore requires a muted device";
        const auto generation=pins.epoch;
        if(!matchesSessionDevice(pins) || bank->epoch()!=generation ||
           document.request.physicalInputs!=pins.inputs.enabledCount ||
           document.outputs.activeOutputChannels!=pins.outputCount ||
           !document.outputs.valid())
            return "NEEDS_REMAP: verified callback layout changed";
        if(document.outputs.secondaryMain()){
            const auto* monitor=document.roster.find(document.legacyIds.monitor);
            if(pins.typeName!="ASIO"||document.binding.backend!="ASIO"||
               pins.deviceName!=document.binding.playback||
               pins.outputNames!=document.binding.outputNames||
               !monitor||monitor->role!=ChannelRole::monitor)
                return "NEEDS_REMAP: split MAIN/MONITOR requires exact named ASIO binding";
        }
        if(document.request.maxFrames<32 || document.request.maxFrames>Block::hardFrameLimit)
            return "Invalid native mixer quantum";
        try {
            // Independent timing is a session-only operator decision.
            // Show recall / retained ASIO remap cannot silently inherit it.
            document.request.monitorLatencyPolicy=MonitorLatencyPolicy::aligned;
            // The outer device binding policy and native snapshot attest
            // the exact current layout. Resolve logical inputs before the
            // single existing native graph publication, never on AUDIO.
            if(!document.logicalInputs.empty()){
                using R=armix::device::LogicalInputRegistry;
                std::vector<R::Pin> inputPins;
                inputPins.reserve(static_cast<std::size_t>(pins.inputs.enabledCount));
                for(int i=0;i<pins.inputs.enabledCount;++i){
                    const auto physical=pins.inputs.physicalIndex(i);
                    auto name=document.binding.inputNames[physical];
                    if(armix::device::uniqueNamedPin(document.binding.inputNames,name)!=physical)name.clear();
                    inputPins.push_back({physical,i,name.toStdString()});
                }
                const R::Inventory live{{document.binding.backend.toStdString(),
                    document.binding.capture.toStdString(),document.binding.playback.toStdString()},
                    generation,inputPins,true};
                if(!armix::session::LogicalInputProjection::prepare(document,live))
                    return "NEEDS_REMAP: conflicting logical input references";
            }
            if(!document.logicalOutputs.empty()){
                using R=armix::device::LogicalOutputRegistry;
                std::vector<R::Pin> outputPins;
                outputPins.reserve(static_cast<std::size_t>(pins.outputCount));
                for(int i=0;i<pins.outputCount;++i){
                    const int physical=pins.outputPhysicalPort(i);
                    auto name=pins.outputNames[physical];
                    if(armix::device::uniqueNamedPin(pins.outputNames,name)!=physical)name.clear();
                    outputPins.push_back({physical,i,name.toStdString()});
                }
                const R::Inventory live{{pins.typeName.toStdString(),pins.deviceName.toStdString()},
                    generation,outputPins,{},true};
                if(!armix::session::LogicalOutputProjection::prepare(document,live))
                    return "NEEDS_REMAP: logical MAIN/MONITOR or conflicting output references";
            }
            document.request.maxScratchBytes=32U*1024U*1024U;
            const auto previous=nativePublisher_.acceptedRevision();
            bank->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=std::static_pointer_cast<void>(bank);
            fx.context=bank.get();
            fx.deviceEpoch=generation;fx.graphRevision=previous+1;
            fx.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(p))(id,l,r,n);
            };
            fx.latency=+[](void* p,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            // Resolve durable external intentions before publishing the new
            // graph. No per-channel post-publication target updates on restore.
            std::vector<DynamicNativeSessionPublisher::InitialExternalTarget> externalTargets;
            std::vector<std::uint64_t> verifiedExternal;
            const auto reserved=document.outputs.sinkPlan().reserved();
            if(pins.typeName=="ASIO"&&pins.deviceName==document.binding.playback){
                const std::span<const int> callbackPins(pins.outputByCallback.data(),
                    static_cast<std::size_t>(pins.outputCount));
                externalTargets.reserve(document.externalOutputs.size());
                verifiedExternal.reserve(document.externalOutputs.size());
                for(const auto& patch:document.externalOutputs){
                    if(!armix::session::MixerDeviceBindingPolicy::canRestoreExternalOutput(
                        patch,pins.deviceName,callbackPins,pins.outputNames,document.outputs))continue;
                    int left=-1,right=-1;
                    for(int i=0;i<pins.outputCount;++i){
                        if(pins.outputPhysicalPort(i)==patch.left)left=i;
                        if(pins.outputPhysicalPort(i)==patch.right)right=i;
                    }
                    externalTargets.push_back({patch.channel,left,right});
                    verifiedExternal.push_back(patch.channel.value);
                }
            }
            const DynamicNativeSessionPublisher::InitialExternalPlan externalPlan{
                pins.outputCount,{reserved.data(),document.outputs.sinkPlan().reservedCount()},externalTargets};
            const auto published=nativePublisher_.commit(
                document.roster,document.request,previous,generation,epoch(),
                true,std::move(fx),false,false,nullptr,&externalPlan);
            if(!published.ok())
                return "Native ChannelId PCM rejected persisted topology: "+
                    juce::String(static_cast<int>(published.fault));
            nativeAccepted_.graph.roster=std::move(document.roster);
            nativeAccepted_.graph.request=std::move(document.request);
            nativeAccepted_.graph.ids=document.legacyIds;
            waitingAsioInputs_=std::move(document.waitingAsioInputs);
            logicalInputs_=std::move(document.logicalInputs);
            logicalOutputs_=std::move(document.logicalOutputs);
            ++logicalOutputRevision_;++logicalInputRevision_;
            nativeAccepted_.graph.revision=nativeAccepted_.graph.roster.revision();
            nativeAccepted_.hardware=document.outputs;
            // Restored physical intentions are inert until exact ASIO identity,
            // both labeled pins and current MAIN/MONITOR ownership match.
            externalOutputAssignments_=std::move(document.externalOutputs);
            externalOutputVerified_=std::move(verifiedExternal);
            externalOutputEpoch_=externalOutputVerified_.empty()?0:generation;
            nativeAccepted_.fxOwners=std::move(bank);
            nativeAccepted_.deviceEpoch=generation;
            nativeAccepted_.pcmRevision=published.revision;
            nativeStructureLive_=true;
            nativeControlDirty_=true;
            publishedEpoch_.store(0,std::memory_order_release);
            publisher_.unpublish();
            nativeMeterEpoch_.store(0,std::memory_order_release);
            // Only the logical source patch is restored. Physical OUT remains
            // disarmed until the user independently enables monitoring.
            installNativeHardware(generation,nativeAccepted_.hardware,
                                  nativeAccepted_.graph.request.maxFrames);
            return channelIdPhysicalPcmActive()?juce::String{}:
                juce::String("NEEDS_REMAP: device changed during native publication");
        }catch(...) {return "Native session restore exception";}
    }

    bool matchesNativeMixerOutputs(const armix::mixer::VerifiedNativeOutputs& outputs) const noexcept {
        return channelIdPhysicalPcmActive() &&
            armix::session::MixerDeviceBindingPolicy::sameOutputs(nativeAccepted_.hardware,outputs);
    }

    std::uint64_t nativeGraphRevisionForRestore() const noexcept {
        const auto current=nativePublisher_.acceptedRevision();
        return current==std::numeric_limits<std::uint64_t>::max()?0:current+1;
    }

    juce::var nativeBusStatus(bool retained=false) const {
        if(!nativeAccepted_.fxOwners ||
           (!retained && !channelIdPhysicalPcmActive()))return {};
        const auto* routing=nativePublisher_.acceptedRouting(
            nativeAccepted_.deviceEpoch,nativeAccepted_.pcmRevision);
        if(!routing)return {};
        auto state=std::make_unique<juce::DynamicObject>();
        state->setProperty("graphRevision",juce::String(
            std::to_string(nativeAccepted_.pcmRevision)));
        state->setProperty("rosterRevision",juce::String(
            std::to_string(routing->rosterRevision)));
        state->setProperty("structural",nativeStructureLive_);
        if(!retained)
            state->setProperty("latencySamples",nativePublisher_.latencySamples(epoch(),nativeAccepted_.pcmRevision));
        juce::Array<juce::var> allowed;
        // Only expose operations backed by a real DSP lease. The Add FX
        // Return workflow prepares its wet-only owner before publication.
        if(!retained)
          for(const char* op: {"NATIVE_CREATE_GROUP_SELECTED",
                             "NATIVE_REMOVE_BUS","NATIVE_SET_BUS_MIX",
                             "NATIVE_CREATE_CHANNEL","NATIVE_DELETE_CHANNEL","NATIVE_PATCH_CHANNEL",
                             "NATIVE_SET_PRIMARY_OUT",
                             "NATIVE_APPLY_WIRE_TRANSACTION",
                             "NATIVE_SET_WIRE_GAIN",
                             "NATIVE_SET_SEND_ENABLED",
                             "NATIVE_SET_PROCESSING_FORMAT",
                             "NATIVE_SET_CHANNEL_MIX",
                             "NATIVE_SET_INPUT_GAIN",
                             "NATIVE_SET_WIRE_SUM_GAIN",
                             "NATIVE_SET_CHANNEL_MUTE_SOLO",
                             "NATIVE_SET_BUS_MUTE_SOLO",
                             "NATIVE_LOAD_INSERT","NATIVE_REMOVE_INSERT","NATIVE_MOVE_INSERT","NATIVE_CONVERT_REVERB_RETURN",
                             "NATIVE_SET_INSERT_BYPASS",
                             "NATIVE_OPEN_INSERT_EDITOR"})
            allowed.add(juce::var(juce::String(op)));
        state->setProperty("supportedOps",juce::var(allowed));
        if(!retained){
            auto logical=std::make_unique<juce::DynamicObject>();
            logical->setProperty("revision",juce::String(std::to_string(logicalInputRevision_)));
            juce::Array<juce::var> ports,refs;
            for(const auto& p:logicalInputs_.ports()){
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("id",juce::String(std::to_string(p.id)));
                row->setProperty("name",juce::String(p.name));
                row->setProperty("format",p.format==armix::device::LogicalInputRegistry::Format::mono?"MONO":"STEREO");
                row->setProperty("bound",p.binding.has_value());
                if(p.binding){
                    row->setProperty("left",p.binding->left);row->setProperty("right",p.binding->right);
                    row->setProperty("leftName",juce::String(p.binding->leftName));
                    row->setProperty("rightName",juce::String(p.binding->rightName));
                    row->setProperty("capture",juce::String(p.binding->device.capture));
                    row->setProperty("backend",juce::String(p.binding->device.backend));
                    row->setProperty("playback",juce::String(p.binding->device.playback));
                }
                ports.add(juce::var(row.release()));
            }
            for(const auto& ref:logicalInputs_.references()){
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("channelId",juce::String(std::to_string(ref.channel)));
                row->setProperty("portId",juce::String(std::to_string(ref.port)));
                const bool installed=std::any_of(nativeAccepted_.graph.request.sources.begin(),
                    nativeAccepted_.graph.request.sources.end(),[&](const auto& p){return p.id.value==ref.channel;});
                row->setProperty("state",installed?"RESOLVED":"NEEDS_REMAP");
                refs.add(juce::var(row.release()));
            }
            logical->setProperty("ports",juce::var(ports));logical->setProperty("references",juce::var(refs));
            state->setProperty("logicalInputRegistry",juce::var(logical.release()));
            using R=armix::device::LogicalOutputRegistry;
            auto output=std::make_unique<juce::DynamicObject>();
            output->setProperty("revision",juce::String(std::to_string(logicalOutputRevision_)));
            juce::Array<juce::var> outputPorts,outputRefs;
            for(const auto& p:logicalOutputs_.ports()){
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("id",juce::String(std::to_string(p.id)));
                row->setProperty("name",juce::String(p.name));row->setProperty("format","STEREO");
                row->setProperty("bound",p.binding.has_value());
                if(p.binding){
                    row->setProperty("backend",juce::String(p.binding->device.backend));
                    row->setProperty("playback",juce::String(p.binding->device.playback));
                    row->setProperty("left",p.binding->left);row->setProperty("right",p.binding->right);
                    row->setProperty("leftName",juce::String(p.binding->leftName));
                    row->setProperty("rightName",juce::String(p.binding->rightName));
                }
                outputPorts.add(juce::var(row.release()));
            }
            for(const auto& ref:logicalOutputs_.references()){
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("channelId",juce::String(std::to_string(ref.channel)));
                row->setProperty("portId",juce::String(std::to_string(ref.port)));
                row->setProperty("role",ref.role==R::Role::main?"MAIN":ref.role==R::Role::monitor?"MONITOR":"EXTERNAL");
                const char* status="NEEDS_REMAP";
                if(ref.role==R::Role::external){
                    const auto target=nativePublisher_.externalOutput(nativeAccepted_.deviceEpoch,nativeAccepted_.pcmRevision,{ref.channel});
                    const auto applied=nativePublisher_.appliedExternalOutput(nativeAccepted_.deviceEpoch,nativeAccepted_.pcmRevision,{ref.channel});
                    if(target.first>=0)status=target==applied?"RESOLVED":"PENDING";
                }else status=nativeBusOutputObserved()?"RESOLVED":"PENDING";
                row->setProperty("state",status);outputRefs.add(juce::var(row.release()));
            }
            output->setProperty("ports",juce::var(outputPorts));output->setProperty("references",juce::var(outputRefs));
            state->setProperty("logicalOutputRegistry",juce::var(output.release()));
        }
        juce::Array<juce::var> rows;
        // Meter taps come from the SAME realtime ChannelId renderer, in
        // accepted roster order. No JS guesses or second DSP render.
        const bool pcmMeterValid=!retained &&
            nativeMeterEpoch_.load(std::memory_order_acquire)==epoch()&&
            nativeMeterRevision_.load(std::memory_order_acquire)==nativeAccepted_.pcmRevision;
        // Zero-filled placeholders are not measured silence. Only the
        // callback-observed graph revision authorizes operator diagnosis.
        if(pcmMeterValid)state->setProperty("meterGraphRevision",
            juce::String(std::to_string(nativeAccepted_.pcmRevision)));
        std::size_t meterIndex=0;
        for(const auto& ch:routing->channels) {
            auto row=std::make_unique<juce::DynamicObject>();
            row->setProperty("id",juce::String(std::to_string(ch.id.value)));
            const char* role=ch.role==armix::mixer::ChannelRole::input?"INPUT":
                             ch.role==armix::mixer::ChannelRole::group?"GROUP":
                             ch.role==armix::mixer::ChannelRole::fxReturn?"FX_RETURN":
                             ch.role==armix::mixer::ChannelRole::main?"MAIN":"MONITOR";
            row->setProperty("role",role);
            // Only E3-created BUSes can be edited here. The original AUX
            // 1–4 have FX_RETURN roles too, but are still M4-owned lanes.
            row->setProperty("nativeManaged",
                (ch.role==armix::mixer::ChannelRole::group ||
                 ch.role==armix::mixer::ChannelRole::fxReturn) &&
                !originalM4Aux(ch.id));
            row->setProperty("name",juce::String(ch.name));
            row->setProperty("format",ch.format==armix::mixer::ProcessingFormat::stereo
                ?"STEREO":"MONO");
            if(const auto* accepted=nativeAccepted_.graph.roster.find(ch.id)) {
                row->setProperty("fader",static_cast<double>(accepted->faderLinear));
                row->setProperty("inputGain",static_cast<double>(accepted->inputGainLinear));
                row->setProperty("wireSumGain",static_cast<double>(accepted->wireSumGainLinear));
                row->setProperty("pan",static_cast<double>(accepted->pan));
                row->setProperty("mute",accepted->mute);
                row->setProperty("solo",accepted->solo);
            }
            row->setProperty("primaryOutput",
                juce::String(std::to_string(ch.primaryOutput.value)));
            juce::Array<juce::var> liveInserts;
            const auto fxCount=nativeAccepted_.fxOwners->insertCount(ch.id);
            for(std::size_t slot=0;slot<fxCount;++slot) {
                const auto* lease=nativeAccepted_.fxOwners->ownerForControl(
                    ch.id,slot);
                if(!lease||!*lease)continue;
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("instanceId",(*lease)->instanceId);
                item->setProperty("id",(*lease)->catalogId);
                item->setProperty("name",(*lease)->name);
                item->setProperty("latencySamples",(*lease)->latencySamples);
                item->setProperty("latencyChanged",(*lease)->latencyChanged.load(std::memory_order_acquire));
                const auto timing=(*lease)->processTiming.snapshot();
                item->setProperty("processTimingSamples",static_cast<juce::int64>(timing.samples));
                item->setProperty("processTimingFrames",static_cast<juce::int64>(timing.frames));
                item->setProperty("processTimingMeanUs",timing.samples?double(timing.totalNs)/timing.samples/1000.:-1.);
                item->setProperty("processTimingMaxUs",timing.samples?double(timing.maxNs)/1000.:-1.);
                item->setProperty("processTimingLastUs",timing.samples?double(timing.lastNs)/1000.:-1.);
                item->setProperty("processTimingFullyBypassedSamples",static_cast<juce::int64>(timing.fullyBypassed));
                const auto workload=(*lease)->workloadEvidence.snapshot();
                item->setProperty("workloadBudgetValid",!retained&&workload.validRate&&workload.calls>0);
                item->setProperty("workloadObservedCalls",static_cast<double>(workload.calls));
                item->setProperty("workloadOverBudgetCalls",static_cast<double>(workload.overBudget));
                item->setProperty("workloadWorstPermille",static_cast<double>(workload.worstPermille));
                item->setProperty("workloadSilenceObserved",static_cast<double>(workload.silenceObserved));
                item->setProperty("workloadSilentInOut",static_cast<double>(workload.silentInOut));
                item->setProperty("workloadWindowValid",!retained&&workload.validRate&&workload.lastWindow.valid);
                item->setProperty("workloadWindowMeanPermille",static_cast<int>(workload.lastWindow.meanPermille));
                item->setProperty("workloadWindowPeakPermille",static_cast<int>(workload.lastWindow.peakPermille));
                item->setProperty("workloadWindowOverBudget",static_cast<int>(workload.lastWindow.overBudget));
                item->setProperty("processContentionCount",static_cast<juce::int64>((*lease)->contention.load(std::memory_order_relaxed)));
                item->setProperty("processExceptionCount",static_cast<juce::int64>((*lease)->exceptions.load(std::memory_order_relaxed)));
                // CONTROL readback of the exact admitted ChannelId/instance.
                // Retained unbound rows must not claim a live processing state.
                item->setProperty("dspTelemetryLive",!retained);
                item->setProperty("dspIdleSupported",(*lease)->immerseIdleAdmitted||(*lease)->nativeExactIdleAdmitted);
                item->setProperty("dspIdlePolicy",(*lease)->nativeExactIdleAdmitted?"NATIVE_EXACT_STATE":
                    (*lease)->immerseIdleAdmitted?"IMMERSIB_QUALIFIED_TAIL":"CONSERVATIVE");
                item->setProperty("dspSleeping",!retained && (*lease)->dspSleeping.load(std::memory_order_relaxed));
                item->setProperty("dspLastUs",retained?juce::var():juce::var(static_cast<double>((*lease)->dspLastUs.load(std::memory_order_relaxed))));
                item->setProperty("dspWorstUs",static_cast<double>((*lease)->dspWorstUs.load(std::memory_order_relaxed)));
                item->setProperty("dspProcessedBlocks",static_cast<double>((*lease)->dspProcessedBlocks.load(std::memory_order_relaxed)));
                item->setProperty("dspSleepBlocks",static_cast<double>((*lease)->dspSleepBlocks.load(std::memory_order_relaxed)));
                item->setProperty("dspParkedBypassBlocks",static_cast<double>((*lease)->dspParkedBypassBlocks.load(std::memory_order_relaxed)));
                // Independently sampled lifetime totals, not an atomic snapshot.
                item->setProperty("dspTotalUs",static_cast<double>((*lease)->dspTotalUs.load(std::memory_order_relaxed)));
                item->setProperty("dspProcessedFrames",static_cast<double>((*lease)->dspProcessedFrames.load(std::memory_order_relaxed)));
                item->setProperty("dspSleepFrames",static_cast<double>((*lease)->dspSleepFrames.load(std::memory_order_relaxed)));
                item->setProperty("bypassed",(*lease)->bypassRequested.load(
                    std::memory_order_relaxed));
                liveInserts.add(juce::var(item.release()));
            }
            row->setProperty("inserts",juce::var(liveInserts));
            row->setProperty("fxRackBypassed",
                nativeAccepted_.fxOwners->rackBypassed(ch.id));
            if(pcmMeterValid && meterIndex<nativeMeterCapacity) {
                row->setProperty("peakL",static_cast<double>(
                    nativeMeterPeaksL_[meterIndex].load(std::memory_order_relaxed)));
                row->setProperty("peakR",static_cast<double>(
                    nativeMeterPeaksR_[meterIndex].load(std::memory_order_relaxed)));
            } else {
                row->setProperty("peakL",0.);
                row->setProperty("peakR",0.);
            }
            ++meterIndex;
            // Actual committed physical patches. Expose the verified original
            // physical pin, not the reordered callback ordinal.
            for(const auto& source:nativeAccepted_.graph.request.sources) {
                if(source.id!=ch.id)continue;
                auto physical=std::make_unique<juce::DynamicObject>();
                if(retained)physical->setProperty("bindingState","NEEDS_REMAP");
                if(!retained && source.inputL>=0 &&
                   source.inputL<portCount_.load(std::memory_order_acquire)) {
                    const auto pin=enabledPorts_[source.inputL].load(std::memory_order_relaxed);
                    physical->setProperty("portId","in:"+juce::String(pin));
                }
                // Preserve BOTH actual physical pins on native readback, not
                // just the left pin. The GUI may not invent the right channel.
                if(!retained && source.stereo && source.inputR>=0 &&
                   source.inputR<portCount_.load(std::memory_order_acquire)) {
                    const auto right=enabledPorts_[source.inputR].load(std::memory_order_relaxed);
                    physical->setProperty("portIdRight","in:"+juce::String(right));
                }
                if(source.kind==armix::mixer::DynamicSourceKind::playerDeck) {
                    // Never mislabel an internal PCM decoder as physical
                    // STEREO with no pin. The browser must verify deck ID.
                    physical->setProperty("sourceFormat","PLAYER");
                    physical->setProperty("playerDeckId",
                        juce::String(std::to_string(source.deckId)));
                } else physical->setProperty(
                    "sourceFormat",source.stereo?"STEREO":"MONO");
                row->setProperty("source",juce::var(physical.release()));
                break;
            }
            if(const auto port=logicalInputs_.assignedPort(ch.id.value)){
                row->setProperty("logicalInputPortId",juce::String(std::to_string(port)));
                const auto* logical=logicalInputs_.find(port);
                if(logical)row->setProperty("logicalInputName",juce::String(logical->name));
                const bool installed=!retained&&std::any_of(nativeAccepted_.graph.request.sources.begin(),
                    nativeAccepted_.graph.request.sources.end(),[&](const auto& p){return p.id==ch.id;});
                row->setProperty("logicalInputState",installed?"RESOLVED":"NEEDS_REMAP");
            }
            juce::Array<juce::var> sends;
            for(const auto& send:ch.parallelSends) {
                auto edge=std::make_unique<juce::DynamicObject>();
                edge->setProperty("target",juce::String(
                    std::to_string(send.destination.value)));
                edge->setProperty("preFader",send.preFader);
                edge->setProperty("gain",static_cast<double>(send.gain));
                edge->setProperty("enabled",send.enabled);
                edge->setProperty("pan",static_cast<double>(send.pan));
                sends.add(juce::var(edge.release()));
            }
            row->setProperty("parallelSends",juce::var(sends));
            rows.add(juce::var(row.release()));
        }
        state->setProperty("channels",juce::var(rows));
        juce::Array<juce::var> edges;
        for(const auto& accepted:routing->edges) {
            auto edge=std::make_unique<juce::DynamicObject>();
            edge->setProperty("edgeId",juce::String(
                std::to_string(accepted.edgeId)));
            edge->setProperty("from",juce::String(
                std::to_string(accepted.from.value)));
            edge->setProperty("to",juce::String(
                std::to_string(accepted.to.value)));
            edge->setProperty("gainLinear",
                static_cast<double>(accepted.gainLinear));
            edge->setProperty("tap",accepted.preFader?"PRE_FADER":"POST_FADER");
            edge->setProperty("primaryOutput",accepted.primaryOutput);
            edge->setProperty("enabled",accepted.enabled);
            edges.add(juce::var(edge.release()));
        }
        state->setProperty("edges",juce::var(edges));
        return juce::var(state.release());
    }


    void revokeChannelIdPcm() noexcept {
        channelIdPcmEpoch_.store(0,std::memory_order_release);
        // Neither a structural BUS nor a native-owned fader/mute/pan may
        // silently resume on the out-of-date legacy M4 PCM renderer.
        if(nativeStructureLive_ || nativeControlDirty_)
            topologyFault_.store(true,std::memory_order_release);
    }
    void rejectedNativeMirror() noexcept {
        // A failed AUX/mirror after any native-owned fader gesture must not
        // reactivate stale legacy M4 controls on the original channels.
        if(nativeStructureLive_ || nativeControlDirty_)
            topologyFault_.store(true,std::memory_order_release);
        else channelIdPcmEpoch_.store(0,std::memory_order_release);
    }
    // Native PCM identity is not the M4 compatibility control revision.
    // A pristine ChannelId starter can be admitted before its first edit.
    bool nativeTopologyMatches(std::uint64_t ep,std::uint64_t rev) const noexcept {
        return ready()&&channelIdPhysicalPcmActive()&&
            nativeAccepted_.matches(nativePublisher_,ep,rev);
    }
    std::uint64_t sessionLiveControlRevision() const noexcept {
        return nativeAccepted_.graph.roster.liveStateRevision();
    }
    std::uint64_t controlRevision() const noexcept {
        return channelIdPhysicalPcmActive() && nativeStructureLive_
            ?nativeAccepted_.pcmRevision:revision();
    }
    // The first accepted SOURCE NONE ChannelId graph is a disposable
    // bootstrap topology, not an authored Show. A persisted .smix may replace
    // it once on cold start; any accepted FX, control, WIRE, BUS or source
    // mutation immediately makes that replacement unsafe.
    // CONTROL THREAD ONLY: hasAuthoredMixerContent traverses roster/FX.
    bool canReplacePristineStartupMixer() const noexcept {
        return channelIdPhysicalPcmActive() && sourceTemplate_.isObject() &&
               !acceptedRoute_.isObject() && !nativeStructureLive_ &&
               !nativeControlDirty_ && nativeAccepted_.fxOwners &&
               nativeAccepted_.graph.request.sources.empty() &&
               !hasAuthoredMixerContent();
    }
    std::span<const armix::session::WaitingAsioInput> waitingAsioInputs() const noexcept {return waitingAsioInputs_;}
    int waitingAsioInputCount() const noexcept {return static_cast<int>(waitingAsioInputs_.size());}
    bool hasVerifiedNativeSources() const noexcept {
        return channelIdPhysicalPcmActive() && !nativeAccepted_.graph.request.sources.empty();
    }
    bool channelIdPhysicalPcmActive() const noexcept {
        const auto active=channelIdPcmEpoch_.load(std::memory_order_acquire);
        return active!=0 && active==epoch() && registryValid_.load(std::memory_order_acquire) &&
               !topologyFault_.load(std::memory_order_acquire);
    }

    void disable() noexcept {
        channelIdPcmEpoch_.store(0,std::memory_order_release);
        nativeMeterEpoch_.store(0,std::memory_order_release);
        nativeStructureLive_=false;
        nativeControlDirty_=false;
        nativePublisher_.unpublish(); // CONTROL-only; audio lease retires safely
        nativeAccepted_=armix::mixer::UnifiedLegacyAccepted{};
        waitingAsioInputs_.clear();
        logicalInputs_={};
        logicalOutputs_={};
        externalOutputAssignments_.clear();
        externalOutputVerified_.clear();
        externalOutputEpoch_=0;
        logicalInputRevision_=1;logicalOutputRevision_=1;
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
    void poll() noexcept {
        publisher_.pollRetirement();
        nativePublisher_.pollRetirement();
    } // message thread only, reclaim both graph generations
    bool active() const noexcept {return enabled_.load(std::memory_order_acquire);}
    float peakLeft() const noexcept {return peakL_.load(std::memory_order_relaxed);}
    float peakRight() const noexcept {return peakR_.load(std::memory_order_relaxed);}
    // CONTROL ONLY: hardware loss makes ready() false but does not erase
    // authored values, FX identity, GROUP/FX_RETURN buses or accepted WIREs.
    // Never downgrade this dynamic document to the fixed 10CH/4AUX schema.
    std::uint64_t retainedNativeEpoch() const noexcept {
        return hasRetainedNativeSession()?nativeAccepted_.deviceEpoch:0;
    }
    bool hasRetainedNativeSession() const noexcept {
        return !channelIdPhysicalPcmActive() && nativeAccepted_.fxOwners && hasAuthoredMixerContent();
    }
    bool hasAuthoredMixerContent() const noexcept {
        if(acceptedRoute_.isObject() || nativeStructureLive_ || nativeControlDirty_)
            return true;
        for(const auto& lane:acceptedFxSelections_)
            for(const auto& fx:lane)
                if(fx.id.isNotEmpty())return true;
        if(nativeAccepted_.fxOwners)
            for(const auto& channel:nativeAccepted_.graph.roster.channels())
                if(nativeAccepted_.fxOwners->insertCount(channel.id)>0)
                    return true;
        return false;
    }
    // A pristine SOURCE NONE mixer must not disappear when an ASIO callback
    // stops/restarts. It contains no authored FX, WIRE, source or fader edit;
    // rebuilding it is safe only on CONTROL after verifying the same device.
    bool hasPristineSilentMixerAwaitingDevice() const noexcept {
        return active() && sourceTemplate_.isObject() &&
            !acceptedRoute_.isObject() && !nativeStructureLive_ &&
            !nativeControlDirty_ && !channelIdPhysicalPcmActive() &&
            !hasAuthoredMixerContent();
    }
    // CONTROL only. The verified profile proves identical port ordinals.
    // Rebuild DSP scratch/PDC while retaining the accepted document and
    // compatible processors. Never re-use a processor at a different rate.
    bool rebindVerifiedDevice(std::uint64_t priorEpoch,std::uint64_t newEpoch,
                              int deviceFrames,bool rateChanged) {
        using namespace armix::mixer;
        if(!priorEpoch || newEpoch<=priorEpoch || newEpoch!=epoch() ||
           !active() || !registryValid_.load(std::memory_order_acquire) ||
           topologyFault_.load(std::memory_order_acquire))return false;
        if(nativeAccepted_.matches(nativePublisher_,priorEpoch,nativeAccepted_.pcmRevision)) {
            const auto revision=nativeAccepted_.pcmRevision;
            const auto& hardware=nativeAccepted_.hardware;
            if(nativeAccepted_.graph.request.physicalInputs!=
                   callbackInputCount_.load(std::memory_order_acquire) ||
               hardware.activeOutputChannels!=callbackOutputCount_.load(std::memory_order_acquire))
                return false;
            bool hasEffects=nativeAccepted_.fxOwners->externalChannel().value!=0;
            for(const auto& row:nativeAccepted_.graph.roster.channels())
                if(nativeAccepted_.fxOwners->insertCount(row.id)>0){hasEffects=true;break;}
            auto request=nativeAccepted_.graph.request;
            request.maxFrames=armix::device::retainedMixerQuantum(
                request.maxFrames,deviceFrames,Block::hardFrameLimit,rateChanged,hasEffects);
            if(request.maxFrames==0)return false;
            auto owner=std::make_shared<DynamicChannelFxLeaseBank>();
            if(DynamicChannelFxLeaseBank::rebindSameFormat(
                   newEpoch,revision+1,*nativeAccepted_.fxOwners,*owner)!=
               DynamicChannelFxLeaseBank::Fault::none)return false;
            owner->bindGlobalRackOverride(&globalFxBypassed_);
            DynamicNativeSessionPublisher::FxPlan fx;
            fx.owner=owner;fx.context=owner.get();
            fx.deviceEpoch=newEpoch;fx.graphRevision=revision+1;
            fx.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
                (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
            };
            fx.latency=+[](void* ctx,ChannelId id) noexcept {
                return static_cast<DynamicChannelFxLeaseBank*>(ctx)->latencySamples(id);
            };
            fx.sidechains=+[](void* p,std::vector<DynamicSidechainDependency>& routes){
                static_cast<DynamicChannelFxLeaseBank*>(p)->collectPostFaderSidechains(routes);
            };
            const auto applied=nativePublisher_.commit(nativeAccepted_.graph.roster,request,
                revision,newEpoch,epoch(),true,std::move(fx));
            if(!applied.ok())return false;
            nativeAccepted_.graph.request=std::move(request);
            nativeAccepted_.deviceEpoch=newEpoch;
            nativeAccepted_.pcmRevision=applied.revision;
            reapplyExternalOutputsSameEpoch();
            nativeAccepted_.fxOwners=std::move(owner);
            nativeControlDirty_=true;
            installNativeHardware(newEpoch,hardware,nativeAccepted_.graph.request.maxFrames);
            return channelIdPhysicalPcmActive();
        }
        if(nativeStructureLive_ || nativeControlDirty_ || publishedEpoch_.load(
               std::memory_order_acquire)!=priorEpoch || !acceptedRoute_.isObject())return false;
        const auto* compatibility=publisher_.controlPrepared(priorEpoch,revision());
        if(!compatibility)return false;
        bool hasEffects=false;
        if(rateChanged)
            for(int lane=0;lane<fxSlots && !hasEffects;++lane)
                hasEffects=compatibility->insertsOn(lane)>0;
        const int quantum=armix::device::retainedMixerQuantum(
            std::min(compatibility->maxFramesPerPlugin(),reboundQuantum_.load(std::memory_order_acquire)),
            deviceFrames,Block::hardFrameLimit,rateChanged,hasEffects);
        if(!quantum || !publisher_.rebindQuiescent(priorEpoch,newEpoch,[&]() noexcept {
                return newEpoch==epoch();
            }))return false;
        reboundQuantum_.store(quantum,std::memory_order_release);
        publishedEpoch_.store(newEpoch,std::memory_order_release);
        userPatched_.store(true,std::memory_order_release);
        return newEpoch==epoch() && ready();
    }
    bool ready() const noexcept {
        return active() && !topologyFault_.load(std::memory_order_acquire)
            && (channelIdPhysicalPcmActive() ||
                publishedEpoch_.load(std::memory_order_acquire)==epoch());
    }
    // Status / control thread only. An empty prepared graph is ready, but
    // cannot be advertised as a LIVE patched route or a savable Show.
    bool hasExplicitRoute() const noexcept {
        return channelIdPhysicalPcmActive() || (ready() && acceptedRoute_.isObject());
    }
    // CONTROL thread: authorization intent for at least one external ASIO
    // pair on THIS device epoch. Do not demand applied PCM here: the first
    // callback/readback needs output enabled before it can prove LIVE.
    bool hasVerifiedExternalOutputIntent() const noexcept {
        if(!channelIdPhysicalPcmActive() ||
           externalOutputEpoch_!=epoch() || externalOutputVerified_.empty())
            return false;
        return std::any_of(externalOutputAssignments_.begin(),
                           externalOutputAssignments_.end(),[this](const auto& patch) {
            return std::find(externalOutputVerified_.begin(),
                             externalOutputVerified_.end(),patch.channel.value)
                   !=externalOutputVerified_.end();
        });
    }
    std::uint64_t epoch() const noexcept {
        return deviceEpoch_.load(std::memory_order_acquire);
    }
    std::uint64_t revision() const noexcept {return publisher_.lastRevision();} // control only

    // Control thread only. The test uses the exact pair the current mixer
    // owns, including non-contiguous physical pins mapped to compact indices.
    std::array<int,2> acceptedMainCallbackPair() const noexcept {
        if(!ready())return {{-1,-1}};
        if(channelIdPhysicalPcmActive())
            return {{nativeAccepted_.hardware.mainL,nativeAccepted_.hardware.mainR}};
        if(const auto* graph=publisher_.controlPrepared(epoch(),revision()))
            return {{graph->mainOutputLeft(),graph->mainOutputRight()}};
        return {{-1,-1}};
    }

    bool loopbackInputUnpatched(int ordinal) const noexcept {
        if (!channelIdPhysicalPcmActive() || !ready() || ordinal < 0)
            return false;
        const auto& sources = nativeAccepted_.graph.request.sources;
        return std::none_of(sources.begin(), sources.end(), [ordinal](const auto& source) {
            return source.kind == armix::mixer::DynamicSourceKind::physical &&
                (source.inputL == ordinal || (source.stereo && source.inputR == ordinal));
        });
    }

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
                 std::array<int,2>* actualMonitorPair=nullptr,
                 bool allowMutedMetering=false,
                 std::span<const armix::mixer::DynamicPlayerPcmBlock> playerPcm={},
                 bool mainOnlyMuted=false,
                 const NativeOutputLease* outputLease=nullptr,bool privateCueAllowed=false,
                 std::array<float*,2>* secondaryMainTap=nullptr) noexcept {
        if(secondaryMainTap)*secondaryMainTap={{nullptr,nullptr}};
        if(actualOutputPair)*actualOutputPair={{-1,-1}};
        if(actualMonitorPair)*actualMonitorPair={{-1,-1}};
        if(!active())return false;
        // The real JUCE M4 callback owns one mutable DSP scratch/fade state.
        // A broken backend overlapping two process() calls must never run
        // two VST chains against the same prepared buffers. Fail SILENT,
        // do not spin or take a lock on the host-owned realtime callback.
        armix::rt::RealtimeRenderPermit exclusive{rendering_};
        if(!exclusive) {
            renderAudit_.deniedOverlap();
            if(outputs && frames>0 && frames<=Block::hardFrameLimit)
                for(int c=0;c<outputCount;++c)
                    if(outputs[c])juce::FloatVectorOperations::clear(outputs[c],frames);
            return false;
        }
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
            nativeMeterEpoch_.store(0,std::memory_order_release);
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
        if(topologyFault_.load(std::memory_order_acquire) ||
           playerPcm.size()>2) {
            silence();return true; // future dual decks are maximum 2
        }
        if(inputCount!=callbackInputCount_.load(std::memory_order_acquire)) {
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        // A prepared graph does NOT authorize physical inputs: skip ALL
        // channel, AUX and MASTER vendor processing until explicit native ACK.
        // One atomic read; outputs are cleared by the fail-silent gate above.
        if(!userPatched_.load(std::memory_order_acquire)) {
            renderAudit_.unpatched();
            fadeRemaining_=0;
            lastOutputL_=lastOutputR_=0.f;
            silence();return true;
        }
        const auto generation=epoch();
        const bool channelIdRender=generation!=0 &&
            channelIdPcmEpoch_.load(std::memory_order_acquire)==generation;
        // Native hardware destinations are published before native admission.
        // The compatibility lease is only required by its own render path.
        auto lease=publisher_.acquire(channelIdRender);
        const auto* snapshot=lease.get(generation);
        if(!channelIdRender && (!snapshot || !snapshot->prepared)) {
            silence();return true;
        }
        if(channelIdRender&&outputLease&&outputLease->epoch!=generation){
            silence();return true;
        }
        const auto native=channelIdRender?
            (outputLease?outputLease->packed:nativeHardware_.load(std::memory_order_acquire)):0;
        const int outLeft=channelIdRender?nativePin(native,0):snapshot->prepared->mainOutputLeft();
        const int outRight=channelIdRender?nativePin(native,1):snapshot->prepared->mainOutputRight();
        if(epoch()!=generation || (channelIdRender &&
           channelIdPcmEpoch_.load(std::memory_order_acquire)!=generation)) {
            silence();return true;
        }
        const bool splitMain=channelIdRender&&armix::mixer::DmxOutputSinkPlan{
            outputCount,{outLeft,outRight},{nativePin(native,2),nativePin(native,3)},true}.secondaryMain();
        if((splitMain&&frames>Block::hardFrameLimit)||
           (!splitMain&&(outLeft<0||outRight<0||outLeft==outRight||
           outLeft>=outputCount||outRight>=outputCount||
           !outputs[outLeft]||!outputs[outRight]))){
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        // Distinct preallocated logical MAIN PCM. Physical ASIO never sees
        // this buffer in split mode; only the existing secondary consumer.
        float* const mainL=splitMain?secondaryMainPcm_[0].data():outputs[outLeft];
        float* const mainR=splitMain?secondaryMainPcm_[1].data():outputs[outRight];
        float* monitorL=nullptr;
        float* monitorR=nullptr;
        const int monitorLeft=channelIdRender?nativePin(native,2):
            snapshot->prepared->monitorOutputLeft();
        if(channelIdRender?monitorLeft>=0:snapshot->prepared->monitorEnabled()) {
            const int l=monitorLeft;
            const int r=channelIdRender?nativePin(native,3):snapshot->prepared->monitorOutputRight();
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
        if(actualOutputPair && !masterMuted && !splitMain)*actualOutputPair={{outLeft,outRight}};
        if(masterMuted && actualMonitorPair)*actualMonitorPair={{-1,-1}};
        const auto renderRevision=channelIdRender?0:snapshot->graphRevision;
        if(renderRevision!=audioRevision_ || generation!=audioEpoch_) {
            audioRevision_=renderRevision;
            audioEpoch_=generation;
            fadeRemaining_=rampFrames;
            fadeFromL_=lastOutputL_;
            fadeFromR_=lastOutputR_;
        }
        // Muted native metering uses scratch and keeps the output ramp at zero.
        const bool meterOnly=masterMuted && allowMutedMetering && channelIdRender;
        if(masterMuted) {
            // True device disarm primes the next MAIN state without output blips.
            mainOnlyMuteSlew_.reset(mainOnlyMuted);
            masterMonoPolicy_.primeWhileMuted(masterMono);
            fadeRemaining_=rampFrames;
            fadeFromL_=fadeFromR_=0.f;
            lastOutputL_=lastOutputR_=0.f;
            if(!meterOnly){
                renderAudit_.muted();
                silence();return true;
            }
        }
        // Large/variable JUCE quantum is chunked using stack pointer views;
        // no heap allocation or new processing object inside callback.
        std::array<const float*,armix::device::LiveInputPortRegistry::limit> shifted{};
        std::array<float*,armix::device::LiveInputPortRegistry::limit> outputSlices{};
        int max=channelIdRender?nativeFrames(native):
            std::min(snapshot->prepared->maxFramesPerPlugin(),
                     reboundQuantum_.load(std::memory_order_acquire));
        if(meterOnly)max=std::min(max,mutedMeterFrames);
        if(max<=0) {
            topologyFault_.store(true,std::memory_order_release);
            silence();return true;
        }
        // One attempt per outer JUCE callback; a 64/256/1024-frame chunk
        // loop still owns exactly ONE native or compatibility DSP pass.
        const auto auditPath=channelIdRender?
            armix::rt::ProductionRenderAudit::Path::channelId:
            armix::rt::ProductionRenderAudit::Path::compatibilityM4;
        renderAudit_.started(auditPath);
        float peakL=0.f,peakR=0.f,monitorPeakL=0.f,monitorPeakR=0.f;
        std::array<float,count> perChannelPeaks{};
        std::array<float,count> perChannelPeaksL{},perChannelPeaksR{};
        // Bounded realtime meter scratch for all accepted ChannelIds, not
        // only the original ten. No allocation or graph iteration on AUDIO.
        std::array<float,nativeMeterCapacity> nativeMaxL{},nativeMaxR{};
        std::array<float,auxCount> perAuxPeaks{};
        std::uint64_t completedNativeRevision=0;
        std::size_t completedMeterCount=0;
        bool mixedNativeRevisions=false;
        for(int offset=0;offset<frames;offset+=max) {
            const int n=std::min(max,frames-offset);
            for(int c=0;c<inputCount;++c)
                shifted[c]=inputs&&inputs[c]?inputs[c]+offset:nullptr;
            if(channelIdRender && !meterOnly)
                for(int c=0;c<outputCount;++c)
                    outputSlices[c]=outputs[c]?outputs[c]+offset:nullptr;
            armix::mixer::RenderLevels level;
            bool processed=false;
            if(channelIdRender) {
                // The current M4 import is at most 10+4+MAIN+MONITOR.
                // Meter sources are the ACTUAL ChannelId PCM POST-fader
                // lanes, not another mixer pass or reused M4 peak values.
                // process() initializes only the actual leased roster prefix.
                std::array<float,nativeMeterCapacity> tapsL,tapsR;
                // One source block is read from each native player FIFO by
                // the HOST before calling process(). This existing ChannelId
                // renderer performs all FX/WIRE/MAIN mixing; no second audio
                // device or per-channel FIFO consumption is permitted.
                std::array<armix::mixer::DynamicPlayerPcmBlock,2> slice{};
                for(std::size_t i=0;i<playerPcm.size();++i) {
                    const auto& source=playerPcm[i];
                    slice[i]={source.deckId,
                              source.left?source.left+offset:nullptr,
                              source.right?source.right+offset:nullptr};
                }
                const auto noExtraFx=[](armix::mixer::ChannelId,float*,float*,int) noexcept {};
                std::uint64_t sliceRevision=0;
                std::size_t sliceMeterCount=0;
                processed=nativePublisher_.render(
                    generation,shifted.data(),inputCount,
                    meterOnly?mutedMain_[0].data():mainL+offset,
                    meterOnly?mutedMain_[1].data():mainR+offset,n,false,noExtraFx,
                    monitorL?(meterOnly?mutedMonitor_[0].data():monitorL+offset):nullptr,
                    monitorR?(meterOnly?mutedMonitor_[1].data():monitorR+offset):nullptr,
                    tapsL,tapsR,
                    std::span<const armix::mixer::DynamicPlayerPcmBlock>(
                        slice.data(),playerPcm.size()),
                     meterOnly?nullptr:outputSlices.data(),
                     meterOnly?0:outputCount,{},
                     privateCueAllowed&&!splitMain&&!masterMuted&&monitorL&&monitorR?native:0,
                     &sliceRevision,&sliceMeterCount,
                     !splitMain&&!monitorL&&nativePin(native,2)<0&&nativePin(native,3)<0&&
                         (native&nativeDormantMonitorBit)!=0);
                if(processed) {
                    if(completedNativeRevision&&(completedNativeRevision!=sliceRevision||
                       completedMeterCount!=sliceMeterCount))mixedNativeRevisions=true;
                    completedNativeRevision=sliceRevision;completedMeterCount=sliceMeterCount;
                    for(std::size_t ch=0;ch<sliceMeterCount;++ch) {
                        nativeMaxL[ch]=std::max(nativeMaxL[ch],tapsL[ch]);
                        nativeMaxR[ch]=std::max(nativeMaxR[ch],tapsR[ch]);
                    }
                    for(int ch=0;ch<count;++ch) {
                        if(static_cast<std::size_t>(ch)<sliceMeterCount){
                            level.channelPeakL[ch]=tapsL[ch];level.channelPeakR[ch]=tapsR[ch];
                            level.channelPeak[ch]=std::max(tapsL[ch],tapsR[ch]);
                        }
                    }
                    for(int b=0;b<auxCount;++b)
                        if(static_cast<std::size_t>(count+b)<sliceMeterCount)
                            level.auxPeak[b]=std::max(tapsL[count+b],tapsR[count+b]);
                }
            } else {
                processed=snapshot->prepared->process(
                    shifted.data(),inputCount,mainL+offset,mainR+offset,n,
                    &level,externalContext,externalFx,
                    monitorL?monitorL+offset:nullptr,
                    monitorR?monitorR+offset:nullptr);
            }
            if(!processed) {
                // A failed DSP pass must never leak partially rendered
                // MAIN/MONITOR PCM into an armed physical output.
                // A native publication can briefly contend with a reader.
                // The epoch/pin checks above own sticky topology faults.
                if(!channelIdRender)topologyFault_.store(true,std::memory_order_release);
                renderAudit_.failed(auditPath);
                silence();return true;
            }
            for(int ch=0;ch<count;++ch) {
                perChannelPeaks[ch]=std::max(perChannelPeaks[ch],level.channelPeak[ch]);
                perChannelPeaksL[ch]=std::max(perChannelPeaksL[ch],level.channelPeakL[ch]);
                perChannelPeaksR[ch]=std::max(perChannelPeaksR[ch],level.channelPeakR[ch]);
            }
            for(int b=0;b<auxCount;++b)
                perAuxPeaks[b]=std::max(perAuxPeaks[b],level.auxPeak[b]);
            if(meterOnly)continue;
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
                // Mute only MAIN: BUS/EXTERNAL PCM remains live post-FX.
                if(channelIdRender) {
                    const float mainGain=mainOnlyMuteSlew_.next(mainOnlyMuted);
                    l*=mainGain;
                    r*=mainGain;
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
        if(!channelIdRender || meterOnly)
            for(int c=0;c<outputCount;++c)
                if(outputs[c] && (meterOnly || (outputs[c]!=mainL && outputs[c]!=mainR &&
                   outputs[c]!=monitorL && outputs[c]!=monitorR)))
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
        if(channelIdRender) {
            if(!mixedNativeRevisions)
                for(std::size_t ch=0;ch<completedMeterCount;++ch) {
                    nativeMeterPeaksL_[ch].store(nativeMaxL[ch],std::memory_order_relaxed);
                    nativeMeterPeaksR_[ch].store(nativeMaxR[ch],std::memory_order_relaxed);
                }
            if(nativeHardwareObserved_.load(std::memory_order_relaxed)!=native)
                nativeHardwareObserved_.store(native,std::memory_order_release);
            nativeMeterRevision_.store(mixedNativeRevisions?0:completedNativeRevision,std::memory_order_relaxed);
            nativeMeterEpoch_.store(generation,std::memory_order_release);
        } else nativeMeterEpoch_.store(0,std::memory_order_release);
        // Only completed PCM may escape. The tap is callback-local and
        // never survives a rejected render, epoch, or muted meter pass.
        if(splitMain&&!masterMuted&&secondaryMainTap)*secondaryMainTap={{mainL,mainR}};
        renderAudit_.completed(auditPath);
        return true;
    }

    juce::var status(juce::AudioIODevice* device) const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("topologyFault",topologyFault_.load(std::memory_order_acquire));
        out->setProperty("ready",ready());
        out->setProperty("hasAcceptedRoute",hasExplicitRoute());
        const bool nativePcm=channelIdPhysicalPcmActive();
        out->setProperty("nativeChannelIdPcmActive",nativePcm);
        // Exact attempted PCM producers, not an optimistic GUI/admission
        // indicator. Allocations are CONTROL-only in status(), never render().
        const auto evidence=renderAudit_.snapshot();
        auto audit=std::make_unique<juce::DynamicObject>();
        audit->setProperty("nativeStarted",static_cast<juce::int64>(evidence.nativeStarted));
        audit->setProperty("nativeCompleted",static_cast<juce::int64>(evidence.nativeCompleted));
        audit->setProperty("nativeFailed",static_cast<juce::int64>(evidence.nativeFailed));
        audit->setProperty("m4Started",static_cast<juce::int64>(evidence.m4Started));
        audit->setProperty("m4Completed",static_cast<juce::int64>(evidence.m4Completed));
        audit->setProperty("m4Failed",static_cast<juce::int64>(evidence.m4Failed));
        audit->setProperty("overlapDenied",static_cast<juce::int64>(evidence.overlapDenied));
        audit->setProperty("unpatchedSilent",static_cast<juce::int64>(evidence.unpatchedSilent));
        audit->setProperty("mutedNoRender",static_cast<juce::int64>(evidence.mutedNoRender));
        out->setProperty("renderAudit",juce::var(audit.release()));
        // ShowV2 and the legacy M4 route cannot serialize extra ChannelId
        // GROUP/FX RETURN buses. This stays visible even after DSP faults
        // silent so SAVE/RECALL cannot silently discard audible topology.
        out->setProperty("nativeBusStructural",nativeStructureLive_);
        if(nativePcm)out->setProperty("nativeBus",nativeBusStatus());
        else if(hasAuthoredMixerContent()) {
            auto retained=nativeBusStatus(true);
            if(retained.isObject())
                out->setProperty("retainedNativeBus",std::move(retained));
        }
        // This exact original vendor Graph owner is retained by the native
        // accepted snapshot, NOT merely a catalog name or M4 CH1 label.
        // Message-thread diagnostic only; no false universal capabilities.
        out->setProperty("originalCh1GraphLeased",
            nativePcm && nativeAccepted_.fxOwners && nativeAccepted_.graph.ids.inputs[0].value &&
            nativeAccepted_.fxOwners->externalChannel()==
                nativeAccepted_.graph.ids.inputs[0]);
        out->setProperty("epoch",static_cast<juce::int64>(epoch()));
        out->setProperty("dmxDeviceEpoch",juce::String(std::to_string(epoch())));
        out->setProperty("revision",static_cast<juce::int64>(controlRevision()));
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
        // A fresh device starts with a REAL native silent prepared topology.
        // Expose it to the Console so IN can be selected directly, without
        // inventing ten browser channels or an APPLY ROUTING prerequisite.
        if(active() && ready() && !hasExplicitRoute() && sourceTemplate_.isObject())
            out->setProperty("preparedM4Route",sourceTemplate_);
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
        juce::Array<juce::var> rackFlags;
        for(int lane=0;lane<fxSlots;++lane)
            rackFlags.add(prepared?prepared->rackBypassed(lane):false);
        out->setProperty("fxRackBypassed",juce::var(rackFlags));
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
        juce::Array<juce::var> liveMain,liveMonitor,liveMonitorSends;
        const auto* mainGraph=publisher_.controlPrepared(epoch(),revision());
        const auto addPair=[&](juce::Array<juce::var>& pair,int left,int right) {
            const int n=callbackOutputCount_.load(std::memory_order_acquire);
            if(left<0 || right<0 || left>=n || right>=n)return;
            pair.add(enabledOutputPorts_[left].load(std::memory_order_relaxed));
            pair.add(enabledOutputPorts_[right].load(std::memory_order_relaxed));
        };
        if(nativePcm) {
            const auto& hw=nativeAccepted_.hardware;
            addPair(liveMain,hw.mainL,hw.mainR);
            if(hw.monitorEnabled)addPair(liveMonitor,hw.monitorL,hw.monitorR);
        } else if(mainGraph && ready() && registryValid_.load(std::memory_order_acquire)) {
            addPair(liveMain,mainGraph->mainOutputLeft(),mainGraph->mainOutputRight());
            if(mainGraph->monitorEnabled())
                addPair(liveMonitor,mainGraph->monitorOutputLeft(),mainGraph->monitorOutputRight());
        }
        if(mainGraph && !liveMonitor.isEmpty())
            for(float gain:mainGraph->monitorGains())liveMonitorSends.add(gain);
        out->setProperty("secondaryMainOnly",nativePcm&&nativeAccepted_.hardware.secondaryMain());
        out->setProperty("mainOutputs",juce::var(liveMain));
        out->setProperty("monitorOutputs",juce::var(liveMonitor));
        out->setProperty("busOutputObserved",nativePcm&&nativeBusOutputObserved());
        out->setProperty("monitorSends",juce::var(liveMonitorSends));
        juce::Array<juce::var> external;
        for(const auto& patch:externalOutputAssignments_) {
            auto row=std::make_unique<juce::DynamicObject>();
            row->setProperty("channelId",juce::String(std::to_string(patch.channel.value)));
            row->setProperty("physicalLeft",patch.left);
            row->setProperty("physicalRight",patch.right);
            row->setProperty("leftName",patch.leftName);
            row->setProperty("rightName",patch.rightName);
            const auto live=nativePublisher_.externalOutput(
                nativeAccepted_.deviceEpoch,nativeAccepted_.pcmRevision,patch.channel);
            const auto applied=nativePublisher_.appliedExternalOutput(
                nativeAccepted_.deviceEpoch,nativeAccepted_.pcmRevision,patch.channel);
            const bool installed=nativePcm && externalOutputEpoch_==epoch() &&
                live.first>=0 && live.second>=0 &&
                applied==live &&
                live.first<callbackOutputCount_.load(std::memory_order_acquire) &&
                live.second<callbackOutputCount_.load(std::memory_order_acquire) &&
                enabledOutputPorts_[live.first].load(std::memory_order_relaxed)==patch.left &&
                enabledOutputPorts_[live.second].load(std::memory_order_relaxed)==patch.right;
            row->setProperty("status",installed?"LIVE":
                (nativePcm && externalOutputEpoch_==epoch() && live.first>=0 &&
                 live.second>=0)?"PENDING":"NEEDS_REMAP");
            external.add(juce::var(row.release()));
        }
        out->setProperty("externalOutputs",juce::var(external));
        out->setProperty("note","Native 10CH+4AUX production mixer; unpatched startup is SILENT. Explicit input assignment/ACK required. Hardware FAT unqualified.");
        return juce::var(out.release());
    }
private:
    // Never heap/lock in RT; every prepared graph references this same stable
    // scalar. OFF means bypass all FX, while per-rack/slot states are untouched.
    std::atomic<bool> globalFxBypassed_{false};
    std::vector<armix::session::WaitingAsioInput> waitingAsioInputs_;
    armix::device::LogicalInputRegistry logicalInputs_; // CONTROL only
    armix::device::LogicalOutputRegistry logicalOutputs_; // CONTROL only
    bool logicalOutputPairReserved(const juce::String& playback,int left,int right) const{
        return logicalOutputs_.reservesPair({"ASIO",playback.toStdString()},left,right);
    }
    std::uint64_t logicalInputRevision_=1,logicalOutputRevision_=1;
    std::vector<armix::session::StoredExternalOutput> externalOutputAssignments_;
    std::vector<std::uint64_t> externalOutputVerified_; // per amplifier, CONTROL only
    std::uint64_t externalOutputEpoch_=0; // CONTROL only, never callback state
    // Four 9-bit callback indices (0 means absent), quantum, and a high
    // bit authorizing an intentionally unbound logical MONITOR. One atomic
    // read gives AUDIO a coherent destination/quantum contract.
    std::atomic<std::uint64_t> nativeHardware_{0},nativeHardwareObserved_{0};
    std::uint64_t busPatchEpoch_=0; // CONTROL-only, arm awaits exact callback map
    static constexpr std::uint64_t nativeDormantMonitorBit=std::uint64_t{1}<<63;
    static int nativeFrames(std::uint64_t hardware) noexcept {
        return static_cast<int>((hardware&~nativeDormantMonitorBit)>>36);
    }
    static int nativePin(std::uint64_t hardware,int index) noexcept {
        return static_cast<int>((hardware>>(index*9))&511U)-1;
    }
    void installNativeHardware(std::uint64_t generation,
            const armix::mixer::VerifiedNativeOutputs& hw,int frames) noexcept {
        nativePublisher_.cueControl().cancel(false); // never retain audition across output rebinding
        const auto dormant=nativeAccepted_.graph.ids.monitor.value&&!hw.monitorEnabled&&
            !hw.secondaryMain()?nativeDormantMonitorBit:std::uint64_t{0};
        nativeHardware_.store(dormant | (static_cast<std::uint64_t>(frames)<<36) |
            (static_cast<std::uint64_t>(hw.monitorR+1)<<27) |
            (static_cast<std::uint64_t>(hw.monitorL+1)<<18) |
            (static_cast<std::uint64_t>(hw.mainR+1)<<9) |
             static_cast<std::uint64_t>(hw.mainL+1),std::memory_order_release);
        enabled_.store(true,std::memory_order_release);
        nativeMeterEpoch_.store(0,std::memory_order_release);
        channelIdPcmEpoch_.store(generation,std::memory_order_release);
        userPatched_.store(true,std::memory_order_release);
    }
    static constexpr int mutedMeterFrames=512;
    std::array<std::array<float,mutedMeterFrames>,2> mutedMain_{},mutedMonitor_{};
    std::array<std::array<float,Block::hardFrameLimit>,2> secondaryMainPcm_{};
    std::atomic_flag rendering_=ATOMIC_FLAG_INIT; // one AUDIO producer
    armix::rt::ProductionRenderAudit renderAudit_; // per-process, RT-only writers
    Publisher publisher_;
    armix::mixer::DynamicNativeSessionPublisher nativePublisher_;
    armix::mixer::UnifiedLegacyAccepted nativeAccepted_;
    std::atomic<std::uint64_t> channelIdPcmEpoch_{0};
    bool nativeStructureLive_=false; // CONTROL only; never read by callback
    bool nativeControlDirty_=false; // Native edit cannot fall back to stale M4 PCM
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
    std::atomic<int> reboundQuantum_{Block::hardFrameLimit};
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
    // Constant-memory native post-fader meters; capacity affects only
    // telemetry, not how many channels the audio graph can admit.
    static constexpr std::size_t nativeMeterCapacity=128;
    std::array<std::atomic<float>,nativeMeterCapacity> nativeMeterPeaksL_{},nativeMeterPeaksR_{};
    std::atomic<std::uint64_t> nativeMeterEpoch_{0},nativeMeterRevision_{0};
    // Exclusive render-producer state. Bounded fade starts from the exact
    // previously output sample, preventing a topology switch discontinuity.
    static constexpr int rampFrames=256;
    int fadeRemaining_=0;
    std::uint64_t audioRevision_=0,audioEpoch_=0;
    float fadeFromL_=0.f,fadeFromR_=0.f;
    float lastOutputL_=0.f,lastOutputR_=0.f;
    armix::mixer::MainOnlyMuteSlew mainOnlyMuteSlew_{};
};
