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
    struct Output { float sample=0.f; bool ready=false; float coverage=0.f; bool recovered=false; };
    struct StereoOutput { float left=0.f,right=0.f; bool ready=false; float coverage=0.f; bool recovered=false; };
    // At scheduling time the desired epoch is width-delay samples in
    // the past. Reserve the full +/- .46-period search plus rounding and
    // fractional refinement margin. This is a sufficient causal bound,
    // not a promise of available voiced landmarks at startup.
    static constexpr float minResonanceScale=.92f,maxResonanceScale=1.08f;
    static bool completeEpochWindow(float period,float ratio,float delay) noexcept {
        return std::isfinite(period)&&std::isfinite(ratio)&&std::isfinite(delay)&&
            period>=8.f&&ratio>=.49f&&ratio<=2.01f&&
            delay>=std::max(period,period/ratio)+.46f*period+3.f;
    }
    // Normalizing a nearly empty overlap can restore full amplitude to
    // an unsupported fragment. Fade only the correction contribution
    // near holes; a normally overlapped period has weight >= 1.
    static float overlapCoverage(float weight) noexcept {
        const float u=std::clamp((weight-.05f)*2.f,0.f,1.f);
        return u*u*(3.f-2.f*u);
    }
    // For every offset in [-width,+width], age equals
    // center-epoch+(1-scale)*offset. Prove the whole source window
    // before admission; no future or overwritten taps mid-grain.
    static bool fullGrainSupported(double center,double epoch,double width,
                                   float scale) noexcept {
        if(!std::isfinite(center)||!std::isfinite(epoch)||
           !std::isfinite(width)||!std::isfinite(scale)||width<=0.||
           scale<minResonanceScale||scale>maxResonanceScale)return false;
        const double spread=std::abs(1.-static_cast<double>(scale))*width;
        const double age=center-epoch;
        return epoch-width*scale>=0.&&age-spread>=2.&&
               age+spread<=static_cast<double>(Capacity-4);
    }
    // Offline tests only, read after process; no extra RT telemetry.
    double lastEpochForOfflineTests(int v) const noexcept {
        return v>=0&&v<Voices&&states_[static_cast<std::size_t>(v)].haveEpoch?
            states_[static_cast<std::size_t>(v)].lastEpoch:-1.;
    }
    void reset() noexcept { states_={}; }
    void resetVoice(int v) noexcept {
        if(v>=0&&v<Voices)states_[static_cast<std::size_t>(v)]={};
    }
    // time/writeIndex refer to the input sample just written to the ring.
    // fixedDelay retains the host's published latency; a singer-specific
    // arrival delay can be added without moving the original dry PCM.
    Output process(int v,std::uint64_t time,std::size_t writeIndex,
                   const std::array<float,Capacity>& input,
                   float sourcePeriod,float ratio,float fixedDelay,
                   float resonanceScale=1.f,bool continuous=true,bool supportAware=true,
                   bool phaseAware=true,bool admitNew=true,bool recoverEpoch=false) noexcept {
        const auto out=render<false>(v,time,writeIndex,input,input,input,
                                     sourcePeriod,ratio,fixedDelay,resonanceScale,continuous,supportAware,phaseAware,admitNew,recoverEpoch);
        return {out.left,out.ready,out.coverage,out.recovered};
    }

    // The detector selects one set of epochs. Both stereo channels use the
    // same windows, timing and normalization, with no lead COLOR filtering.
    StereoOutput processStereo(int v,std::uint64_t time,std::size_t writeIndex,
                   const std::array<float,Capacity>& epochs,
                   const std::array<float,Capacity>& left,
                   const std::array<float,Capacity>& right,
                   float sourcePeriod,float ratio,float fixedDelay) noexcept {
        return render<true>(v,time,writeIndex,epochs,left,right,
                            sourcePeriod,ratio,fixedDelay,1.f,false,false,false,true,false);
    }
