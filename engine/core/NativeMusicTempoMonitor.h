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
        float bpm=0.f,confidence=0.f,phaseConfidence=0.f;
        bool phaseLocked=false;
        std::uint64_t beatAnchorTick=0,observationTick=0;
        // Both are authoritative native callback sample-frame positions.
        // Onset is quantized by the 10ms summary window; not sample-perfect
        // transient detection or DAW-wide delay compensation.
        std::uint64_t beatAnchorFrame=0,observationFrame=0;
        bool locked=false;
        // Last CONTROL-only scan's actual tempo-bin comparisons. Enables
        // deterministic worst-case budget regression without wall-clock
        // timing or instrumentation on the ASIO callback.
        std::uint32_t scoringComparisons=0;
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
            result_.phaseLocked=false;result_.phaseConfidence=0.f;
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
        // CONTROL-owned fixed stack scratch; no heap or unbounded history.
        // Retain the incumbent candidate's score so a transient half/double
        // time accent cannot steal an already convincing song clock.
        std::array<float,121> candidates{};
        // CONTROL ONLY, exact-score-preserving sparse candidate search.
        // The former 121-BPM outer loop re-traversed the complete onset
        // history for every candidate, adding unpredictable GUI timer work.
        // Each onset pair can support only nearby BPM bins (<=3 periods).
        // Enumerate each pair ONCE, derive the mathematically eligible BPM
        // neighborhood, then use the original round/error/weight formula.
        // Memory is the same 121-float fixed stack array; no heap, worker,
        // allocations, callback scheduling or changed confidence threshold.
        constexpr int minBpm=60,maxBpm=180;
        constexpr float maxPairTicks=(6000.f/minBpm)*3.08f;
        std::uint32_t compared=0;
        for(std::size_t j=1;j<count_;++j) {
            const auto& later=at(j);
            if(lastTick_-later.tick>1100)continue;
            for(std::size_t i=j;i-->0;) {
                const auto& earlier=at(i);
                const float dt=static_cast<float>(later.tick-earlier.tick);
                if(dt>maxPairTicks)break;
                const float strength=std::sqrt(earlier.strength*later.strength);
                for(int cycles=1;cycles<=3;++cycles) {
                    const float targetBpm=6000.f*cycles/dt;
                    // The original tolerance is max(1.4 ticks, 2.2% of
                    // a multi-beat period). Within 60–180 BPM, its largest
                    // possible candidate separation is <10 BPM. +/-12
                    // therefore cannot discard an original valid score.
                    const int near=static_cast<int>(std::lround(targetBpm));
                    const int lo=std::max(minBpm,near-12);
                    const int hi=std::min(maxBpm,near+12);
                    for(int bpm=lo;bpm<=hi;++bpm) {
                        ++compared;
                        const float period=6000.f/static_cast<float>(bpm);
                        if(dt>period*3.08f)continue;
                        if(static_cast<int>(std::lround(dt/period))!=cycles)continue;
                        const float tolerance=std::max(1.4f,.022f*period*cycles);
                        const float error=std::abs(dt-period*cycles);
                        if(error>=tolerance)continue;
                        candidates[static_cast<std::size_t>(bpm-minBpm)]+=
                            strength*(1.f-error/tolerance)/
                            (cycles==1?1.f:cycles==2?1.6f:2.25f);
                    }
                }
            }
        }
        result_.scoringComparisons=compared;
        // Preserve deterministic ascending-BPM tie-breaking and the old
        // half/double-time decision semantics exactly.
        for(int bpm=minBpm;bpm<=maxBpm;++bpm) {
            const float score=candidates[static_cast<std::size_t>(bpm-minBpm)];
            if(score>bestScore){bestScore=score;bestBpm=bpm;}
        }
        if(bestBpm==0 || bestScore<2.0f)return;
        if(result_.locked) {
            const int previous=static_cast<int>(std::lround(result_.bpm));
            const bool octave=std::abs(2*previous-bestBpm)<=3 ||
                              std::abs(previous-2*bestBpm)<=3;
            if(octave && previous>=60 && previous<=180 &&
               candidates[static_cast<std::size_t>(previous-60)]>=
                   bestScore*.70f) {
                // The rhythmic subdivision is ambiguous. Stay on the song
                // BPM until a genuine, repeatedly stronger tempo emerges.
                bestBpm=previous;
            }
        }
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
        // CONTROL: phase is a separate decision from the BPM estimate.
        // Search the bounded onset set for the strongest coherent pulse
        // lattice, not simply the most recent loud snare / clap.
        const float period=6000.f/result_.bpm;
        const float tolerance=std::max(1.6f,period*.075f);
        const auto matches=[&](std::uint64_t a,std::uint64_t b) noexcept {
            const float elapsed=static_cast<float>(a>b?a-b:b-a);
            const float cycles=std::round(elapsed/period);
            const float difference=std::abs(elapsed-cycles*period);
            return std::max(0.f,1.f-difference/tolerance);
        };
        float total=0.f;
        for(std::size_t i=0;i<count_;++i)
            if(lastTick_-at(i).tick<=1100)total+=at(i).strength;
        float leading=0.f;
        std::uint64_t anchor=0,anchorFrame=0;
        if(total>0.f)for(std::size_t candidate=0;candidate<count_;++candidate) {
            const auto& center=at(candidate);
            if(lastTick_-center.tick>1100)continue;
            float coherence=0.f;
            for(std::size_t i=0;i<count_;++i) {
                const auto& h=at(i);
                if(lastTick_-h.tick<=1100)
                    coherence+=h.strength*matches(h.tick,center.tick);
            }
            // Prefer a more recent coherent beat anchor on ties, not a
            // historical frame from before a long musical break.
            if(coherence>leading+.0001f ||
               (std::abs(coherence-leading)<=.0001f &&
                center.tick>anchor)) {
                leading=coherence;
                anchor=center.tick;
                anchorFrame=center.frame;
            }
        }
        // Competing off-beat cohorts mean the quarter-note phase is not
        // established, even when interval BPM itself is reliable.
        float competing=0.f;
        if(anchor)for(std::size_t candidate=0;candidate<count_;++candidate) {
            const auto& center=at(candidate);
            if(lastTick_-center.tick>1100 ||
               matches(center.tick,anchor)>.15f)continue;
            float coherence=0.f;
            for(std::size_t i=0;i<count_;++i) {
                const auto& h=at(i);
                if(lastTick_-h.tick<=1100)
                    coherence+=h.strength*matches(h.tick,center.tick);
            }
            competing=std::max(competing,coherence);
        }
        result_.beatAnchorTick=anchor;
        result_.beatAnchorFrame=anchorFrame;
        result_.observationTick=lastTick_;
        result_.locked=true;
        result_.phaseConfidence=total>0.f?
            std::clamp(leading/total,0.f,1.f):0.f;
        result_.phaseLocked=anchorFrame>0 &&
            result_.phaseConfidence>=.52f &&
            competing<leading*.72f;
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
