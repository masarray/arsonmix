// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/PreparedMixerBlock.h"
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
        // Preserve explicit LAB ownership across hotplug; the new epoch
        // makes the old prepared graph return silence, NEVER CH1 fallthrough.
        const auto generation=deviceEpoch_.fetch_add(1,std::memory_order_acq_rel)+1;
        registryValid_=false;
        enabledPorts_.fill(-1);
        portCount_=0;
        if(!device) return;
        const auto mask=device->getActiveInputChannels();
        int bit=mask.findNextSetBit(0);
        while(bit>=0) {
            if(portCount_>=armix::device::LiveInputPortRegistry::limit)
                return; // no silent truncation
            enabledPorts_[portCount_++]=bit;
            bit=mask.findNextSetBit(bit+1);
        }
        armix::device::LiveInputPortRegistry::Fault fault;
        registryValid_=armix::device::LiveInputPortRegistry::prepare(
            generation,{enabledPorts_.data(),static_cast<std::size_t>(portCount_)},
            registry_,fault);
        callbackInputCount_.store(portCount_,std::memory_order_release);
    }
    void deviceStopped() noexcept {
        // Epoch invalidates output while LAB mode remains fail-closed until
        // the operator explicitly disables it or applies a new device map.
        deviceEpoch_.fetch_add(1,std::memory_order_acq_rel);
        callbackInputCount_.store(-1,std::memory_order_release);
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
        if(!registryValid_ || portCount_==0)
            return "Audio device has no validated active capture ports";
        if(!command.isObject()) return "Invalid Dry Routing configuration";
        const auto currentEpoch=deviceEpoch_.load(std::memory_order_acquire);
        const auto expectedEpoch=static_cast<std::uint64_t>(
            static_cast<juce::int64>(command.getProperty("deviceEpoch",juce::var(0))));
        const auto expectedRevision=static_cast<std::uint64_t>(
            static_cast<juce::int64>(command.getProperty("revision",juce::var(0))));
        if(currentEpoch==0 || expectedEpoch!=currentEpoch)
            return "Audio device changed: reopen ROUTING to remap physical ports";
        const auto values=command.getProperty("channels",{});
        auto* assignments=values.getArray();
        if(!assignments || assignments->size()!=count)
            return "Supply exactly 10 explicit channel assignments";
        armix::mixer::ChannelRoutingConfig cfg;
        for(int i=0;i<count;++i) {
            const auto item=assignments->getReference(i);
            if(!item.isObject())return "Invalid channel assignment";
            const auto mode=item.getProperty("mode",{}).toString();
            const auto left=item.getProperty("left",-1);
            const auto right=item.getProperty("right",-1);
            const auto gain=item.getProperty("gain",0.25);
            if(!left.isInt()||!right.isInt()||!(gain.isDouble()||gain.isInt()))
                return "Invalid input port number or channel level";
            const float level=static_cast<float>(static_cast<double>(gain));
            if(!std::isfinite(level)||level<0.f||level>1.f)
                return "Channel level must be 0..1";
            auto& c=cfg.stripsAndMaster.strips[i];
            c.faderLinear=level;
            c.pan=0.f;
            if(mode=="NONE") {
                c.source=armix::mixer::SourceMode::disconnected;
                continue;
            }
            if(mode=="MONO") {
                c.source=armix::mixer::SourceMode::mono;
                c.inputA=static_cast<int>(left);
                cfg.formats[i]=armix::mixer::ChannelFormat::mono;
            } else if(mode=="STEREO") {
                c.source=armix::mixer::SourceMode::stereo;
                c.inputA=static_cast<int>(left);
                c.inputB=static_cast<int>(right);
                cfg.formats[i]=armix::mixer::ChannelFormat::stereo;
            } else return "Channel format must be NONE, MONO or STEREO";
        }
        armix::mixer::SendRequest sends; // hardware dry FAT, no FX/send fabric yet
        Plan plan;
        armix::mixer::RenderError error;
        if(!Plan::compile(cfg,
                          {enabledPorts_.data(),static_cast<std::size_t>(portCount_)},
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
        const auto result=publisher_.publish(std::move(next),expectedRevision,currentEpoch);
        switch(result) {
            case Publisher::Result::applied:
                enabled_.store(true,std::memory_order_release);
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
        publisher_.unpublish(); // control thread only
        peakL_.store(0.f,std::memory_order_relaxed);
        peakR_.store(0.f,std::memory_order_relaxed);
    }
    void poll() noexcept {publisher_.pollRetirement();} // message thread
    bool active() const noexcept {return enabled_.load(std::memory_order_acquire);}
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
        if(frames<=0)return true;
        for(int c=0;c<outputCount;++c)
            if(outputs && outputs[c])
                juce::FloatVectorOperations::clear(outputs[c],frames);
        if(!outputs||outputCount<2||!outputs[0]||!outputs[1] ||
           inputCount<0 || inputCount>armix::device::LiveInputPortRegistry::limit)
            return true;
        const auto generation=epoch();
        auto lease=publisher_.acquire();
        const auto* snapshot=lease.get(generation);
        if(!snapshot||!snapshot->prepared ||
           inputCount!=callbackInputCount_.load(std::memory_order_acquire))
            return true;
        // Large/variable JUCE quantum is chunked using stack pointer views;
        // no heap allocation or new processing object inside callback.
        std::array<const float*,armix::device::LiveInputPortRegistry::limit> shifted{};
        constexpr int max=Block::hardFrameLimit;
        const auto noFx=[](int,float*,float*,int) noexcept {};
        float peakL=0.f,peakR=0.f;
        for(int offset=0;offset<frames;offset+=max) {
            const int n=std::min(max,frames-offset);
            for(int c=0;c<inputCount;++c)
                shifted[c]=inputs&&inputs[c]?inputs[c]+offset:nullptr;
            armix::mixer::RenderLevels level;
            if(!snapshot->prepared->process(shifted.data(),inputCount,
                   outputs[0]+offset,outputs[1]+offset,n,noFx,&level))
                return true;
            const float g=masterMuted?0.f:std::clamp(masterGain,0.f,2.f);
            for(int i=0;i<n;++i){
                outputs[0][offset+i]*=g;
                outputs[1][offset+i]*=g;
            }
            peakL=std::max(peakL,level.masterPeakL*g);
            peakR=std::max(peakR,level.masterPeakR*g);
        }
        peakL_.store(peakL,std::memory_order_relaxed);
        peakR_.store(peakR,std::memory_order_relaxed);
        return true;
    }

    juce::var status(juce::AudioIODevice* device) const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("epoch",static_cast<juce::int64>(epoch()));
        out->setProperty("revision",static_cast<juce::int64>(revision()));
        out->setProperty("peakL",peakL_.load(std::memory_order_relaxed));
        out->setProperty("peakR",peakR_.load(std::memory_order_relaxed));
        juce::Array<juce::var> ports;
        if(device && registryValid_) {
            const auto names=device->getInputChannelNames();
            for(int i=0;i<portCount_;++i){
                const int physical=enabledPorts_[i];
                auto p=std::make_unique<juce::DynamicObject>();
                p->setProperty("port",physical);
                p->setProperty("callback",i);
                p->setProperty("label",physical<names.size()
                    ?names[physical]:juce::String("Input ")+juce::String(physical+1));
                ports.add(juce::var(p.release()));
            }
        }
        out->setProperty("ports",juce::var(ports));
        out->setProperty("note","EXPLICIT DRY HARDWARE FAT ONLY: no channel VST3, no sends, no portable route persistence");
        return juce::var(out.release());
    }
private:
    Publisher publisher_;
    armix::device::LiveInputPortRegistry registry_; // device lifecycle only
    std::array<int,armix::device::LiveInputPortRegistry::limit> enabledPorts_{};
    int portCount_=0;
    bool registryValid_=false;
    std::atomic<std::uint64_t> deviceEpoch_{0};
    std::atomic<int> callbackInputCount_{-1};
    std::atomic<bool> enabled_{false};
    std::atomic<float> peakL_{0.f},peakR_{0.f};
};
