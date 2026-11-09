// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

// ArSon Player device-rate stereo PCM bridge. Exactly ONE decoding producer
// and ONE render callback consumer. No file I/O, heap, locks, waits, sample
// rate conversion or time stretch in read(); graph fan-out uses the caller's
// single borrowed read() block rather than one FIFO read per mixer channel.
// Device restart or song-generation swap must retire this object OFF realtime
// after the producer and consumer have both stopped using it.
namespace armix::player {
class PreparedStereoPcmRing final {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "Realtime stereo ring requires lock-free 64-bit cursors");
public:
    PreparedStereoPcmRing()=default;
    PreparedStereoPcmRing(const PreparedStereoPcmRing&)=delete;
    PreparedStereoPcmRing& operator=(const PreparedStereoPcmRing&)=delete;
    PreparedStereoPcmRing(PreparedStereoPcmRing&&)=delete;
    PreparedStereoPcmRing& operator=(PreparedStereoPcmRing&&)=delete;

    // CONTROL ONLY before the two threads begin, or after quiescence.
    bool prepare(std::size_t frames) {
        if(frames<2 || frames>(1U<<22))return false;
        auto left=std::make_unique<float[]>(frames);
        auto right=std::make_unique<float[]>(frames);
        left_=std::move(left);
        right_=std::move(right);
        capacity_=frames;
        readFrame_.store(0,std::memory_order_relaxed);
        writeFrame_.store(0,std::memory_order_relaxed);
        underrunFrames_.store(0,std::memory_order_relaxed);
        return true;
    }

    std::size_t capacityFrames() const noexcept {return capacity_;}
    std::uint64_t underrunFrames() const noexcept {
        return underrunFrames_.load(std::memory_order_relaxed);
    }
    // WORKER ONLY. Full ring returns 0; never overwrites unread sound.
    std::size_t write(const float* inputL,const float* inputR,
                      std::size_t frames) noexcept {
        if(!capacity_ || !inputL || !inputR || !frames)return 0;
        const auto write=writeFrame_.load(std::memory_order_relaxed);
        const auto read=readFrame_.load(std::memory_order_acquire);
        const auto occupied=write-read;
        if(occupied>capacity_)return 0; // invariant violation fails closed
        const auto n=std::min(frames,capacity_-static_cast<std::size_t>(occupied));
        const auto first=std::min(n,capacity_-static_cast<std::size_t>(write%capacity_));
        std::copy_n(inputL,first,left_.get()+(write%capacity_));
        std::copy_n(inputR,first,right_.get()+(write%capacity_));
        std::copy_n(inputL+first,n-first,left_.get());
        std::copy_n(inputR+first,n-first,right_.get());
        writeFrame_.store(write+n,std::memory_order_release);
        return n;
    }

    // AUDIO CALLBACK ONLY. Returns actual frames; the rest are SILENCE.
    // The mixer/ASIO clock never waits for a decoder or disk stall.
    std::size_t read(float* outputL,float* outputR,
                     std::size_t frames) noexcept {
        if(!outputL || !outputR || outputL==outputR)return 0;
        const auto read=readFrame_.load(std::memory_order_relaxed);
        const auto write=writeFrame_.load(std::memory_order_acquire);
        const auto available=write-read;
        const auto n=capacity_&&available<=capacity_
            ?std::min(frames,static_cast<std::size_t>(available)):0;
        if(n) {
            const auto first=std::min(n,capacity_-static_cast<std::size_t>(read%capacity_));
            std::copy_n(left_.get()+(read%capacity_),first,outputL);
            std::copy_n(right_.get()+(read%capacity_),first,outputR);
            std::copy_n(left_.get(),n-first,outputL+first);
            std::copy_n(right_.get(),n-first,outputR+first);
            readFrame_.store(read+n,std::memory_order_release);
        }
        std::fill_n(outputL+n,frames-n,0.f);
        std::fill_n(outputR+n,frames-n,0.f);
        if(n<frames)underrunFrames_.fetch_add(frames-n,std::memory_order_relaxed);
        return n;
    }
    // Approximate worker scheduling metric (not a cross-thread transaction).
    std::size_t bufferedFrames() const noexcept {
        const auto read=readFrame_.load(std::memory_order_acquire);
        const auto write=writeFrame_.load(std::memory_order_acquire);
        const auto delta=write-read;
        return delta<=capacity_?static_cast<std::size_t>(delta):0;
    }
private:
    std::unique_ptr<float[]> left_,right_;
    std::size_t capacity_=0;
    alignas(64) std::atomic<std::uint64_t> readFrame_{0};
    alignas(64) std::atomic<std::uint64_t> writeFrame_{0};
    alignas(64) std::atomic<std::uint64_t> underrunFrames_{0};
};
} // namespace armix::player
