// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMasterLoudness.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <memory>
#include <thread>

namespace armix::dsp {
// Analysis-only SPSC queue. Never feeds a speaker, plugin or render clock.
// One producer (device render), one consumer (analysis worker). Device
// lifecycle changes the generation, never resets indices under the consumer.
template<unsigned Capacity=512, int BlockFrames=256>
class MasterAnalysisQueue final {
public:
    static_assert(Capacity>1 && (Capacity&(Capacity-1))==0);
    static_assert(BlockFrames>0);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    struct Block {
        std::uint64_t generation=0;
        int channels=0,frames=0;
        std::array<std::array<float,BlockFrames>,2> pcm{};
    };
    bool push(std::uint64_t generation,const float* const* pcm,int channels,int frames) noexcept {
        if(channels<1 || channels>2 || frames<1 || frames>BlockFrames)return false;
        const auto write=write_.load(std::memory_order_relaxed);
        if(write-read_.load(std::memory_order_acquire)>=Capacity)return false;
        auto& b=blocks_[write&(Capacity-1)];
        b.generation=generation;b.channels=channels;b.frames=frames;
        for(int c=0;c<channels;++c) {
            if(pcm && pcm[c])std::copy_n(pcm[c],frames,b.pcm[c].data());
            else std::fill_n(b.pcm[c].data(),frames,0.f);
        }
        write_.store(write+1,std::memory_order_release);
        return true;
    }
    template<class Consume>
    bool consume(Consume&& fn) noexcept {
        const auto read=read_.load(std::memory_order_relaxed);
        if(read==write_.load(std::memory_order_acquire))return false;
        fn(static_cast<const Block&>(blocks_[read&(Capacity-1)]));
        read_.store(read+1,std::memory_order_release);
        return true;
    }
    unsigned pending() const noexcept {
        const auto read=read_.load(std::memory_order_acquire);
        const auto write=write_.load(std::memory_order_acquire);
        return static_cast<unsigned>(std::min<std::uint64_t>(write-read,Capacity));
    }
private:
    std::array<Block,Capacity> blocks_{};
    alignas(64) std::atomic<std::uint64_t> write_{0};
    alignas(64) std::atomic<std::uint64_t> read_{0};
};

// Exactly the previous meter algorithm, on a non-realtime worker. No
// callback mutex, wake syscall, waiting, heap, DSP or UI notification.
// A gap invalidates the whole integrated segment (no fake continuous LUFS).
// Recovery begins a new, explicitly numbered analysis segment after the
// queue drains. It never changes the audio device or playback state.
class NativeMasterAnalysisTap final {
public:
    using Queue=MasterAnalysisQueue<>;
    using Clock=std::chrono::steady_clock;
    enum class State { unsupported, workerUnavailable, warming, measured, stale, gap };
    struct Reading : NativeMasterLoudness::Reading {
        State state=State::unsupported;
        std::uint64_t generation=0,processedFrames=0,droppedFramesLifetime=0,segmentRestartsLifetime=0;
        unsigned pendingBlocks=0;
    };
    NativeMasterAnalysisTap() {
        try {queue_=std::make_unique<Queue>();worker_=std::thread([this]{run();});available_=true;}
        catch(...) {available_=false;} // audio remains available without analysis
    }
    ~NativeMasterAnalysisTap() {
        stop_.store(true,std::memory_order_release);
        if(worker_.joinable())worker_.join();
    }
    NativeMasterAnalysisTap(const NativeMasterAnalysisTap&)=delete;
    NativeMasterAnalysisTap& operator=(const NativeMasterAnalysisTap&)=delete;

