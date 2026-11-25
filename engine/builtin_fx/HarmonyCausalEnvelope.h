// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace armix::harmony {
// Audition-only causal LPC envelope-to-EQ approximation. Fixed storage,
// no FFT, heap, extra PCM delay or OS calls. Analysis is time-sliced.
// Not a complete source/filter decomposition or a guarantee of formants.
class CausalEnvelope final {
public:
    static constexpr int order=12,bands=5;
    static constexpr float pi=3.14159265358979323846f;
    void prepare(double rate,bool adaptive=false) noexcept {
        *this=CausalEnvelope{};adaptive_=adaptive;
        decimation_=std::max(1,int(std::lround(rate/12000.)));
        const double ar=rate/decimation_;
        lowAlpha_=float(1.-std::exp(-2.*pi*std::min(4400.,.36*ar)/rate));
        corrAlpha_=float(1.-std::exp(-1./(ar*.035)));
        smooth_=float(1.-std::exp(-1./(rate*.025)));
        hop_=std::max(order+bands+2,int(ar*.010));
        warm_=int(ar*.040);
        constexpr std::array<float,bands> hz{{550.f,1000.f,1700.f,2800.f,4300.f}};
        for(int b=0;b<bands;++b){
            const double w=2.*pi*hz[b]/rate;
            auto& e=eq_[b];
            e.cos=float(std::cos(w));e.alpha=float(std::sin(w)/(2.*1.25));
            // H27: fixed-denominator unity-peak bandpass. Time variation
            // happens only in a feedforward scalar; recursive poles never move.
            const float norm=1.f+e.alpha;
            e.bp0=e.alpha/norm;e.bp1=-2.f*e.cos/norm;e.bp2=(1.f-e.alpha)/norm;
            for(int k=0;k<=order;++k){
                const double a=2.*pi*hz[b]*k/ar;
                cosine_[b][k]=float(std::cos(a));sine_[b][k]=float(std::sin(a));
            }
        }
    }
    static float harmonicSupport(float hz,float f0) noexcept {
        if(!std::isfinite(f0)||f0<=0.f)return 0.f;
        return std::clamp((hz/f0-1.5f)/2.5f,0.f,1.f);
    }
    float process(float source,float wet,bool trusted,float sourceF0=0.f,
                  float targetF0=0.f,float quality=1.f) noexcept {
        if(adaptive_){
            // An LPC peak at high F0 may be a harmonic, not a formant.
            // Do not extrapolate from missing/invalid pitch evidence.
            trusted=trusted&&std::isfinite(sourceF0)&&std::isfinite(targetF0)&&
                sourceF0>=65.f&&targetF0>=65.f&&sourceF0<=1200.f&&targetF0<=1800.f&&
                std::isfinite(quality)&&quality>=.5f;
            supportF0_=trusted?std::max(sourceF0,targetF0):0.f;
            quality_=trusted?std::clamp(quality,0.f,1.f):0.f;
        }
        // Two antialias poles, identical source/wet phase response.
        src_.lp1+=lowAlpha_*(source-src_.lp1);src_.lp2+=lowAlpha_*(src_.lp1-src_.lp2);
        dst_.lp1+=lowAlpha_*(wet-dst_.lp1);dst_.lp2+=lowAlpha_*(dst_.lp1-dst_.lp2);
        if(trusted_&&!trusted){
            work_=0;warmCount_=0;neutral();
            if(adaptive_){
                clearEvidence(src_);clearEvidence(dst_);
                ++releases_;
            }
        }
        trusted_=trusted;
        if(++decCounter_>=decimation_){
            decCounter_=0;if(!adaptive_||trusted_){observe(src_);observe(dst_);}
            if(trusted_&&warmCount_<warm_)++warmCount_;
            if(++hopCounter_>=hop_){hopCounter_=0;begin();}
            if(work_>0&&work_<=order){step(src_,work_);step(dst_,work_);++work_;}
            else if(work_>order&&work_<=order+bands){
                const int b=work_-order-1;
                const float p=power(src_,b),q=power(dst_,b);
                logRatio_[b]=.5f*std::log(std::clamp(q/p,.01f,100.f));++work_;
            }else if(work_==order+bands+1){finish();work_=0;}
        }
        blend_+=smooth_*((trusted&&warmCount_>=warm_?1.f:0.f)-blend_);
        float y=wet;
        for(auto& e:eq_){
            if(adaptive_){
                e.gain+=smooth_*(e.gainTarget-e.gain);
                const float bp=e.bp0*y+e.z1;
                e.z1=-e.bp1*bp+e.z2;
                e.z2=-e.bp0*y-e.bp2*bp;
                y+=e.gain*bp;
                continue;
            }
            for(int k=0;k<5;++k)e.c[k]+=smooth_*(e.target[k]-e.c[k]);
            const float z=e.c[0]*y+e.z1;
            e.z1=e.c[1]*y-e.c[3]*z+e.z2;
            e.z2=e.c[2]*y-e.c[4]*z;
            y=z;
        }
        return wet+blend_*(y-wet);
    }
    std::uint64_t accepted()const noexcept{return accepted_;}
    float maxDb()const noexcept{return maxDb_;}
    std::uint64_t releases()const noexcept{return releases_;}
    float lastSupport()const noexcept{return lastSupport_;}
private:
    struct Analysis {
        std::array<float,order+1> x{},r{},snapshot{},a{};
        float lp1=0,lp2=0,previous=0,error=0;
        int cursor=0;
    };
    struct Eq {
        std::array<float,5> c{{1,0,0,0,0}},target{{1,0,0,0,0}};
        float cos=0,alpha=0,z1=0,z2=0;
        float bp0=0,bp1=0,bp2=0,gain=0,gainTarget=0;
    };
    static void clearEvidence(Analysis& s) noexcept {
        s.x.fill(0.f);s.r.fill(0.f);s.cursor=0;s.previous=s.lp2;
    }
    void observe(Analysis& s) noexcept {
        const float x=s.lp2-.94f*s.previous;s.previous=s.lp2;
        s.x[s.cursor]=x;
        for(int k=0;k<=order;++k){const float v=x*s.x[(s.cursor+order+1-k)%(order+1)];s.r[k]+=corrAlpha_*(v-s.r[k]);}
        if(++s.cursor>order)s.cursor=0;
    }
    void begin() noexcept {
        if(!trusted_||warmCount_<warm_||src_.r[0]<1.e-8f||dst_.r[0]<1.e-8f){neutral();work_=0;return;}
        for(auto* s:{&src_,&dst_}){s->snapshot=s->r;s->snapshot[0]*=1.01f;s->a.fill(0.f);s->a[0]=1.f;s->error=s->snapshot[0];}
        frameF0_=supportF0_;frameQuality_=quality_;work_=1;
    }
    static void step(Analysis& s,int k) noexcept {
        float acc=s.snapshot[k];for(int j=1;j<k;++j)acc+=s.a[j]*s.snapshot[k-j];
        const float r=std::clamp(-acc/std::max(s.error,s.snapshot[0]*1.e-6f),-.95f,.95f);
        const auto old=s.a;for(int j=1;j<k;++j)s.a[j]=old[j]+r*old[k-j];
        s.a[k]=r;s.error*=1.f-r*r;
    }
    float power(const Analysis& s,int b)const noexcept {
        float re=1.f,im=0.f;for(int k=1;k<=order;++k){re+=s.a[k]*cosine_[b][k];im+=s.a[k]*sine_[b][k];}
        return std::max(1.e-5f,re*re+im*im);
    }
    void neutral() noexcept {for(auto& e:eq_){e.target={1,0,0,0,0};e.gainTarget=0.f;}}
    void finish() noexcept {
        if(!trusted_){neutral();return;}
        float mean=0;for(float v:logRatio_)mean+=v;mean/=bands;
        constexpr float cap=.4605170186f; // +/-4 dB amplitude per section
        for(int b=0;b<bands;++b){
            float logGain=.8f*std::clamp(logRatio_[b]-mean,-cap,cap);
            auto& e=eq_[b];
            if(adaptive_){
                constexpr std::array<float,bands> hz{{550.f,1000.f,1700.f,2800.f,4300.f}};
                // Use the more conservative of snapshot and current
                // pitch/quality; a new octave jump must not amplify stale evidence.
                const float support=harmonicSupport(hz[b],std::max(frameF0_,supportF0_));
                logGain*=support*std::min(frameQuality_,quality_);
                e.gainTarget=std::exp(logGain)-1.f;
                if(b==0)lastSupport_=support;
                maxDb_=std::max(maxDb_,std::abs(logGain)*8.685889638f);
                continue;
            }
            const float A=std::exp(.5f*logGain),a0=1.f+e.alpha/A;
            e.target={(1.f+e.alpha*A)/a0,-2.f*e.cos/a0,(1.f-e.alpha*A)/a0,-2.f*e.cos/a0,(1.f-e.alpha/A)/a0};
            maxDb_=std::max(maxDb_,std::abs(logGain)*8.685889638f);
        }
        ++accepted_;
    }
    Analysis src_{},dst_{};std::array<Eq,bands> eq_{};
    std::array<std::array<float,order+1>,bands> cosine_{},sine_{};
    std::array<float,bands> logRatio_{};
    float lowAlpha_=0,corrAlpha_=0,smooth_=0,blend_=0,maxDb_=0;
    int decimation_=4,decCounter_=0,hop_=120,hopCounter_=0,work_=0,warm_=480,warmCount_=0;
    bool trusted_=false,adaptive_=false;std::uint64_t accepted_=0,releases_=0;
    float supportF0_=0,quality_=0,frameF0_=0,frameQuality_=0,lastSupport_=0;
};
} // namespace armix::harmony