private:
    template <bool Stereo>
    StereoOutput render(int v,std::uint64_t time,std::size_t writeIndex,
                   const std::array<float,Capacity>& epochs,
                   const std::array<float,Capacity>& left,
                   const std::array<float,Capacity>& right,
                   float sourcePeriod,float ratio,float fixedDelay,
                   float resonanceScale,bool continuous,bool supportAware,bool phaseAware,bool admitNew,bool recoverEpoch) noexcept {
        if(v<0||v>=Voices||!std::isfinite(sourcePeriod)||
           !std::isfinite(ratio)||!std::isfinite(fixedDelay)||
           sourcePeriod<8.f||sourcePeriod>3200.f||
           ratio<.49f||ratio>2.01f||(admitNew&&fixedDelay<sourcePeriod*1.2f)||
           (!admitNew&&(!continuous||!supportAware||Stereo)))
            return {};
        // Epoch-centered waveform dilation: target-center spacing owns F0;
        // the intra-grain read rate controls local spectral structure.
        // Freeze the rate per grain, never sweep a running source tap.
        // A stereo lead is always unity and remains bit-exact.
        auto& state=states_[static_cast<std::size_t>(v)];
        // A role can stop being called when disabled or the causal
        // lookback is insufficient. Never resume an abandoned schedule.
        if(continuous&&state.initialized&&time!=state.lastTime+1)state={};
        state.lastTime=time;
        const double period=static_cast<double>(sourcePeriod);
        const double spacing=period/static_cast<double>(ratio);
        const double width=std::max(period,spacing);
        if(!state.initialized) {
            if(!admitNew)return {};
            state.initialized=true;
            state.nextCenter=static_cast<double>(time)+width;
            // Align mono backing *and* stereo lead to fractional epochs.
            // Integer-peak rounding previously induced a 1-sample phase
            // staircase on high soprano periods, heard as brittle buzz.
            const auto peak=findStableEpoch(epochs,time,writeIndex,
                state.nextCenter-fixedDelay,period,state.epochPolarity);
            if(peak>=0)
                state.nextCenter=refineEpoch(epochs,time,writeIndex,peak,
                    state.epochPolarity)+fixedDelay;
        }
        bool recovered=false;
        // Advance at most two grains per sample, even on hostile period jumps.
        // Normal supported periods place >=4 samples between target marks.
        for(int scheduled=0;scheduled<2 &&
            static_cast<double>(time)+.0001>=state.nextCenter-width;++scheduled) {
            // A new epoch is not causal, but previously admitted windows
            // have full support. Keep the target clock and drain them.
            if(!admitNew){state.nextCenter+=spacing;continue;}
            const double desired=state.nextCenter-static_cast<double>(fixedDelay);
            // Track a source landmark, not merely the largest sample
            // in an arbitrarily truncated search window. A predicted
            // period neighborhood avoids alternating vowel harmonics.
            std::int64_t peak=-1;
            if(continuous&&state.haveEpoch){
                const double predicted=state.lastEpoch+
                    std::round((desired-state.lastEpoch)/period)*period;
                if(std::abs(predicted-desired)<=.55*period)
                    peak=findLocalEpoch(epochs,time,writeIndex,predicted,
                                        .24*period,state.epochPolarity,phaseAware);
            }
            if(peak<0){
                if(continuous&&state.epochPolarity!=0)
                    peak=findLocalEpoch(epochs,time,writeIndex,desired,
                                        .56*period,state.epochPolarity);
                else
                    peak=findStableEpoch(epochs,time,writeIndex,desired,period,
                                         state.epochPolarity);
            }
            // The accepted F0 and delayed source waveform can temporarily
            // disagree during a note leap. A normal +/- .56P scan may
            // contain no same-polarity maximum. Only on such a miss, try
            // a bounded nearest-phase search; require a real local maximum
            // and the unchanged full-window safety proof below.
            bool rescue=false;
            if(peak<0&&recoverEpoch&&continuous&&supportAware&&state.haveEpoch&&
               state.epochPolarity!=0){
                // Bound the displacement to 1.5 periods AND 12ms of the
                // prepared lookback (half the nominal 24ms is not assumed:
                // fixedDelay includes each singer's arrival).
                const double radius=std::min(1.5*period,.40*fixedDelay);
                if(radius>.56*period){
                    peak=findLocalEpoch(epochs,time,writeIndex,desired,radius,
                                        state.epochPolarity,true);
                    rescue=peak>=0;
                }
            }
            if(peak>=0) {
                auto& grain=state.grains[state.nextSlot];
                // Never evict an audible grain on a sudden period jump.
                // Advance target time even if the bounded pool is full.
                if(continuous&&grain.active&&
                   std::abs(static_cast<double>(time)-grain.center)<grain.width){
                    state.nextCenter+=spacing;
                    continue;
                }
                const double epoch=refineEpoch(
                    epochs,time,writeIndex,peak,state.epochPolarity);
                // Only at grain admission, reuse the already-computed
                // width; no duplicate per-sample division or scale check.
                float scale=1.f,gain=1.f;
                if constexpr(!Stereo){
                    if(std::isfinite(resonanceScale)&&
                       fixedDelay>=width+.46*period+3.)
                        scale=std::clamp(resonanceScale,minResonanceScale,maxResonanceScale);
                    // Slow local read reduces HIGH coherent overlap
                    // amplitude. Bounded static compensation, frozen per
                    // grain and weighted by its smooth overlap window.
                    gain=1.f+1.5f*std::max(0.f,1.f-scale);
                }
                if(supportAware&&continuous&&
                   !fullGrainSupported(state.nextCenter,epoch,width,scale)){
                    state.nextCenter+=spacing;
                    continue;
                }
                state.lastEpoch=epoch;state.haveEpoch=true;
                // A larger new width can admit a grain INSIDE its window.
                // Ramp only these late admissions from zero rather than
                // injecting nonzero weight into an existing overlap.
                const double late=static_cast<double>(time)-(state.nextCenter-width);
                const double fade=continuous&&late>1.?std::max(2.,period*.25):0.;
                grain={state.nextCenter,epoch,width,true,scale,gain,
                       static_cast<double>(time),fade>0.?1./fade:0.,supportAware&&continuous};
                state.nextSlot=(state.nextSlot+1)%state.grains.size();
                recovered|=rescue;
            }
            state.nextCenter+=spacing;
        }
        if(state.nextCenter+width<static_cast<double>(time))
            state.nextCenter=static_cast<double>(time)+width;
        float sumL=0.f,sumDifference=0.f,weight=0.f;
        bool remaining=false;
        for(auto& grain:state.grains) {
            if(!grain.active)continue;
            const double offset=static_cast<double>(time)-grain.center;
            const double u=offset/grain.width;
            if(u>=1.){grain.active=false;continue;}
            remaining=true;
            if(u<=-1.)continue;
            // Raised compact zero-slope window, avoiding per-sample trig.
            float w=static_cast<float>((1.-u*u)*(1.-u*u));
            if(grain.admissionInv>0.){
                const float a=static_cast<float>(std::min(1.,
                    (static_cast<double>(time)-grain.admitted)*grain.admissionInv));
                w*=a*a*(3.f-2.f*a);
            }
            if constexpr(Stereo){
                const auto pair=readStereoAt(left,right,time,writeIndex,
                                             grain.source+offset);
                sumL+=pair.left*w;
                sumDifference+=pair.right*w;
            }else{
                const double position=grain.source+offset*grain.resonanceScale;
                // An expanded source tap can exceed history at startup or
                // a hostile pitch jump. Do not count missing PCM as a
                // valid grain. Unity preserves the existing renderer exactly.
                if(!grain.supported&&(continuous||grain.resonanceScale!=1.f)&&
                   (time<2||position<0.||position>static_cast<double>(time-1)||
                    position<static_cast<double>(time>=Capacity-3?time-(Capacity-3):0)))
                    continue;
                const float sample=grain.supported?readAtKnown(left,time,writeIndex,position):
                    readAt(left,time,writeIndex,position);
                sumL+=sample*(w*grain.resonanceGain);
            }
            weight+=w;
        }
        // Once all validated grains finish, discard the stale epoch clock.
        // The next safe sample starts a fresh phase search; no hidden tail.
        if(!admitNew&&!remaining){state={};return {};}
        if(weight<.05f)return {0.f,0.f,false,0.f,recovered};
        const float l=sumL/weight;
        if constexpr(Stereo){
            // Difference interpolated BEFORE accumulation: dual-mono
            // history is exactly zero side, irrespective of compiler FMA.
            const float r=sumDifference==0.f?l:l+sumDifference/weight;
            return {l,r,true};
        }
        return {l,0.f,true,supportAware&&continuous?overlapCoverage(weight):1.f,recovered};
    }
    struct Grain {
        double center=0.,source=0.,width=1.;bool active=false;
        float resonanceScale=1.f,resonanceGain=1.f;
        double admitted=0.,admissionInv=0.;
        bool supported=false;
    };
    struct State {
        std::array<Grain,8> grains{};
        double nextCenter=0.;
        std::size_t nextSlot=0;
        int epochPolarity=0; // stable sign throughout the voiced resynthesis run
        bool initialized=false,haveEpoch=false;
        std::uint64_t lastTime=0;
        double lastEpoch=0.;
    };
    // A fixed +3mV positive-peak threshold rejects quiet voiced periods and
    // asymmetric vowels, leaving holes in the high backing shifter. Select
    // the strongest signed source landmark once per voice and keep its sign
    // throughout the phrase, so polarity never flips between adjacent grains.
    // The caller's independent high-confidence pitch detector remains the
    // voiced/noise gate; this floor only excludes numerical silence.
    // The same <1-period scan and history bounds apply at every sample rate.
    static std::int64_t findStableEpoch(const std::array<float,Capacity>& input,
              std::uint64_t time,std::size_t writeIndex,
              double desired,double period,int& polarity) noexcept {
        const auto half=static_cast<std::int64_t>(std::ceil(period*.46));
        const auto center=static_cast<std::int64_t>(std::llround(desired));
        const auto oldest=static_cast<std::int64_t>(
            time>=Capacity-3?time-(Capacity-3):0);
        const auto newest=static_cast<std::int64_t>(time>1?time-1:0);
        const auto start=std::max(oldest,center-half);
        const auto end=std::min(newest,center+half);
        if(end<start)return -1;
        // Once a singer's epoch polarity is established, only that signed
        // landmark can win. The old scan still computed both extrema on
        // every grain. A sign-specific scan returns the identical first
        // winning sample (strict > tie rule), with one comparison per tap.
        constexpr float silenceFloor=1.e-5f;
        if(polarity!=0){
            float maximum=0.f;
            std::int64_t selected=-1;
            const float sign=polarity>0?1.f:-1.f;
            for(auto t=start;t<=end;++t){
                const auto age=static_cast<std::size_t>(
                    static_cast<std::int64_t>(time)-t);
                const float value=sign*input[(writeIndex+Capacity-age)&(Capacity-1)];
                if(value>maximum){maximum=value;selected=t;}
            }
            return maximum>=silenceFloor?selected:-1;
        }
        std::int64_t positive=-1,negative=-1;
        float maxPositive=0.f,maxNegative=0.f;
        for(auto t=start;t<=end;++t) {
            const auto age=static_cast<std::size_t>(
                static_cast<std::int64_t>(time)-t);
            const float value=input[(writeIndex+Capacity-age)&(Capacity-1)];
            if(value>maxPositive){maxPositive=value;positive=t;}
            if(-value>maxNegative){maxNegative=-value;negative=t;}
        }
        if(std::max(maxPositive,maxNegative)<silenceFloor)return -1;
        polarity=maxPositive>=maxNegative?1:-1;
        return polarity>0?
            (maxPositive>=silenceFloor?positive:-1):
            (maxNegative>=silenceFloor?negative:-1);
    }
    // One bounded scan with rolling neighbors; endpoints are candidates
    // only if their actual adjacent samples establish a local maximum.
    static std::int64_t findLocalEpoch(const std::array<float,Capacity>& input,
          std::uint64_t time,std::size_t writeIndex,double desired,
          double radius,int polarity,bool phaseAware=false) noexcept {
        if(time<3||polarity==0)return -1;
        const auto oldest=static_cast<std::int64_t>(time>=Capacity-3?time-(Capacity-3):0);
        const auto newest=static_cast<std::int64_t>(time)-2;
        const auto center=static_cast<std::int64_t>(std::llround(desired));
        const auto half=static_cast<std::int64_t>(std::ceil(radius));
        const auto start=std::max(oldest+1,center-half),end=std::min(newest,center+half);
        if(end<start)return -1;
        const float sign=polarity>0?1.f:-1.f;
        auto ix=(writeIndex+Capacity-static_cast<std::size_t>(
            static_cast<std::int64_t>(time)-start))&(Capacity-1);
        float prev=sign*input[(ix+Capacity-1)&(Capacity-1)],cur=sign*input[ix];
        float best=0.f;std::int64_t result=-1;
        const double invRadius=1./std::max(1.,radius);
        for(auto t=start;t<=end;++t){
            ix=(ix+1)&(Capacity-1);
            const float next=sign*input[ix];
            if(cur>=prev&&cur>next&&cur>1.e-5f){
                // A competing harmonic peak may become taller during a vowel.
                // Prefer phase continuity inside the predicted neighborhood,
                // but allow a genuinely stronger landmark to win. The wide
                // reacquisition scan remains amplitude-driven.
                const float score=phaseAware?cur*static_cast<float>(1.-.75*
                    std::min(1.,std::abs(static_cast<double>(t)-desired)*invRadius)):cur;
                if(score>best){best=score;result=t;}
            }
            prev=cur;cur=next;
        }
        return result;
    }
    static double refineEpoch(const std::array<float,Capacity>& input,
          std::uint64_t time,std::size_t writeIndex,std::int64_t peak,
          int polarity) noexcept {
        const auto age=time-static_cast<std::uint64_t>(peak);
        if(age<2||age>=Capacity-3||peak==0)return static_cast<double>(peak);
        const auto i=(writeIndex+Capacity-age)&(Capacity-1);
        const float sign=polarity<0?-1.f:1.f;
        const float a=sign*input[(i+Capacity-1)&(Capacity-1)];
        const float b=sign*input[i];
        const float c=sign*input[(i+1)&(Capacity-1)];
        const float curvature=a-2.f*b+c;
        if(b<a||b<c||curvature>=-1.e-6f)return static_cast<double>(peak);
        return static_cast<double>(peak)+
            std::clamp(.5f*(a-c)/curvature,-.5f,.5f);
    }
    static float readAt(const std::array<float,Capacity>& input,
          std::uint64_t time,std::size_t writeIndex,double position) noexcept {
        if(time<2||position<0.||position>static_cast<double>(time-1)||
           position<static_cast<double>(
               time>=Capacity-3?time-(Capacity-3):0))return 0.f;
        return readAtKnown(input,time,writeIndex,position);
    }
    static float readAtKnown(const std::array<float,Capacity>& input,
          std::uint64_t time,std::size_t writeIndex,double position) noexcept {
        const double age=static_cast<double>(time)-position;
        const auto whole=static_cast<std::size_t>(age);
        const float fractional=static_cast<float>(
            age-static_cast<double>(whole));
        const auto index=(writeIndex+Capacity-whole)&(Capacity-1);
        return input[index]*(1.f-fractional)+
               input[(index+Capacity-1)&(Capacity-1)]*fractional;
    }
    struct StereoTap {float left=0.f,right=0.f;}; // right stores R-L
    static StereoTap readStereoAt(const std::array<float,Capacity>& left,
          const std::array<float,Capacity>& right,std::uint64_t time,
          std::size_t writeIndex,double position) noexcept {
        if(time<2||position<0.||position>static_cast<double>(time-1)||
           position<static_cast<double>(
               time>=Capacity-3?time-(Capacity-3):0))return {};
        const double age=static_cast<double>(time)-position;
        const auto whole=static_cast<std::size_t>(age);
        const float f=static_cast<float>(age-static_cast<double>(whole));
        const auto a=(writeIndex+Capacity-whole)&(Capacity-1);
        const auto b=(a+Capacity-1)&(Capacity-1);
        const float la=left[a],lb=left[b];
        return {la*(1.f-f)+lb*f,
                (right[a]-la)*(1.f-f)+(right[b]-lb)*f};
    }
    std::array<State,Voices> states_{};
};
} // namespace armix::harmony
