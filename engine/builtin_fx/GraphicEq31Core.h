// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace armix::builtin_fx {

// IEC/ISO one-third-octave nominal centres, as used on dbx 31-band consoles.
// Fixed frequencies are an immutable DSP contract, independent of GUI labels.
inline constexpr std::array<double,31> graphicEq31Hz {{
    20.,25.,31.5,40.,50.,63.,80.,100.,125.,160.,200.,250.,
    315.,400.,500.,630.,800.,1000.,1250.,1600.,2000.,2500.,
    3150.,4000.,5000.,6300.,8000.,10000.,12500.,16000.,20000.
}};
inline constexpr std::array<const char*,31> graphicEq31Labels {{
    "20","25","31.5","40","50","63","80","100","125","160","200",
    "250","315","400","500","630","800","1k","1.25k","1.6k",
    "2k","2.5k","3.15k","4k","5k","6.3k","8k","10k",
    "12.5k","16k","20k"
}};

class GraphicEq31Core final {
public:
    static constexpr std::size_t bandCount=graphicEq31Hz.size();
    static constexpr double maximumDb=12.;
    static constexpr double nominalQ=4.318473; // one-third-octave bandwidth

    bool prepare(double rate) noexcept {
        ready_=std::isfinite(rate)&&rate>=8000.&&rate<=384000.;
        if(!ready_)return false;
        rate_=rate;
        rampFrames_=std::max(32,static_cast<int>(std::round(rate_*0.005)));
        // prepare() belongs to the unpublished CONTROL lifecycle. Precompute
        // expensive functions here, never on the sample-processing callback.
        for(std::size_t index=0;index<dbFactors_.size();++index)
            dbFactors_[index]=std::pow(10.,(factorMinDb_+
                factorStepDb_*static_cast<double>(index))/40.);
        constexpr double pi=3.1415926535897932384626433832795;
        activeMask_=0;
        for(std::size_t i=0;i<bandCount;++i) {
            const double omega=2.*pi*graphicEq31Hz[i]/rate_;
            bandConstants_[i]={std::cos(omega),
                               std::sin(omega)/(2.*nominalQ)};
            auto& b=bands_[i];
            b.current=b.target=coefficients(i,b.db);
            b.step={};b.frames=0;
            b.l1=b.l2=b.r1=b.r2=0.;
            if(b.db!=0.)activeMask_|=std::uint32_t{1}<<i;
        }
        return true;
    }
    bool ready() const noexcept {return ready_;}
    double sampleRate() const noexcept {return rate_;}
    // One prepared table also covers the post-EQ Output Level [-24,+6] dB.
    // The APVTS publishes 0.1 dB steps; linear interpolation safely handles
    // out-of-grid host/state values without any transcendental RT operation.
    float linearGainForDb(float db) const noexcept {
        if(!ready_ || !std::isfinite(db))return 1.f;
        const double a=factorForDb(static_cast<double>(db));
        return static_cast<float>(a*a);
    }
    double gainDb(std::size_t i) const noexcept {
        return i<bandCount?bands_[i].db:0.;
    }
    bool setGain(std::size_t i,double db) noexcept {
        if(i>=bandCount||!std::isfinite(db)||db < -maximumDb||db>maximumDb)
            return false;
        auto& b=bands_[i];
        if(std::abs(b.db-db)<1e-5)return true;
        b.db=db;
        if(!ready_)return true;
        b.target=coefficients(i,db);
        const double inv=1./static_cast<double>(rampFrames_);
        b.step={
            (b.target.b0-b.current.b0)*inv,
            (b.target.b1-b.current.b1)*inv,
            (b.target.b2-b.current.b2)*inv,
            (b.target.a1-b.current.a1)*inv,
            (b.target.a2-b.current.a2)*inv
        };
        b.frames=rampFrames_;
        activeMask_|=std::uint32_t{1}<<i;
        return true;
    }
    // Flat means no ramp and no pending history cleanup. The bit remains
    // set for the whole final ramp block, even when the target is zero.
    // This preserves the old block-boundary re-enable behavior exactly.
    bool flatSettled() const noexcept {return ready_&&activeMask_==0;}
    // Exact unity shortcut for the host wrapper. Reject nonfinite and
    // subnormal PCM: the full path owns sanitization and FTZ/DAZ semantics.
    bool tryUnityPeak(const float* left,const float* right,std::size_t frames,
                      float& peak) const noexcept {
        if(!flatSettled()||!left||!right||!frames)return false;
        std::uint32_t maximum=0;
        for(std::size_t n=0;n<frames;++n){
            const auto l=std::bit_cast<std::uint32_t>(left[n])&0x7fffffffu;
            const auto r=std::bit_cast<std::uint32_t>(right[n])&0x7fffffffu;
            if(l>=0x7f800000u||r>=0x7f800000u||
               (l!=0&&l<0x00800000u)||(r!=0&&r<0x00800000u))return false;
            maximum=std::max(maximum,std::max(l,r));
        }
        peak=std::bit_cast<float>(maximum);return true;
    }
    // Per-band, in-place stereo cascade: the active band processes the
    // entire buffer before the next band. Flat EQ skips all 31 biquads,
    // avoiding work in the default zero-gain state without extra buffers.
    // The coefficient ramp is sample-accurate and remains click-free.
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right)return;
        // A third-party upstream FX may deliver a non-finite sample. Never
        // let it poison the persistent IIR memories for every later block.
        // Bounded in-place validation; ordinary finite PCM is bit-unchanged.
        bool invalidLeft=false,invalidRight=false;
        for(std::size_t n=0;n<frames;++n) {
            if(!std::isfinite(left[n])){left[n]=0.f;invalidLeft=true;}
            if(!std::isfinite(right[n])){right[n]=0.f;invalidRight=true;}
        }
        // The stereo filters have independent delay memories. A fault on
        // one input must not erase the other channel's healthy IIR tail.
        if(invalidLeft||invalidRight)
            resetHistories(invalidLeft,invalidRight);
        bool processed=false;
        // Visit only active/ramping/pending-cleanup bands in ascending
        // order. Inactive bands have already been reset; never touch their
        // cache lines on steady sparse/flat curves.
        auto remaining=activeMask_;
        while(remaining){
            const auto i=static_cast<std::size_t>(std::countr_zero(remaining));
            const auto bit=std::uint32_t{1}<<i;
            remaining&=~bit;
            auto& b=bands_[i];
            if(b.frames==0 && b.db==0.) {
                // Flush stale IIR memories after a completed ramp-to-unity,
                // so re-enabling a band never exposes a frozen old history.
                b.l1=b.l2=b.r1=b.r2=0.;
                activeMask_&=~bit;
                continue;
            }
            processed=true;
            for(std::size_t n=0;n<frames;++n) {
                if(b.frames>0) {
                    b.current.b0+=b.step.b0;b.current.b1+=b.step.b1;
                    b.current.b2+=b.step.b2;b.current.a1+=b.step.a1;
                    b.current.a2+=b.step.a2;
                    if(--b.frames==0)b.current=b.target;
                }
                const auto& co=b.current;
                const double l=left[n],r=right[n];
                const double yl=co.b0*l+b.l1;
                b.l1=co.b1*l-co.a1*yl+b.l2;
                b.l2=co.b2*l-co.a2*yl;
                const double yr=co.b0*r+b.r1;
                b.r1=co.b1*r-co.a1*yr+b.r2;
                b.r2=co.b2*r-co.a2*yr;
                left[n]=static_cast<float>(yl);
                right[n]=static_cast<float>(yr);
            }
        }
        // Extreme, invalid upstream amplitudes may overflow when converting
        // double biquad results back to float. This is an error-recovery path,
        // not a musical limiter or automatic gain control. Finite >0 dBFS
        // PCM remains untouched for the operator to manage headroom.
        if(processed) {
            bool invalidLeft=false,invalidRight=false;
            for(std::size_t n=0;n<frames;++n) {
                if(!std::isfinite(left[n])){left[n]=0.f;invalidLeft=true;}
                if(!std::isfinite(right[n])){right[n]=0.f;invalidRight=true;}
            }
            if(invalidLeft||invalidRight)
                resetHistories(invalidLeft,invalidRight);
        }
    }
    void reset() noexcept {resetHistories(true,true);}
