// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "MultiDeviceInputPlan.h"
#include "SecondaryCaptureClockStream.h"
#include "../core/RealtimeRenderPermit.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

// IO-2C: one primary callback consuming ONE independent capture clock (B).
// This only assembles qualified borrowed PCM for the existing native graph;
// it NEVER renders FX, owns hardware, creates a callback, or publishes a
// second source of truth. The operator-verified map is published separately
// in the SAME native PCM/FX snapshot by DynamicNativeSessionPublisher.
//
// prepare/stop: CONTROL ONLY, after output disarm and callback ownership gate.
// withBlocks: PRIMARY callback ONLY. Buffer/pointer lifetime spans the entire
// caller's render invocation. Future N-device support must extend this bounded
// writer/reader ownership explicitly; sharing one SPSC between producers is
// prohibited.
namespace armix::device {

class QualifiedSecondaryCaptureReader final {
public:
    using CaptureBlock=MultiDeviceInputPlan::CaptureBlock;
    static constexpr int maxSecondaryChannels=16;
    static constexpr int maxCallbackFrames=8192;
    // One seq-cst atomic for writer admission AND bounded callback leases.
    static constexpr std::uint32_t writerFlag=1U,readerUnit=2U;
    struct Status {
        bool ready=false;
        std::uint64_t primaryBlocks=0,secondaryAccepted=0,secondarySilent=0,
                      rejected=0,overlaps=0;
    };

    QualifiedSecondaryCaptureReader()=default;
    QualifiedSecondaryCaptureReader(const QualifiedSecondaryCaptureReader&)=delete;
    QualifiedSecondaryCaptureReader& operator=(const QualifiedSecondaryCaptureReader&)=delete;

    // CONTROL: allocate once with exact size, never on the primary callback.
    // The native host must detach/disarm before modifying this adapter.
    bool prepare(const MultiDeviceInputPlan& qualified,int maximumFrames) {
        stop();
        if(qualified.primaryEpoch()==0||qualified.deviceCount()!=2||
           maximumFrames<=0||maximumFrames>maxCallbackFrames||
           !qualified.device(0).primary||qualified.device(1).primary ||
           qualified.device(0).generation!=qualified.primaryEpoch()||
           qualified.device(0).deviceLease==0||qualified.device(1).deviceLease==0||
           qualified.device(0).deviceLease==qualified.device(1).deviceLease||
           qualified.device(0).activePins<1||
           qualified.device(1).activePins<1||
           qualified.device(1).activePins>maxSecondaryChannels)return false;
        const int n=qualified.device(1).activePins;
        const auto total=static_cast<std::size_t>(n)*
                         static_cast<std::size_t>(maximumFrames);
        std::unique_ptr<float[]> newScratch{new(std::nothrow) float[total]{}};
        if(!newScratch)return false;
        scratch_=std::move(newScratch);
        epoch_=qualified.primaryEpoch();
        primaryGeneration_=qualified.device(0).generation;
        primaryLease_=qualified.device(0).deviceLease;
        secondaryGeneration_=qualified.device(1).generation;
        secondaryLease_=qualified.device(1).deviceLease;
        primaryChannels_=qualified.device(0).activePins;
        secondaryChannels_=n;
        maximumFrames_=maximumFrames;
        for(int c=0;c<n;++c){
            auto* channel=scratch_.get()+
                static_cast<std::size_t>(c)*static_cast<std::size_t>(maximumFrames);
            write_[c]=channel;read_[c]=channel;
        }
        primaryBlocks_.store(0,std::memory_order_relaxed);
        secondaryAccepted_.store(0,std::memory_order_relaxed);
        secondarySilent_.store(0,std::memory_order_relaxed);
        rejected_.store(0,std::memory_order_relaxed);
        overlaps_.store(0,std::memory_order_relaxed);
        ready_.store(true,std::memory_order_release);
        // stop() keeps the writer bit held until every scratch pointer and
        // physical lease is initialized, then readers may enter atomically.
        activity_.fetch_and(~writerFlag,std::memory_order_seq_cst);
        return true;
    }

