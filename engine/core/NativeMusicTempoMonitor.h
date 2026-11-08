// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../builtin_fx/FxSpsc.h"
#include "DynamicChannelRoster.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// ArSon M2: source-specific music tempo observation inside the ONE installed
// ChannelId renderer. AUDIO only summarizes its explicitly selected music
// channel into 10ms energy frames and pushes a bounded SPSC. CONTROL owns all
// onset, pulse-correlation and tempo work. This is NOT a second audio engine.
namespace armix::mixer {
class MusicTempoEstimator final {
public:
    struct Result final {
        float bpm=0.f,confidence=0.f;
        std::uint64_t beatAnchorTick=0,observationTick=0;
        // Both are authoritative native callback sample-frame positions.
        // Onset is quantized by the 10ms summary window; not sample-perfect
        // transient detection or DAW-wide delay compensation.
        std::uint64_t beatAnchorFrame=0,observationFrame=0;
        bool locked=false;
    };
    void reset() noexcept {
        previous_=floor_=0.f;count_=write_=0;
        lastHit_=lastScan_=lastTick_=0;
        stableCount_=0;candidateBpm_=0;result_={};
    }
    void ingest(float energy,std::uint64_t tick,
                std::uint64_t endingFrame=0) noexcept {
        if(!std::isfinite(energy)||energy<0.f)energy=0.f;
        if(lastTick_ && (tick<=lastTick_ || tick-lastTick_>4))reset();
        lastTick_=tick;
        floor_+=.005f*(energy-floor_);
        const float rise=energy-previous_;
        // Attack transient only; sustained pads/bleed cannot retrigger
        // every 10ms. 80ms refractory rejects the same drum's ringing.
        if(tick>=lastHit_+8 && energy>.003f &&
           rise>std::max(.005f,floor_*.45f)) {
            hits_[write_]={tick,std::min(1.f,rise),endingFrame};
            write_=(write_+1)%hits_.size();
            count_=std::min(count_+1,hits_.size());
            lastHit_=tick;
        }
        previous_=energy;
        if(endingFrame)result_.observationFrame=endingFrame;
        if(result_.locked && tick>lastHit_+320) {
            result_.locked=false;result_.confidence=0.f;
            candidateBpm_=stableCount_=0;
        }
        if(tick>=lastScan_+50) {
            lastScan_=tick;
            evaluate();
        }
    }
    Result result() const noexcept {return result_;}
private:
    struct Hit {
        std::uint64_t tick=0;
        float strength=0.f;
        std::uint64_t frame=0;
    };
    std::array<Hit,128> hits_{};
    std::size_t count_=0,write_=0;
    std::uint64_t lastHit_=0,lastScan_=0,lastTick_=0;
    float previous_=0.f,floor_=0.f;
    int stableCount_=0,candidateBpm_=0;
    Result result_{};