private:
    struct Coeff {
        double b0=1.,b1=0.,b2=0.,a1=0.,a2=0.;
    };
    struct Band {
        double db=0.;
        Coeff current{},target{},step{};
        int frames=0;
        double l1=0.,l2=0.,r1=0.,r2=0.;
    };
    void resetHistories(bool left,bool right) noexcept {
        for(auto& b:bands_) {
            if(left)b.l1=b.l2=0.;
            if(right)b.r1=b.r2=0.;
        }
    }
    static constexpr double factorMinDb_=-24.;
    static constexpr double factorStepDb_=0.1;
    static constexpr std::size_t factorCount_=361; // -24..+12 inclusive
    struct BandConstant { double cs=1.,alpha=0.; };
    double factorForDb(double db) const noexcept {
        const double position=std::clamp(
            (db-factorMinDb_)/factorStepDb_,0.,
            static_cast<double>(factorCount_-1));
        const auto lower=static_cast<std::size_t>(position);
        if(lower>=factorCount_-1)return dbFactors_.back();
        const double fraction=position-static_cast<double>(lower);
        return dbFactors_[lower]+fraction*
            (dbFactors_[lower+1]-dbFactors_[lower]);
    }
    Coeff coefficients(std::size_t i,double db) const noexcept {
        // This is called during realtime edits. Only bounded arithmetic,
        // one table interpolation and a division; no sin/cos/pow/alloc.
        const double hz=graphicEq31Hz[i];
        if(std::abs(db)<1e-5 || hz>=rate_*0.45)return {};
        const auto& constant=bandConstants_[i];
        const double a=factorForDb(db);
        const double norm=1./(1.+constant.alpha/a);
        return {(1.+constant.alpha*a)*norm,(-2.*constant.cs)*norm,
                (1.-constant.alpha*a)*norm,(-2.*constant.cs)*norm,
                (1.-constant.alpha/a)*norm};
    }
    std::array<double,factorCount_> dbFactors_{};
    std::array<BandConstant,bandCount> bandConstants_{};
    std::array<Band,bandCount> bands_{};
    double rate_=0.;
    int rampFrames_=240;
    std::uint32_t activeMask_=0;
    bool ready_=false;
};
} // namespace armix::builtin_fx
