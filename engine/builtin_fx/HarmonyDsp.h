// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Deterministic, allocation-free vocal harmony DSP. No JUCE or GUI dependency.
// Grain shifting is a functioning prototype, not formant-accurate synthesis.
namespace armix::harmony {
struct Settings {
    int key=0, scale=0, voices=3; // C=0..B=11; 0=major, 1=natural minor; 2..4 voices
    bool correction=false;
    float wet=0.45f, space=0.60f, color=0.50f, gainDb=0.f;
};
inline bool valid(const Settings& s) noexcept {
    return s.key>=0&&s.key<12&&s.scale>=0&&s.scale<=1&&
      s.voices>=2&&s.voices<=4&&std::isfinite(s.wet)&&s.wet>=0&&s.wet<=1&&
      std::isfinite(s.space)&&s.space>=0&&s.space<=1&&
      std::isfinite(s.color)&&s.color>=0&&s.color<=1&&
      std::isfinite(s.gainDb)&&s.gainDb>=-24&&s.gainDb<=12;
}
inline constexpr std::array<int,7> major{{0,2,4,5,7,9,11}};
inline constexpr std::array<int,7> minor{{0,2,3,5,7,8,10}};
inline int floorDiv(int a,int b) noexcept { const int q=a/b,r=a%b;return r<0?q-1:q; }
inline int noteAt(int degree,int key,int scale) noexcept {
    const auto& notes=scale==0?major:minor;
    const int oct=floorDiv(degree,7),step=degree-oct*7;
    return key+12*oct+notes[static_cast<std::size_t>(step)];
}
inline int nearestDegree(float midi,int key,int scale) noexcept {
    const int approx=static_cast<int>(std::floor(midi/12.f))*7;
    int best=approx; float error=1.e9f;
    for(int d=approx-21;d<=approx+21;++d) {
        const float e=std::abs(static_cast<float>(noteAt(d,key,scale))-midi);
        if(e<error){error=e;best=d;}
    }
    return best;
}
class Engine final {
public:
    static constexpr int maxVoices=4;
    static constexpr std::size_t capacity=32768;
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        rate_=rate;
        grain_=std::clamp(static_cast<int>(std::round(rate*0.016)),256,3072);
        latency_=2*grain_;
        decimation_=std::max(1,static_cast<int>(std::round(rate/12000.)));
        detectEvery_=std::max(128,static_cast<int>(rate/150.));
        lowLag_=std::max(8,static_cast<int>(rate/(decimation_*850.)));
        highLag_=std::min(230,static_cast<int>(rate/(decimation_*85.)));
        if(lowLag_>=highLag_||latency_+3*grain_>=static_cast<int>(capacity))return false;
        left_.fill(0.f);right_.fill(0.f);mono_.fill(0.f);decimated_.fill(0.f);
        filtered_.fill(0.f);phase_.fill(0.f);ratio_.fill(1.f);targetRatio_.fill(1.f);
        // Low-pass before downsampling: raw 12 kHz decimation aliases vocal sibilants
        // into the periodicity detector and destabilizes the live note readback.
        decimationLowpass_=0.f;
        const float cutoff=1800.f;
        decimationAlpha_=static_cast<float>(1.-std::exp(-2.*3.141592653589793*cutoff/rate));
        write_=0;downWrite_=0;downSeen_=0;downCounter_=0;
        detectCounter_=0;noteHold_=0;currentNote_=-1;candidateNote_=-1;
        voiced_=false;voicedGain_=0.f;settings_=Settings{};
        outputGain_=1.f;return true;
    }
    bool setSettings(const Settings& next) noexcept {
        if(!valid(next))return false;
        settings_=next;outputGain_=std::pow(10.f,next.gainDb/20.f);
        return true;
    }
    const Settings& settings() const noexcept{return settings_;}
    int latencySamples() const noexcept{return latency_;}
    int observedMidiNote() const noexcept{return currentNote_;}
    bool voiced() const noexcept{return voiced_;}
    void process(float* l,float* r,int frames) noexcept {
        if(!l||!r||frames<=0||rate_<=0.)return;
        constexpr float pi=3.14159265358979323846f;
        const float wet=settings_.wet,space=settings_.space,color=settings_.color;
        const float harmonyAmp=wet*0.65f/std::sqrt(static_cast<float>(settings_.voices));
        for(int i=0;i<frames;++i) {
            const float inputL=std::isfinite(l[i])?l[i]:0.f;
            const float inputR=std::isfinite(r[i])?r[i]:0.f;
            const float mono=0.5f*(inputL+inputR);
            left_[write_]=inputL;right_[write_]=inputR;mono_[write_]=mono;
            decimationLowpass_+=decimationAlpha_*(mono-decimationLowpass_);
            if(++downCounter_>=decimation_){
                downCounter_=0;
                decimated_[downWrite_]=decimationLowpass_;
                downWrite_=(downWrite_+1)%decimated_.size();
                ++downSeen_;
            }
            if(++detectCounter_>=detectEvery_){detectCounter_=0;detectPitch();}
            const float dryL=tap(left_,static_cast<float>(latency_));
            const float dryR=tap(right_,static_cast<float>(latency_));
            voicedGain_+=((voiced_?1.f:0.f)-voicedGain_)*0.002f;
            float lead=0.f;
            if(settings_.correction&&voicedGain_>0.00001f)lead=shifted(4,color);
            const float leadBlend=settings_.correction?voicedGain_:0.f;
            float outL=(dryL*(1.f-leadBlend)+lead*leadBlend)*(1.f-wet*0.65f*voicedGain_);
            float outR=(dryR*(1.f-leadBlend)+lead*leadBlend)*(1.f-wet*0.65f*voicedGain_);
            if(voicedGain_>0.00001f){
                for(int v=0;v<settings_.voices;++v){
                    const float y=shifted(v,color)*harmonyAmp*voicedGain_;
                    const float pan=space*(2.f*static_cast<float>(v)/(settings_.voices-1)-1.f);
                    const float a=(pan+1.f)*pi*0.25f;
                    outL+=y*std::cos(a);outR+=y*std::sin(a);
                }
            }
            l[i]=std::clamp(outL*outputGain_,-4.f,4.f);
            r[i]=std::clamp(outR*outputGain_,-4.f,4.f);
            write_=(write_+1)%capacity;
        }
    }
private:
    float tap(const std::array<float,capacity>& a,float delay) const noexcept {
        const float d=std::clamp(delay,1.f,static_cast<float>(capacity-3));
        const int whole=static_cast<int>(d);
        const float f=d-whole;
        const auto a0=(write_+capacity-static_cast<std::size_t>(whole))%capacity;
        const auto a1=(a0+capacity-1)%capacity;
        return a[a0]*(1.f-f)+a[a1]*f;
    }
    float shifted(int voice,float color) noexcept {
        float& currentRatio=ratio_[static_cast<std::size_t>(voice)];
        currentRatio+=(targetRatio_[static_cast<std::size_t>(voice)]-currentRatio)*0.00045f;
        const float p=phase_[static_cast<std::size_t>(voice)];
        const float pB=p<0.5f?p+0.5f:p-0.5f;
        const float delayA=2.f*grain_+(1.f-currentRatio)*p*grain_;
        const float delayB=2.f*grain_+(1.f-currentRatio)*pB*grain_;
        const float w=1.f-std::abs(2.f*p-1.f);
        float y=w*tap(mono_,delayA)+(1.f-w)*tap(mono_,delayB);
        const float coef=0.07f+color*0.77f;
        float& f=filtered_[static_cast<std::size_t>(voice)];
        f+=coef*(y-f);y=f;
        phase_[static_cast<std::size_t>(voice)]+=1.f/static_cast<float>(grain_);
        if(phase_[static_cast<std::size_t>(voice)]>=1.f)
            phase_[static_cast<std::size_t>(voice)]-=1.f;
        return y;
    }
    void detectPitch() noexcept {
        if(downSeen_<static_cast<std::size_t>(highLag_+170))return;
        constexpr int span=160;
        // Reject background noise-floor and silent periods BEFORE correlation.
        // Normalized correlation alone can report a confident pitch on tiny noise.
        double energy=0.;
        for(int j=0;j<span;++j){
            const auto ix=(downWrite_+decimated_.size()-1-static_cast<std::size_t>(j))%decimated_.size();
            const double x=decimated_[ix];energy+=x*x;
        }
        if(energy/static_cast<double>(span)<1.e-6){
            voiced_=false;noteHold_=0;candidateNote_=-1;return;
        }
        float best=-1.f;int bestLag=0;
        std::array<float,231> correlations{};
        for(int lag=lowLag_;lag<=highLag_;++lag){
            double xy=0.,xx=0.,yy=0.;
            for(int j=0;j<span;++j){
                const auto a=(downWrite_+decimated_.size()-1-static_cast<std::size_t>(j))%decimated_.size();
                const auto b=(a+decimated_.size()-static_cast<std::size_t>(lag))%decimated_.size();
                const float x=decimated_[a],y=decimated_[b];
                xy+=x*y;xx+=x*x;yy+=y*y;
            }
            const float correlation=static_cast<float>(xy/std::sqrt(xx*yy+1.e-14));
            correlations[static_cast<std::size_t>(lag)]=correlation;
            if(correlation>best){best=correlation;bestLag=lag;}
        }
        // Prefer shortest strong period rather than an octave-multiple peak.
        if(best>0.77f){
            for(int lag=lowLag_;lag<=highLag_;++lag){
                if(correlations[static_cast<std::size_t>(lag)]>=std::max(0.77f,best-0.035f)){
                    bestLag=lag;break;
                }
            }
        }
        if(bestLag==0||best<=0.77f){voiced_=false;noteHold_=0;return;}
        // Three-point peak interpolation reduces integer-lag tuning steps,
        // especially at the high end of the supported singing range.
        float lag=static_cast<float>(bestLag);
        if(bestLag>lowLag_&&bestLag<highLag_){
            const float a=correlations[static_cast<std::size_t>(bestLag-1)];
            const float b=correlations[static_cast<std::size_t>(bestLag)];
            const float d=correlations[static_cast<std::size_t>(bestLag+1)];
            const float curvature=a-2.f*b+d;
            if(curvature < -1.e-6f)
                lag+=std::clamp(0.5f*(a-d)/curvature,-0.5f,0.5f);
        }
        const float hz=static_cast<float>(rate_)/(decimation_*lag);
        const float midi=69.f+12.f*std::log2(hz/440.f);
        if(!std::isfinite(midi)||midi<31.f||midi>96.f){voiced_=false;return;}
        const int degree=nearestDegree(midi,settings_.key,settings_.scale);
        const int note=noteAt(degree,settings_.key,settings_.scale);
        if(note!=candidateNote_){candidateNote_=note;noteHold_=1;}
        else ++noteHold_;
        if(noteHold_<2&&currentNote_>=0)return;
        currentNote_=note;voiced_=true;
        constexpr std::array<int,4> intervals{{2,4,-2,6}};
        for(int v=0;v<maxVoices;++v){
            const int target=noteAt(degree+intervals[static_cast<std::size_t>(v)],
                                    settings_.key,settings_.scale);
            targetRatio_[static_cast<std::size_t>(v)]=std::clamp(
                std::pow(2.f,(target-midi)/12.f),0.50f,2.0f);
        }
        targetRatio_[4]=std::clamp(std::pow(2.f,(note-midi)/12.f),0.75f,1.34f);
    }
    double rate_=0.; int grain_=0,latency_=0,decimation_=4,detectEvery_=320;
    int lowLag_=12,highLag_=144;std::size_t write_=0,downWrite_=0,downSeen_=0;
    int downCounter_=0,detectCounter_=0;
    int currentNote_=-1,candidateNote_=-1,noteHold_=0;
    bool voiced_=false;float voicedGain_=0.f,outputGain_=1.f;
    float decimationLowpass_=0.f,decimationAlpha_=0.f;
    Settings settings_{};
    std::array<float,capacity> left_{},right_{},mono_{};
    std::array<float,2048> decimated_{};
    std::array<float,5> filtered_{},phase_{},ratio_{},targetRatio_{};
};
} // namespace armix::harmony
