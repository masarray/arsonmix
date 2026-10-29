// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/MasterOutputMonoPolicy.h"
#include "../engine/core/LiveChannelFxGraph.h"
#include "VstBusLayout.h"
#include "../engine/core/NativeDryRouteCommand.h"
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
    static constexpr int count=armix::mixer::stripCount;
    static constexpr int auxCount=armix::mixer::auxBusCount;
    static constexpr int fxSlots=count+auxCount;
    struct ChannelEdit {
        float linear=1.f, pan=0.f;
        bool mute=false, solo=false, mono=false;
    };

    // JUCE calls aboutToStart after configuring the input/output device.
    // One device session gets a new epoch; stored route cannot survive it.
    void prepareDevice (juce::AudioIODevice* device) noexcept {
        const auto generation=deviceEpoch_.fetch_add(1,std::memory_order_acq_rel)+1;
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
        for(auto& p:auxPeaks_)p.store(0.f,std::memory_order_relaxed);
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
    }

    juce::String apply (const juce::var& command, bool directPermitted,
                        bool independentClock, int loadedFx,
                        const std::array<FxSelection,fxSlots>& selections,
                        juce::AudioPluginFormatManager& formats,
                        double sampleRate,int deviceFrames) {
        // Called by the native message thread only. Not callable from RT.
        if(!directPermitted || independentClock)
            return "Dry Routing LAB requires one verified DIRECT audio device";
        if(loadedFx>0)
            return "Disable/remove Channel 1 VST3 before DRY Routing LAB (no plugin state is touched)";
        if(!command.isObject()) return "Invalid Dry Routing configuration";
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
        std::array<ChannelEdit,count> proposed{};
        for(int i=0;i<count;++i) {
            const auto& strip=cfg.stripsAndMaster.strips[i];
            proposed[i]={strip.faderLinear,strip.pan,strip.mute,strip.solo,
                plan.channels.effectiveFormats[i]==armix::mixer::ChannelFormat::mono};
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
            const auto& req=selections[lane];
            if(req.id.isEmpty())continue;
            const bool scheduled=lane<count?plan.scheduled[lane]:
                plan.aux.buses[lane-count].enabled;
            const juce::String label=lane<count
                ?"CH "+juce::String(lane+1)
                :"AUX "+juce::String(lane-count+1);
            if(!scheduled)
                return label+": enable a real input/return before adding VST3";
            if(prior && next->prepared->reuse(lane,*prior,req.id)) {
                ++insertCount;
                ++reusedCount;
                continue; // NO disk scan, NO plugin prepare, NO tail/state reset
            }
            if(!(sampleRate>0.0) || deviceFrames<=0)
                return "Audio device sample rate or buffer size is invalid";
            auto* fmt=formats.getFormat(0);
            if(!fmt || !fmt->fileMightContainThisPluginType(req.path))
                return label+": VST3 module is unavailable; rescan catalog";
            juce::OwnedArray<juce::PluginDescription> descriptions;
            fmt->findAllTypesForFile(descriptions,req.path);
            juce::PluginDescription* chosen=nullptr;
            for(auto* item:descriptions)
                if(item && item->uniqueId==req.uid && !item->isInstrument)
                    {chosen=item;break;}
            if(!chosen)
                return label+": selected VST3 effect identity not found (instrument plugins are unsupported)";
            juce::String pluginError;
            auto instance=formats.createPluginInstance(*chosen,sampleRate,
                juce::jmax(32,deviceFrames),pluginError);
            if(!instance)
                return label+": VST3 instantiation failed: "+pluginError;
            const auto layoutError=armix::vst::prepareStereoEffectLayout(*instance);
            if(layoutError.isNotEmpty())
                return label+": "+layoutError;
            try {
                instance->prepareToPlay(sampleRate,
                    juce::jmax(32,deviceFrames));
            } catch(const std::exception& ex) {
                return label+": plugin prepare failed: "+juce::String(ex.what());
            } catch(...) {
                return label+": plugin prepare failed (unknown exception)";
            }
            // Uncompensated latent inserts produce comb filtering in PRE/POST
            // sends. Until native PDC exists, reject instead of lying about
            // phase accuracy or silent extra buffer latency.
            if(instance->getLatencySamples()!=0)
                return label+
                    ": this VST3 reports latency; choose zero-latency mode until PDC is implemented";
            if(!next->prepared->install(lane,std::move(instance),chosen->name,req.id))
                return label+": duplicate or invalid VST3 insert";
            ++insertCount;
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
                acceptedRoute_=command;
                channelEdits_=proposed; // control thread SSOT for this route revision
                for(int i=0;i<count;++i)channelFxNames_[i]=selections[i].name;
                for(int b=0;b<auxCount;++b)
                    auxFxNames_[b]=selections[count+b].name;
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

    void disable() noexcept {
        enabled_.store(false,std::memory_order_release);
        publishedEpoch_.store(0,std::memory_order_release);
        publisher_.unpublish(); // control thread only
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        sendCount_.store(0,std::memory_order_relaxed);
        fxCount_.store(0,std::memory_order_relaxed);
        fxReused_.store(0,std::memory_order_relaxed);
        acceptedRoute_=juce::var(); // control-thread STOP only
        for(auto& n:auxFxNames_)n.clear();
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
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
    std::uint64_t epoch() const noexcept {
        return deviceEpoch_.load(std::memory_order_acquire);
    }
    std::uint64_t revision() const noexcept {return publisher_.lastRevision();} // control only

    // JUCE render callback, with a verified single-clock device. Routing
    // publishes at the BLOCK boundary. Missing/stale ports output SILENCE.
    // A failed run never falls through to an unrelated microphone/plugin path.
    bool process(const float* const* inputs,int inputCount,
                 float* const* outputs,int outputCount,int frames,
                 float masterGain,bool masterMuted,bool masterMono) noexcept {
        if(!active())return false;
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
        for(auto& p:auxPeaks_)p.store(0.f,std::memory_order_relaxed);
        // Stale meters must never survive a failed device/epoch mapping.
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        if(frames<=0)return true;
        for(int c=0;c<outputCount;++c)
            if(outputs && outputs[c])
                juce::FloatVectorOperations::clear(outputs[c],frames);
        if(!outputs||outputCount<2||!outputs[0]||!outputs[1] ||
           outputCount!=callbackOutputCount_.load(std::memory_order_acquire) ||
           inputCount<0 || inputCount>armix::device::LiveInputPortRegistry::limit) {
            topologyFault_.store(true,std::memory_order_release);
            return true;
        }
        if(topologyFault_.load(std::memory_order_acquire))return true;
        const auto generation=epoch();
        auto lease=publisher_.acquire();
        const auto* snapshot=lease.get(generation);
        if(!snapshot||!snapshot->prepared ||
           inputCount!=callbackInputCount_.load(std::memory_order_acquire)) {
            if(inputCount!=callbackInputCount_.load(std::memory_order_acquire))
                topologyFault_.store(true,std::memory_order_release);
            fadeRemaining_=0;
            lastOutputL_=lastOutputR_=0.f;
            return true;
        }
        if(snapshot->graphRevision!=audioRevision_ || generation!=audioEpoch_) {
            audioRevision_=snapshot->graphRevision;
            audioEpoch_=generation;
            fadeRemaining_=rampFrames;
            fadeFromL_=lastOutputL_;
            fadeFromR_=lastOutputR_;
        }
        // Strict output mute wins over every topology fade. Output buffers
        // were zeroed before reading the graph, so skip all muted DSP work.
        // Prime a 256-sample fade FROM ZERO for the first unmuted block.
        // Never interpolate any previously audible sample while muted.
        if(masterMuted) {
            masterMonoPolicy_.primeWhileMuted(masterMono); // RT-only; no startup stereo leak
            fadeRemaining_=rampFrames;
            fadeFromL_=fadeFromR_=0.f;
            lastOutputL_=lastOutputR_=0.f;
            return true;
        }
        // Large/variable JUCE quantum is chunked using stack pointer views;
        // no heap allocation or new processing object inside callback.
        std::array<const float*,armix::device::LiveInputPortRegistry::limit> shifted{};
        const int max=snapshot->prepared->maxFramesPerPlugin();
        float peakL=0.f,peakR=0.f;
        std::array<float,count> perChannelPeaks{};
        std::array<float,auxCount> perAuxPeaks{};
        for(int offset=0;offset<frames;offset+=max) {
            const int n=std::min(max,frames-offset);
            for(int c=0;c<inputCount;++c)
                shifted[c]=inputs&&inputs[c]?inputs[c]+offset:nullptr;
            armix::mixer::RenderLevels level;
            if(!snapshot->prepared->process(shifted.data(),inputCount,
                   outputs[0]+offset,outputs[1]+offset,n,&level))
                return true;
            for(int ch=0;ch<count;++ch)
                perChannelPeaks[ch]=std::max(perChannelPeaks[ch],level.channelPeak[ch]);
            for(int b=0;b<auxCount;++b)
                perAuxPeaks[b]=std::max(perAuxPeaks[b],level.auxPeak[b]);
            const float g=std::clamp(masterGain,0.f,2.f);
            for(int i=0;i<n;++i) {
                float l=outputs[0][offset+i]*g;
                float r=outputs[1][offset+i]*g;
                if(fadeRemaining_>0) {
                    const float t=1.f-static_cast<float>(fadeRemaining_-1)/
                                           static_cast<float>(rampFrames);
                    l=fadeFromL_+(l-fadeFromL_)*t;
                    r=fadeFromR_+(r-fadeFromR_)*t;
                    --fadeRemaining_;
                }
                // 64-sample click-safe mono transition, with exact dual-mono
                // L/R at blend=1. No extra device quantum or buffer latency.
                masterMonoPolicy_.process(masterMono,l,r);
                outputs[0][offset+i]=l;
                outputs[1][offset+i]=r;
                lastOutputL_=l;
                lastOutputR_=r;
                peakL=std::max(peakL,std::abs(l));
                peakR=std::max(peakR,std::abs(r));
            }
        }
        peakL_.store(peakL,std::memory_order_relaxed);
        peakR_.store(peakR,std::memory_order_relaxed);
        for(int ch=0;ch<count;++ch)
            channelPeaks_[ch].store(perChannelPeaks[ch],std::memory_order_relaxed);
        for(int b=0;b<auxCount;++b)
            auxPeaks_[b].store(perAuxPeaks[b],std::memory_order_relaxed);
        return true;
    }

    juce::var status(juce::AudioIODevice* device) const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("topologyFault",topologyFault_.load(std::memory_order_acquire));
        out->setProperty("ready",ready());
        out->setProperty("epoch",static_cast<juce::int64>(epoch()));
        out->setProperty("revision",static_cast<juce::int64>(revision()));
        out->setProperty("peakL",peakL_.load(std::memory_order_relaxed));
        out->setProperty("peakR",peakR_.load(std::memory_order_relaxed));
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
        // Only the message thread calls status(), never the audio callback.
        if(active() && ready())out->setProperty("acceptedRoute",acceptedRoute_);
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
        out->setProperty("note","EXPLICIT NATIVE ROUTING LAB: per-strip stereo VST3, PRE/POST sends; no performance preset recall or hardware FAT qualification");
        return juce::var(out.release());
    }
private:
    Publisher publisher_;
    armix::dsp::MasterOutputMonoPolicy masterMonoPolicy_; // audio callback only
    std::array<ChannelEdit,count> channelEdits_{}; // JUCE message thread only
    std::array<juce::String,count> channelFxNames_{}; // status/control thread only
    std::array<juce::String,auxCount> auxFxNames_{}; // dedicated bus FX
    std::array<std::atomic<float>,auxCount> auxPeaks_{};
    std::atomic<int> auxCountActive_{0},auxSendCount_{0};
    juce::var acceptedRoute_; // native ACK only, no UI preview/localStorage truth
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
    std::atomic<float> peakL_{0.f},peakR_{0.f};
    std::atomic<int> sendCount_{0};
    std::atomic<int> fxCount_{0};
    std::atomic<int> fxReused_{0};
    std::array<std::atomic<float>,count> channelPeaks_{};
    // Exclusive render-producer state. Bounded fade starts from the exact
    // previously output sample, preventing a topology switch discontinuity.
    static constexpr int rampFrames=256;
    int fadeRemaining_=0;
    std::uint64_t audioRevision_=0,audioEpoch_=0;
    float fadeFromL_=0.f,fadeFromR_=0.f;
    float lastOutputL_=0.f,lastOutputR_=0.f;
};