    // Device lifecycle only, serialized with the render producer.
    // Low bit is the exact supported-rate flag; high bits are epoch.
    void prepare(double rate) noexcept {
        const auto old=generation_.load(std::memory_order_relaxed);
        const bool supported=std::isfinite(rate)&&std::abs(rate-48000.0)<=0.5;
        generation_.store(((old>>1)+1)*2+(supported?1:0),std::memory_order_release);
    }
    void publish(const float* const* output,int channels,int frames) noexcept {
        auto gen=generation_.load(std::memory_order_acquire);
        if(!available_ || !(gen&1) || channels<1 || frames<=0)return;
        if(gapGeneration_.load(std::memory_order_relaxed)==gen){
            if(queue_->pending()!=0){
                dropped_.store(dropped_.load(std::memory_order_relaxed)+
                    static_cast<std::uint64_t>(frames),std::memory_order_relaxed);
                return;
            }
            // The producer and device lifecycle are serialized; advance
            // only the analysis generation. A new segment cannot inherit
            // a prior integrated measurement or restart the sound device.
            gen+=2;
            restarts_.store(restarts_.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
            generation_.store(gen,std::memory_order_release);
        }
        const int count=std::min(channels,2);
        for(int offset=0;offset<frames;) {
            const int n=std::min(256,frames-offset);
            const float* pcm[2]{output&&output[0]?output[0]+offset:nullptr,
                count>1&&output&&output[1]?output[1]+offset:nullptr};
            if(!queue_->push(gen,pcm,count,n)) {
                dropped_.store(dropped_.load(std::memory_order_relaxed)+
                    static_cast<std::uint64_t>(frames-offset),std::memory_order_relaxed);
                gapGeneration_.store(gen,std::memory_order_release);
                return; // drop analysis, never delay or alter audio
            }
            offset+=n;
        }
    }
    // CONTROL only. The worker publishes one small coherent reading. No
    // audio producer ever acquires this mutex or reads the analyzer state.
    Reading read() const {
        const auto gen=generation_.load(std::memory_order_acquire);
        Reading out;
        out.generation=gen>>1;
        out.pendingBlocks=queue_?queue_->pending():0;
        out.droppedFramesLifetime=dropped_.load(std::memory_order_relaxed);
        out.segmentRestartsLifetime=restarts_.load(std::memory_order_relaxed);
        out.supported=(gen&1)!=0;
        if(!out.supported)return out;
        if(!available_){out.state=State::workerUnavailable;return out;}
        if(gapGeneration_.load(std::memory_order_acquire)==gen){out.state=State::gap;return out;}
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        if(gen!=generation_.load(std::memory_order_acquire))return {};
        if(gapGeneration_.load(std::memory_order_acquire)==gen){out.state=State::gap;return out;}
        if(snapshotGeneration_!=gen || !snapshotFrames_){out.state=State::warming;return out;}
        out.processedFrames=snapshotFrames_;
        if(Clock::now()-snapshotAt_>std::chrono::milliseconds(250)){
            out.state=State::stale;return out;
        }
        static_cast<NativeMasterLoudness::Reading&>(out)=snapshot_;
        out.state=State::measured;
        return out;
    }
    static const char* stateName(State state) noexcept {
        switch(state){
            case State::workerUnavailable:return "WORKER_UNAVAILABLE";
            case State::warming:return "WARMING";
            case State::measured:return "MEASURED";
            case State::stale:return "STALE";
            case State::gap:return "ANALYSIS_GAP_NEW_SEGMENT_PENDING";
            default:return "UNSUPPORTED";
        }
    }
private:
    void run() noexcept {
        NativeMasterLoudness meter;
        std::uint64_t current=0,frames=0;
        auto lastPublish=Clock::now(),lastProcessed=lastPublish;
        bool dirty=false;
        const auto activate=[&](std::uint64_t gen){
            if(gen==current)return;
            current=gen;frames=0;dirty=false;
            meter.prepare(gen&1?48000.0:0.0);
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotGeneration_=gen;snapshotFrames_=0;snapshot_={};
        };
        while(!stop_.load(std::memory_order_acquire)){
            const auto gen=generation_.load(std::memory_order_acquire);
            activate(gen);
            bool drained=false;
            for(unsigned i=0;i<64;++i){
                if(!queue_->consume([&](const Queue::Block& b) noexcept {
                    // Read the live generation at consumption, not only at
                    // the start of the batch: a new epoch can publish its
                    // first PCM while this worker is draining an old one.
                    const auto live=generation_.load(std::memory_order_acquire);
                    if(b.generation!=live || !(live&1) ||
                       gapGeneration_.load(std::memory_order_acquire)==live)return;
                    activate(live);
                    const float* pcm[2]{b.pcm[0].data(),b.pcm[1].data()};
                    meter.process(pcm,b.channels,b.frames);
                    frames+=static_cast<std::uint64_t>(b.frames);
                    dirty=true;lastProcessed=Clock::now();
                })){drained=true;break;}
            }
            const auto now=Clock::now();
            // Publish at most 40 times/s, including a final partial batch
            // after producer stops. Avoid scanning the histogram on every
            // small ASIO block, even on the normal-priority worker.
            if(dirty && now-lastPublish>=std::chrono::milliseconds(25)){
                const auto value=meter.read();
                std::lock_guard<std::mutex> lock(snapshotMutex_);
                snapshot_=value;snapshotFrames_=frames;snapshotGeneration_=current;snapshotAt_=lastProcessed;
                lastPublish=now;dirty=false;
            }
            if(drained)std::this_thread::sleep_for(std::chrono::milliseconds((gen&1)?2:20));
        }
    }
    std::unique_ptr<Queue> queue_; // CONTROL preallocation; avoid a 1 MiB engine stack member
    std::atomic<std::uint64_t> generation_{0},gapGeneration_{0},dropped_{0},restarts_{0};
    std::atomic<bool> stop_{false};
    mutable std::mutex snapshotMutex_;
    NativeMasterLoudness::Reading snapshot_{};
    std::uint64_t snapshotGeneration_=0,snapshotFrames_=0;
    Clock::time_point snapshotAt_{};
    std::thread worker_;
    bool available_=false; // immutable after construction; no RT writes
};
} // namespace armix::dsp
