// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace armix::harmony {
// Experimental causal pitch-synchronous source-window candidate.
// Vowel waveforms are read at unity sample speed; pitch changes arise by
// re-spacing aligned source windows, rather than resampling their formants.
// Positive waveform peaks are approximate epochs, NOT proven glottal closures.
// This must pass actual singer A/B before acoustic release qualification.
// All state is fixed; no allocation, locks, FFT or IO in process().
template <std::size_t Capacity, int Voices>
class PitchSynchronousVoice final {
    static_assert(Capacity >= 4096 && (Capacity & (Capacity-1)) == 0);
    static_assert(Voices > 0 && Voices <= 8);
public:
    struct Output { float sample=0.f; bool ready=false; };
    void reset() noexcept { states_={}; }
    void resetVoice(int v) noexcept {
        if(v>=0&&v<Voices)states_[static_cast<std::size_t>(v)]={};
    }
    // time/writeIndex refer to the input sample just written to the ring.
    // fixedDelay retains the host's published latency; a singer-specific
    // arrival delay can be added without moving the original dry PCM.
    Output process(int v,std::uint64_t time,std::size_t writeIndex,
                   const std::array<float,Capacity>& input,
                   float sourcePeriod,float ratio,float fixedDelay) noexcept {
        if(v<0||v>=Voices||!std::isfinite(sourcePeriod)||
           !std::isfinite(ratio)||!std::isfinite(fixedDelay)||
           sourcePeriod<8.f||sourcePeriod>3200.f||
           ratio<.49f||ratio>2.01f||fixedDelay<sourcePeriod*1.2f)
            return {};
        auto& state=states_[static_cast<std::size_t>(v)];
        const double period=static_cast<double>(sourcePeriod);
        const double spacing=period/static_cast<double>(ratio);
        const double width=std::max(period,spacing);
        if(!state.initialized) {
            state.initialized=true;
            state.nextCenter=static_cast<double>(time)+width;
        }
        // Advance at most two grains per sample, even on hostile period jumps.
        // Normal supported periods place >=4 samples between target marks.
        for(int scheduled=0;scheduled<2 &&
            static_cast<double>(time)+.0001>=state.nextCenter-width;++scheduled) {
            const double desired=state.nextCenter-static_cast<double>(fixedDelay);
            const auto peak=findPositiveEpoch(input,time,writeIndex,desired,period);
            if(peak>=0) {
                auto& grain=state.grains[state.nextSlot];
                grain={state.nextCenter,static_cast<double>(peak),width,true};
                state.nextSlot=(state.nextSlot+1)%state.grains.size();
            }
            state.nextCenter+=spacing;
        }
        if(state.nextCenter+width<static_cast<double>(time))
            state.nextCenter=static_cast<double>(time)+width;
        float sum=0.f,weight=0.f;
        for(auto& grain:state.grains) {
            if(!grain.active)continue;
            const double offset=static_cast<double>(time)-grain.center;
            const double u=offset/grain.width;
            if(u>=1.){grain.active=false;continue;}
            if(u<=-1.)continue;
            // Raised compact zero-slope window, avoiding per-sample trig.
            const float w=static_cast<float>((1.-u*u)*(1.-u*u));
            const float sample=readAt(input,time,writeIndex,grain.source+offset);
            sum+=sample*w;weight+=w;
        }
        if(weight<.05f)return {};
        return {sum/weight,true};
    }
private:
    struct Grain {double center=0.,source=0.,width=1.;bool active=false;};
    struct State {
        std::array<Grain,8> grains{};
        double nextCenter=0.;
        std::size_t nextSlot=0;
        bool initialized=false;
    };
    // Bound the peak search to <1 source cycle, never scanning beyond the
    // already received microphone input. Unreliable speech uses fallback.
    static std::int64_t findPositiveEpoch(const std::array<float,Capacity>& input,
              std::uint64_t time,std::size_t writeIndex,
              double desired,double period) noexcept {
        const auto half=static_cast<std::int64_t>(std::ceil(period*.46));
        const auto center=static_cast<std::int64_t>(std::llround(desired));
        const auto oldest=static_cast<std::int64_t>(
            time>=Capacity-3?time-(Capacity-3):0);
        const auto newest=static_cast<std::int64_t>(time>1?time-1:0);
        const auto start=std::max(oldest,center-half);
        const auto end=std::min(newest,center+half);
        if(end<start)return -1;
        std::int64_t peak=-1;
        float best=.003f;
        for(auto t=start;t<=end;++t) {
            const auto age=static_cast<std::size_t>(
                static_cast<std::int64_t>(time)-t);
            const float value=input[(writeIndex+Capacity-age)&(Capacity-1)];
            if(value>best){best=value;peak=t;}
        }
        return peak;
    }
    static float readAt(const std::array<float,Capacity>& input,
          std::uint64_t time,std::size_t writeIndex,double position) noexcept {
        if(time<2||position<0.||position>static_cast<double>(time-1)||
           position<static_cast<double>(
               time>=Capacity-3?time-(Capacity-3):0))return 0.f;
        const double age=static_cast<double>(time)-position;
        const auto whole=static_cast<std::size_t>(age);
        const float fractional=static_cast<float>(
            age-static_cast<double>(whole));
        const auto index=(writeIndex+Capacity-whole)&(Capacity-1);
        return input[index]*(1.f-fractional)+
               input[(index+Capacity-1)&(Capacity-1)]*fractional;
    }
    std::array<State,Voices> states_{};
};
} // namespace armix::harmony
