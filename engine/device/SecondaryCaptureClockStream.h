// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AdaptiveClockBridge.h"
#include "ClockBridgeSizing.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
    // Independent atomic admission gates avoid A/B clock callback contention.
    static constexpr std::uint32_t writerFlag=1U,readerUnit=2U;
    struct Format
    {
        double captureHz=0.,primaryHz=0.;
        int capturePeriod=0,primaryPeriod=0,channels=0;
        std::uint64_t captureGeneration=0,primaryEpoch=0,deviceLease=0;
    };
    struct Status
    {
        bool ready=false,faulted=false;
        std::uint64_t captureBlocks=0,primaryBlocks=0,invalidBlocks=0,staleReads=0;
        AdaptiveClockBridge::Status asrc{};
    };

    bool prepare(const Format& format,double desiredTargetMs=24.)
    {
        if(ready_.load(std::memory_order_acquire))return false;
        // Faulted callbacks may still be in flight even with ready=false.
        // CONTROL closes BOTH entry gates and drains old leases before ASRC.
        stop(); // leaves both writer bits held until activate()
        // CONTROL owns the entire format and bridge reprepare interval.
        fault_.store(false,std::memory_order_release);
        // A previous successfully prepared (but not activated) stream must
        // never survive an invalid replacement request. activate() may not
        // re-arm a stale bridge after ANY prepare failure.
        prepared_=false;
        format_={};targetMs_=0.;
        if(format.captureGeneration==0||format.primaryEpoch==0||
           format.deviceLease==0||
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
        // Complete physical format + FIR initialization before opening RT.
        producerActivity_.fetch_and(~writerFlag,std::memory_order_seq_cst);
        consumerActivity_.fetch_and(~writerFlag,std::memory_order_seq_cst);
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
        // Set BOTH writer bits BEFORE observing in-flight borrow counts.
        // Each CAS-admitted producer/consumer must retire before resetStopped.
        producerActivity_.fetch_or(writerFlag,std::memory_order_seq_cst);
        consumerActivity_.fetch_or(writerFlag,std::memory_order_seq_cst);
        ready_.store(false,std::memory_order_release);
        while((producerActivity_.load(std::memory_order_seq_cst)&~writerFlag)!=0||
              (consumerActivity_.load(std::memory_order_seq_cst)&~writerFlag)!=0)
            std::this_thread::yield(); // CONTROL only, never RT
        prepared_=false;
        fault_.store(false,std::memory_order_release);
        bridge_.resetStopped(); // no live producer or consumer
        format_={};targetMs_=0.;
    }

    bool push(const float* const* capture,int channels,int frames) noexcept
    {
        // One nonblocking CAS; no spin/lock/device call on B callback.
        auto state=producerActivity_.load(std::memory_order_seq_cst);
        if((state&writerFlag)!=0 ||
           state>std::numeric_limits<std::uint32_t>::max()-readerUnit ||
           !producerActivity_.compare_exchange_strong(
                state,state+readerUnit,
                std::memory_order_seq_cst,std::memory_order_seq_cst))
            return false;
        const bool enabled=ready_.load(std::memory_order_acquire)&&
            !fault_.load(std::memory_order_acquire);
        if(!enabled){producerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);return false;}
        if(!capture||channels!=format_.channels||frames<1||
           frames>maxCallbackFrames)
        {
            invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
            fault();
            producerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
            return false;
        }
        for(int c=0;c<channels;++c)
            if(!capture[c])
            {
                invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
                fault();
                producerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
                return false;
            }
        captureBlocks_.fetch_add(1,std::memory_order_relaxed);
        // AdaptiveClockBridge drops NEWEST frames on overflow; no RT wait.
        const bool accepted=bridge_.push(capture,channels,frames)==frames;
        producerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
        return accepted;
    }

    bool pull(float* const* output,int channels,int frames,
              std::uint64_t captureGeneration,std::uint64_t primaryEpoch,
              std::uint64_t deviceLease) noexcept
    {
        // Independent A render gate: no producer-vs-consumer CAS collision.
        auto state=consumerActivity_.load(std::memory_order_seq_cst);
        if((state&writerFlag)!=0 ||
           state>std::numeric_limits<std::uint32_t>::max()-readerUnit ||
           !consumerActivity_.compare_exchange_strong(
                state,state+readerUnit,
                std::memory_order_seq_cst,std::memory_order_seq_cst))
            return false;
        const bool valid=output&&channels>0&&channels<=16&&frames>0&&
            frames<=maxCallbackFrames;
        if(!valid)
        {
            consumerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
            return false;
        }
        for(int c=0;c<channels;++c)
            if(!output[c])
            {
                invalidBlocks_.fetch_add(1,std::memory_order_relaxed);
                fault();
                consumerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
                return false;
            }
        const bool authorised=ready_.load(std::memory_order_acquire)&&
            !fault_.load(std::memory_order_acquire)&&
            captureGeneration==format_.captureGeneration&&
            primaryEpoch==format_.primaryEpoch&&
            deviceLease==format_.deviceLease&&
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
        consumerActivity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
        return authorised;
    }

    bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire)&&
               !fault_.load(std::memory_order_acquire)&&
               !(producerActivity_.load(std::memory_order_seq_cst)&writerFlag)&&
               !(consumerActivity_.load(std::memory_order_seq_cst)&writerFlag);
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
    std::atomic<std::uint32_t> producerActivity_{writerFlag},
                               consumerActivity_{writerFlag};
    std::atomic<std::uint64_t> captureBlocks_{0},primaryBlocks_{0},
                               invalidBlocks_{0},staleReads_{0};
};
} // namespace armix::device
