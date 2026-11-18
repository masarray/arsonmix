// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AdaptiveClockBridge.h"
#include "ClockBridgeSizing.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>

namespace armix::device
{
// IO-1: one independently clocked CAPTURE producer -> one PRIMARY render
// consumer. Reuses the production bounded AdaptiveClockBridge SPSC/ASRC;
// NO mixer, VST3, output device, callback owner or audio heap allocation.
// prepare/activate/stop: CONTROL. push: ONLY the device-B callback.
// pull: ONLY the primary device-A callback, after explicit source admission.
// No session/physical DeviceKey -> ChannelId binding is claimed until IO-2.
class SecondaryCaptureClockStream final
{
public:
    static constexpr int capacityFrames=32768;
    static constexpr int maxCallbackFrames=8192;
    struct Format
    {
        double captureHz=0.,primaryHz=0.;
        int capturePeriod=0,primaryPeriod=0,channels=0;
        std::uint64_t captureGeneration=0,primaryEpoch=0;
    };
    struct Status
    {
        bool ready=false,faulted=false;
        std::uint64_t captureBlocks=0,primaryBlocks=0,invalidBlocks=0,staleReads=0;
        AdaptiveClockBridge::Status asrc{};
    };

    bool prepare(const Format& format,double desiredTargetMs=24.)
    {
        if(ready_.load(std::memory_order_acquire)||
           producers_.load(std::memory_order_acquire)!=0||
           consumers_.load(std::memory_order_acquire)!=0)
            return false;
        // CONTROL owns the entire format and bridge reprepare interval.
        ready_.store(false,std::memory_order_release);
        fault_.store(false,std::memory_order_release);
        format_={};targetMs_=0.;
        if(format.captureGeneration==0||format.primaryEpoch==0||
           format.channels<1||format.channels>16||
           format.capturePeriod<1||format.capturePeriod>maxCallbackFrames||
           format.primaryPeriod<1||format.primaryPeriod>maxCallbackFrames)
            return false;
        const auto sizing=ClockBridgeSizing::choose(
            format.captureHz,format.primaryHz,format.capturePeriod,
            format.primaryPeriod,AdaptiveClockBridge::taps,
            capacityFrames,desiredTargetMs);
        if(!sizing.valid||!bridge_.prepare(format.captureHz,format.primaryHz,
                 format.channels,capacityFrames,sizing.targetMs))
            return false;
        format_=format;targetMs_=sizing.targetMs;
        captureBlocks_.store(0,std::memory_order_relaxed);
        primaryBlocks_.store(0,std::memory_order_relaxed);
        invalidBlocks_.store(0,std::memory_order_relaxed);
        staleReads_.store(0,std::memory_order_relaxed);
        prepared_=true;
        return true;
    }

    bool activate() noexcept
    {
        if(!prepared_||fault_.load(std::memory_order_acquire))
            return false;
        ready_.store(true,std::memory_order_release);
        return true;
    }

    // Fail an independent capture without disturbing PRIMARY playback.
    void fault() noexcept
    {
        fault_.store(true,std::memory_order_release);
        ready_.store(false,std::memory_order_release);
    }

    // Device callbacks must be DETACHED before CONTROL calls stop().
    // A concurrently finishing primary pull is held by a short RT lease.
    // Never call this or yield on either audio callback.
    void stop() noexcept
    {
        ready_.store(false,std::memory_order_release);
        while(producers_.load(std::memory_order_acquire)!=0||
              consumers_.load(std::memory_order_acquire)!=0)
            std::this_thread::yield();
        prepared_=false;
        fault_.store(false,std::memory_order_release);
        bridge_.resetStopped(); // no live producer or consumer
        format_={};targetMs_=0.;
    }

    bool push(const float* const* capture,int channels,int frames) noexcept
    {
        producers_.fetch_add(1,std::memory_order_acq_rel);
        const bool enabled=ready_.load(std::memory_order_acquire)&&
            !fault_.load(std::memory_order_acquire);
        if(!enabled){producers_.fetch_sub(1,std::memory_order_release);return false;}
        if(!capture||channels!=format_.channels||frames<1||
           frames>maxCallbackFrames)
        {
            invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
            fault();
            producers_.fetch_sub(1,std::memory_order_release);
            return false;
        }
        for(int c=0;c<channels;++c)
            if(!capture[c])
            {
                invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
                fault();
                producers_.fetch_sub(1,std::memory_order_release);
                return false;
            }
        captureBlocks_.fetch_add(1,std::memory_order_relaxed);
        // AdaptiveClockBridge drops NEWEST frames on overflow; no RT wait.
        const bool accepted=bridge_.push(capture,channels,frames)==frames;
        producers_.fetch_sub(1,std::memory_order_release);
        return accepted;
    }

    bool pull(float* const* output,int channels,int frames,
              std::uint64_t captureGeneration,std::uint64_t primaryEpoch) noexcept
    {
        consumers_.fetch_add(1,std::memory_order_acq_rel);
        const bool valid=output&&channels>0&&channels<=16&&frames>0&&
            frames<=maxCallbackFrames;
        if(!valid)
        {
            consumers_.fetch_sub(1,std::memory_order_release);
            return false;
        }
        for(int c=0;c<channels;++c)
            if(!output[c])
            {
                invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
                fault();
                consumers_.fetch_sub(1,std::memory_order_release);
                return false;
            }
        const bool authorised=ready_.load(std::memory_order_acquire)&&
            !fault_.load(std::memory_order_acquire)&&
            captureGeneration==format_.captureGeneration&&
            primaryEpoch==format_.primaryEpoch&&
            channels==format_.channels;
        if(authorised)
        {
            primaryBlocks_.fetch_add(1,std::memory_order_relaxed);
            bridge_.pull(output,channels,frames);
        }
        else
        {
            staleReads_.fetch_add(1,std::memory_order_relaxed);
            for(int c=0;c<channels;++c)
                std::fill_n(output[c],frames,0.f);
        }
        consumers_.fetch_sub(1,std::memory_order_release);
        return authorised;
    }

    bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire)&&
               !fault_.load(std::memory_order_acquire);
    }
    bool faulted() const noexcept{return fault_.load(std::memory_order_acquire);}
    const Format& format() const noexcept{return format_;} // CONTROL ONLY
    double targetMs() const noexcept{return targetMs_;} // CONTROL ONLY
    Status status() const noexcept
    {
        return {ready(),faulted(),
            captureBlocks_.load(std::memory_order_relaxed),
            primaryBlocks_.load(std::memory_order_relaxed),
            invalidBlocks_.load(std::memory_order_relaxed),
            staleReads_.load(std::memory_order_relaxed),
            bridge_.status()};
    }

private:
    AdaptiveClockBridge bridge_;
    Format format_{}; // immutable between prepare and stopped callback leases
    double targetMs_=0.;
    bool prepared_=false; // CONTROL
    std::atomic<bool> ready_{false},fault_{false};
    std::atomic<unsigned> producers_{0},consumers_{0};
    std::atomic<std::uint64_t> captureBlocks_{0},primaryBlocks_{0},
                               invalidBlocks_{0},staleReads_{0};
};
} // namespace armix::device