    void evaluate() noexcept {
        if(count_<7 || lastTick_<300 || lastTick_>lastHit_+320)return;
        const auto at=[&](std::size_t i) noexcept ->const Hit& {
            return hits_[(write_+hits_.size()-count_+i)%hits_.size()];
        };
        if(at(count_-1).tick-at(0).tick<300)return;
        int bestBpm=0;
        float bestScore=0.f;
        // CONTROL ONLY: bounded 121 candidates x 128 onsets. Emphasize
        // one-beat matches; harmonics, half- and double-time are penalized.
        for(int bpm=60;bpm<=180;++bpm) {
            const float period=6000.f/static_cast<float>(bpm);
            float score=0.f;
            for(std::size_t j=1;j<count_;++j) {
                const auto& later=at(j);
                if(lastTick_-later.tick>1100)continue;
                for(std::size_t i=j;i-->0;) {
                    const auto& earlier=at(i);
                    const float dt=static_cast<float>(later.tick-earlier.tick);
                    if(dt>period*3.08f)break;
                    const int cycles=static_cast<int>(std::lround(dt/period));
                    if(cycles<1||cycles>3)continue;
                    const float tolerance=std::max(1.4f,.022f*period*cycles);
                    const float error=std::abs(dt-period*cycles);
                    if(error>=tolerance)continue;
                    const float strength=std::sqrt(earlier.strength*later.strength);
                    score+=strength*(1.f-error/tolerance)/
                        (cycles==1?1.f:cycles==2?1.6f:2.25f);
                }
            }
            if(score>bestScore) {bestScore=score;bestBpm=bpm;}
        }
        if(bestBpm==0 || bestScore<2.0f)return;
        if(std::abs(bestBpm-candidateBpm_)<=2)++stableCount_;
        else {candidateBpm_=bestBpm;stableCount_=1;}
        // A proven lock resists transient alternate half/double-time peaks.
        if(result_.locked && std::abs(bestBpm-result_.bpm)>5.f &&
           stableCount_<6)return;
        if(stableCount_<3)return;
        result_.bpm=static_cast<float>(candidateBpm_);
        result_.confidence=std::clamp(bestScore/
            static_cast<float>(std::max<std::size_t>(7,count_))*.9f,0.f,1.f);
        if(result_.confidence<.28f)return;
        // The strongest recent transient is a practical pulse anchor;
        // explicit "beat-phase certified" remains a separate field FAT.
        float strength=0.f;
        std::uint64_t anchor=0,anchorFrame=0;
        const auto period=6000.f/result_.bpm;
        for(std::size_t i=0;i<count_;++i) {
            const auto& h=at(i);
            if(lastTick_-h.tick>period*1.8f)continue;
            if(h.strength>=strength) {
                strength=h.strength;anchor=h.tick;anchorFrame=h.frame;
            }
        }
        result_.beatAnchorTick=anchor;
        result_.beatAnchorFrame=anchorFrame;
        result_.observationTick=lastTick_;
        result_.locked=true;
    }
};

class NativeMusicTempoMonitor final {
public:
    struct State final {
        ChannelId source{};
        std::uint64_t epoch=0,revision=0;
        MusicTempoEstimator::Result tempo{};
        std::uint32_t dropped=0;
        bool active=false;
    };
    // CONTROL, only after checking this exact ChannelId exists in the
    // accepted PCM graph and does not point at vocals, MAIN or MONITOR.
    bool select(ChannelId id,std::uint64_t epoch,std::uint64_t revision,
                double sampleRate) noexcept {
        if(id.value && (!epoch||!revision||!std::isfinite(sampleRate)||
                        sampleRate<8000.||sampleRate>192000.))return false;
        source_.store(id.value,std::memory_order_relaxed);
        epoch_.store(epoch,std::memory_order_relaxed);
        revision_.store(revision,std::memory_order_relaxed);
        window_.store(id.value?
            static_cast<std::uint32_t>(std::lround(sampleRate*.01)):0,
            std::memory_order_relaxed);
        config_.fetch_add(1,std::memory_order_release);
        return true;
    }
    // CONTROL: harmless structural WIRE/FX commits should preserve a valid
    // music source. If the selected channel disappears, fall back to manual
    // immediately rather than capturing stale processor/graph PCM.
    ChannelId selectedSource() const noexcept {
        return ChannelId{source_.load(std::memory_order_relaxed)};
    }
    void onGraphCommitted(std::uint64_t epoch,std::uint64_t previousRevision,
                          std::uint64_t revision,bool retained) noexcept {
        if(!selectedSource().value)return;
        if(!retained || epoch_.load(std::memory_order_relaxed)!=epoch ||
           revision_.load(std::memory_order_relaxed)!=previousRevision) {
            (void)select(ChannelId{},epoch,revision,0);
            return;
        }
        revision_.store(revision,std::memory_order_relaxed);
        config_.fetch_add(1,std::memory_order_release);
    }
    // AUDIO ONLY: the accepted publisher calls this ONCE per successfully
    // admitted render block before traversing its ChannelId lanes. Revisions
    // do not reset this clock; a physical epoch change always does.
    std::uint64_t beginAudioBlock(std::uint64_t epoch,int frames) noexcept {
        if(audioEpoch_!=epoch) {
            audioEpoch_=epoch;
            audioFrameClock_=0;
        }
        const auto start=audioFrameClock_;
        if(frames>0)audioFrameClock_+=static_cast<std::uint64_t>(frames);
        blockStartFrame_=start;
        return start;
    }
    // AUDIO: invoked once for every scheduled source channel by the existing
    // renderer's pre-FX dispatch. Only the selected ID costs per-sample work.
    void capture(ChannelId id,const float* left,const float* right,int frames,
                 std::uint64_t epoch,std::uint64_t revision) noexcept {
        const auto generation=config_.load(std::memory_order_acquire);
        if(audioGeneration_!=generation) {
            audioGeneration_=generation;
            bucketSamples_=0;energySum_=0.f;audioTick_=0;
        }
        if(!id.value||id.value!=source_.load(std::memory_order_relaxed)||
           epoch!=epoch_.load(std::memory_order_relaxed)||
           revision!=revision_.load(std::memory_order_relaxed)||
           !left||!right||frames<=0)return;
        const auto window=window_.load(std::memory_order_relaxed);
        if(window==0)return;
        for(int i=0;i<frames;++i) {
            const float l=std::isfinite(left[i])?std::abs(left[i]):0.f;
            const float r=std::isfinite(right[i])?std::abs(right[i]):0.f;
            energySum_+=.5f*(l+r);
            if(++bucketSamples_==window) {
                const Frame f{generation,++audioTick_,energySum_/window,
                    blockStartFrame_+static_cast<std::uint64_t>(i)+1};
                if(!frames_.push(f))
                    dropped_.fetch_add(1,std::memory_order_relaxed);
                bucketSamples_=0;energySum_=0.f;
            }
        }
    }
    // CONTROL: limited work per timer tick, never called from ASIO callback.
    State poll(std::uint64_t liveEpoch,std::uint64_t liveRevision) noexcept {
        const auto generation=config_.load(std::memory_order_acquire);
        if(controlGeneration_!=generation) {
            controlGeneration_=generation;estimator_.reset();
        }
        Frame f{};
        for(int i=0;i<512 && frames_.pop(f);++i)
            if(f.generation==generation)
                estimator_.ingest(f.energy,f.tick,f.frameEnd);
        State out{};
        out.source.value=source_.load(std::memory_order_relaxed);
        out.epoch=epoch_.load(std::memory_order_relaxed);
        out.revision=revision_.load(std::memory_order_relaxed);
        out.dropped=dropped_.load(std::memory_order_relaxed);
        out.active=out.source.value && out.epoch==liveEpoch &&
                   out.revision==liveRevision;
        if(out.active)out.tempo=estimator_.result();
        return out;
    }
private:
    struct Frame {
        std::uint32_t generation=0;
        std::uint64_t tick=0;
        float energy=0.f;
        std::uint64_t frameEnd=0;
    };
    armix::builtin_fx::FxSpsc<Frame,2048> frames_{};
    MusicTempoEstimator estimator_{}; // CONTROL owner only
    std::atomic<std::uint64_t> source_{0},epoch_{0},revision_{0};
    std::atomic<std::uint32_t> config_{0},window_{0},dropped_{0};
    // The following fields are AUDIO-thread exclusive.
    std::uint32_t audioGeneration_=0,bucketSamples_=0;
    float energySum_=0.f;
    std::uint64_t audioTick_=0;
    std::uint64_t audioEpoch_=0,audioFrameClock_=0,blockStartFrame_=0;
    std::uint32_t controlGeneration_=0; // CONTROL only
};
} // namespace armix::mixer