    // CONTROL ONLY, never called inside either audio callback.
    // Prevent reprepare/free while a borrowed scratch view is rendering.
    void stop() noexcept {
        // Atomically close admission before examining reader count.
        // A reader that won the CAS must finish; new arrivals reject.
        activity_.fetch_or(writerFlag,std::memory_order_seq_cst);
        ready_.store(false,std::memory_order_release);
        while((activity_.load(std::memory_order_seq_cst)&~writerFlag)!=0)
            std::this_thread::yield(); // CONTROL only, never audio callback
        scratch_.reset();
        write_={};read_={};
        epoch_=primaryGeneration_=primaryLease_=0;
        secondaryGeneration_=secondaryLease_=0;
        primaryChannels_=secondaryChannels_=maximumFrames_=0;
    }

    // PRIMARY callback only. Render owns the view synchronously and returns
    // before scratch can be reused; a broken overlapping driver fails silent.
    // The caller MUST silence physical outputs on a false return.
    template<class Render>
    bool withBlocks(std::uint64_t primaryEpoch,
                    const float* const* primary,int primaryChannels,
                    int frames,SecondaryCaptureClockStream& secondary,
                    Render&& render) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool,Render,
                   std::span<const CaptureBlock>>,
                   "Render must synchronously consume blocks without throwing");
        // Single non-blocking compare-exchange; no audio-thread spin.
        // Counter and stop/admission flag share one linearizable state.
        auto activity=activity_.load(std::memory_order_seq_cst);
        if((activity&writerFlag)!=0 ||
           activity>std::numeric_limits<std::uint32_t>::max()-readerUnit ||
           !activity_.compare_exchange_strong(activity,activity+readerUnit,
               std::memory_order_seq_cst,std::memory_order_seq_cst)) {
            rejected_.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        const bool admitted=ready_.load(std::memory_order_acquire) &&
            scratch_ && primaryEpoch==epoch_ &&
            primaryChannels==primaryChannels_ &&
            frames>0&&frames<=maximumFrames_ &&
            (primaryChannels==0||primary!=nullptr);
        if(!admitted) {
            rejected_.fetch_add(1,std::memory_order_relaxed);
            activity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
            return false;
        }
        armix::rt::RealtimeRenderPermit exclusive{consumer_};
        if(!exclusive){
            overlaps_.fetch_add(1,std::memory_order_relaxed);
            activity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
            return false;
        }
        primaryBlocks_.fetch_add(1,std::memory_order_relaxed);
        // Separate SPSC: B's real device callback pushes at capture rate;
        // this primary callback is the sole FIFO consumer after ASRC.
        const bool bReady=secondary.pull(write_.data(),secondaryChannels_,
            frames,secondaryGeneration_,primaryEpoch,secondaryLease_);
        if(bReady)secondaryAccepted_.fetch_add(1,std::memory_order_relaxed);
        else secondarySilent_.fetch_add(1,std::memory_order_relaxed);
        const std::array<CaptureBlock,2> blocks{{
            {primary,primaryChannels,frames,primaryGeneration_,true,primaryLease_},
            {read_.data(),secondaryChannels_,frames,secondaryGeneration_,
                bReady,secondaryLease_}
        }};
        // The receiver must use the SAME snapshot-owned input map/graph as
        // this primary callback; it cannot hold this view after return.
        const bool ok=std::forward<Render>(render)(
            std::span<const CaptureBlock>(blocks.data(),blocks.size()));
        activity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
        return ok;
    }

    Status status() const noexcept {
        return {ready_.load(std::memory_order_acquire) &&
                    !(activity_.load(std::memory_order_seq_cst)&writerFlag),
            primaryBlocks_.load(std::memory_order_relaxed),
            secondaryAccepted_.load(std::memory_order_relaxed),
            secondarySilent_.load(std::memory_order_relaxed),
            rejected_.load(std::memory_order_relaxed),
            overlaps_.load(std::memory_order_relaxed)};
    }

private:
    // No reader can remain after stop's CONTROL quiescence barrier.
    // Same gate as RealtimeRoutePublisher: writer=bit0, each reader=+2.
    std::unique_ptr<float[]> scratch_;
    std::array<float*,maxSecondaryChannels> write_{};
    std::array<const float*,maxSecondaryChannels> read_{};
    std::uint64_t epoch_=0,primaryGeneration_=0,primaryLease_=0,
                  secondaryGeneration_=0,secondaryLease_=0;
    int primaryChannels_=0,secondaryChannels_=0,maximumFrames_=0;
    std::atomic<bool> ready_{false};
    std::atomic<std::uint32_t> activity_{writerFlag};
    std::atomic_flag consumer_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> primaryBlocks_{0},secondaryAccepted_{0},
                              secondarySilent_{0},rejected_{0},overlaps_{0};
};
} // namespace armix::device
