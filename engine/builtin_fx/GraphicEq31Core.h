// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
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
        for(std::size_t i=0;i<bandCount;++i) {
            auto& b=bands_[i];
            b.current=b.target=coefficients(i,b.db,rate_);
            b.step={};b.frames=0;
            b.l1=b.l2=b.r1=b.r2=0.;
        }
        return true;
    }
    bool ready() const noexcept {return ready_;}
    double sampleRate() const noexcept {return rate_;}
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
        b.target=coefficients(i,db,rate_);
        const double inv=1./static_cast<double>(rampFrames_);
        b.step={
            (b.target.b0-b.current.b0)*inv,
            (b.target.b1-b.current.b1)*inv,
            (b.target.b2-b.current.b2)*inv,
            (b.target.a1-b.current.a1)*inv,
            (b.target.a2-b.current.a2)*inv
        };
        b.frames=rampFrames_;
        return true;
    }
    // Per-band, in-place stereo cascade: the active band processes the
    // entire buffer before the next band. Flat EQ skips all 31 biquads,
    // avoiding work in the default zero-gain state without extra buffers.
    // The coefficient ramp is sample-accurate and remains click-free.
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right)return;
        for(auto& b:bands_) {
            if(b.frames==0 && b.db==0.) {
                // Flush stale IIR memories after a completed ramp-to-unity,
                // so re-enabling a band never exposes a frozen old history.
                b.l1=b.l2=b.r1=b.r2=0.;
                continue;
            }
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
    }
    void reset() noexcept {
        for(auto& b:bands_)b.l1=b.l2=b.r1=b.r2=0.;
    }
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
    static Coeff coefficients(std::size_t i,double db,double rate) noexcept {
        // Bands over the physical Nyquist guard are transparent at low sample
        // rates. Preserve the operator's saved dB setting for a future rate.
        const double hz=graphicEq31Hz[i];
        if(std::abs(db)<1e-5 || hz>=rate*0.45)return {};
        constexpr double pi=3.1415926535897932384626433832795;
        const double omega=2.*pi*hz/rate;
        const double cs=std::cos(omega);
        const double alpha=std::sin(omega)/(2.*nominalQ);
        const double a=std::pow(10.,db/40.);
        const double norm=1./(1.+alpha/a);
        return {(1.+alpha*a)*norm,(-2.*cs)*norm,
                (1.-alpha*a)*norm,(-2.*cs)*norm,
                (1.-alpha/a)*norm};
    }
    std::array<Band,bandCount> bands_{};
    double rate_=0.;
    int rampFrames_=240;
    bool ready_=false;
};
} // namespace armix::builtin_fx
