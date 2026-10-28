// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/PreparedMixerBlock.h"
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
    using Publisher=armix::rt::RealtimeRoutePublisher<Block>;
    static constexpr int count=armix::mixer::stripCount;

    // JUCE calls aboutToStart after configuring the input/output device.
    // One device session gets a new epoch; stored route cannot survive it.
    void prepareDevice (juce::AudioIODevice* device) noexcept {
        const auto generation=deviceEpoch_.fetch_add(1,std::memory_order_acq_rel)+1;
        registryValid_.store(false,std::memory_order_release);
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
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
    }

    juce::String apply (const juce::var& command, bool directPermitted,
                        bool independentClock, int loadedFx) {
        // Called by the native message thread only. Not callable from RT.
        if(!directPermitted || independentClock)
            return "Dry Routing LAB requires one verified DIRECT audio device";
        if(loadedFx>0)
            return "Disable/remove Channel 1 VST3 before DRY Routing LAB (no plugin state is touched)";
        if(!command.isObject()) return "Invalid Dry Routing configuration";
        const auto currentEpoch=deviceEpoch_.load(std::memory_order_acquire);
        if(!registryValid_.load(std::memory_order_acquire))
            return "Audio device has no validated active capture ports";
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
        Plan plan;
        armix::mixer::RenderError error;
        if(!Plan::compile(cfg,
                          {physicalPorts.data(),static_cast<std::size_t>(countNow)},
                          sends,plan,error))
            return "Invalid physical routing (inactive port, format or mapping)";
        const auto revision=expectedRevision+1;
        if(revision==0) return "Routing revision overflow";
        auto next=std::make_unique<Publisher::Snapshot>();
        next->deviceEpoch=currentEpoch;
        next->graphRevision=revision;
        next->prepared=std::make_unique<Block>(plan,Block::hardFrameLimit);
        if(!next->prepared || next->prepared->capacity()==0)
            return "Cannot allocate routing workspace";
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

    void disable() noexcept {
        enabled_.store(false,std::memory_order_release);
        publishedEpoch_.store(0,std::memory_order_release);
        publisher_.unpublish(); // control thread only
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        sendCount_.store(0,std::memory_order_relaxed);
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
    }
    void poll() noexcept {publisher_.pollRetirement();} // message thread
    bool active() const noexcept {return enabled_.load(std::memory_order_acquire);}
    float peakLeft() const noexcept {return peakL_.load(std::memory_order_relaxed);}
    float peakRight() const noexcept {return peakR_.load(std::memory_order_relaxed);}
    bool ready() const noexcept {
        return active() && publishedEpoch_.load(std::memory_order_acquire)==epoch();
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
                 float masterGain,bool masterMuted) noexcept {
        if(!active())return false;
        for(auto& p:channelPeaks_)p.store(0.f,std::memory_order_relaxed);
        // Stale meters must never survive a failed device/epoch mapping.
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
        if(frames<=0)return true;
        for(int c=0;c<outputCount;++c)
            if(outputs && outputs[c])
                juce::FloatVectorOperations::clear(outputs[c],frames);
        if(!outputs||outputCount<2||!outputs[0]||!outputs[1] ||
           outputCount!=callbackOutputCount_.load(std::memory_order_acquire) ||
           inputCount<0 || inputCount>armix::device::LiveInputPortRegistry::limit)
            return true;
        const auto generation=epoch();
        auto lease=publisher_.acquire();
        const auto* snapshot=lease.get(generation);
        if(!snapshot||!snapshot->prepared ||
           inputCount!=callbackInputCount_.load(std::memory_order_acquire)) {
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
            fadeRemaining_=rampFrames;
            fadeFromL_=fadeFromR_=0.f;
            lastOutputL_=lastOutputR_=0.f;
            return true;
        }
        // Large/variable JUCE quantum is chunked using stack pointer views;
        // no heap allocation or new processing object inside callback.
        std::array<const float*,armix::device::LiveInputPortRegistry::limit> shifted{};
        constexpr int max=Block::hardFrameLimit;
        const auto noFx=[](int,float*,float*,int) noexcept {};
        float peakL=0.f,peakR=0.f;
        std::array<float,count> perChannelPeaks{};
        for(int offset=0;offset<frames;offset+=max) {
            const int n=std::min(max,frames-offset);
            for(int c=0;c<inputCount;++c)
                shifted[c]=inputs&&inputs[c]?inputs[c]+offset:nullptr;
            armix::mixer::RenderLevels level;
            if(!snapshot->prepared->process(shifted.data(),inputCount,
                   outputs[0]+offset,outputs[1]+offset,n,noFx,&level))
                return true;
            for(int ch=0;ch<count;++ch)
                perChannelPeaks[ch]=std::max(perChannelPeaks[ch],level.channelPeak[ch]);
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
        return true;
    }

    juce::var status(juce::AudioIODevice* device) const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("ready",ready());
        out->setProperty("epoch",static_cast<juce::int64>(epoch()));
        out->setProperty("revision",static_cast<juce::int64>(revision()));
        out->setProperty("peakL",peakL_.load(std::memory_order_relaxed));
        out->setProperty("peakR",peakR_.load(std::memory_order_relaxed));
        out->setProperty("sendCount",sendCount_.load(std::memory_order_relaxed));
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
        out->setProperty("note","EXPLICIT DRY HARDWARE FAT: real pre/post sends, no VST3 or portable route persistence");
        return juce::var(out.release());
    }
private:
    Publisher publisher_;
    // All data shared between JUCE device lifecycle and GUI/control reads
    // are atomic; the callback never enumerates masks or channel names.
    std::array<std::atomic<int>,armix::device::LiveInputPortRegistry::limit> enabledPorts_{};
    std::array<std::atomic<int>,armix::device::LiveInputPortRegistry::limit> enabledOutputPorts_{};
    std::atomic<int> portCount_{0};
    std::atomic<bool> registryValid_{false};
    std::atomic<std::uint64_t> deviceEpoch_{0};
    std::atomic<std::uint64_t> publishedEpoch_{0};
    std::atomic<int> callbackInputCount_{-1};
    std::atomic<int> callbackOutputCount_{-1};
    std::atomic<bool> enabled_{false};
    std::atomic<float> peakL_{0.f},peakR_{0.f};
    std::atomic<int> sendCount_{0};
    std::array<std::atomic<float>,count> channelPeaks_{};
    // Exclusive render-producer state. Bounded fade starts from the exact
    // previously output sample, preventing a topology switch discontinuity.
    static constexpr int rampFrames=256;
    int fadeRemaining_=0;
    std::uint64_t audioRevision_=0,audioEpoch_=0;
    float fadeFromL_=0.f,fadeFromR_=0.f;
    float lastOutputL_=0.f,lastOutputR_=0.f;
};
