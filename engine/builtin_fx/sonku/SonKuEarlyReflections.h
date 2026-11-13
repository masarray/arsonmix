// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Bounded, first-order image-source room for headphone externalization trials.
// Reflection image geometry follows Allen & Berkley (JASA, 1979), not
// randomized stereo delays. The ears use lightweight ITD/ILD approximation;
// this is NOT an HRTF-convolved reflection / arbitrary geometry simulation.
// No allocation, locks, worker, I/O, FFT or second device callback.
namespace armix::sonku {
class SonKuEarlyReflections final {
public:
    struct Ears {float left=0.f,right=0.f;};
    static constexpr int wallCount=4;
    static constexpr int sourceCount=2;
    static constexpr int ringFrames=8192;
    static constexpr int geometryInterval=64;
    bool prepare(double rate) noexcept {
        ready_=false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        rate_=static_cast<float>(rate);
        delaySlew_=static_cast<float>(1.-std::exp(-1./(rate*0.024)));
        gainSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.012)));
        amountSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.010)));
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& h:history_)h.fill(0.f);
        lowpass_.fill(0.f);
        for(auto& paths:tap_)for(auto& p:paths)p=Tap{};
        write_=0;untilGeometry_=0;primed_=false;
        amount_=0.f;
    }
    // Deliberately experimental and OFF by default. Legacy 8/8 direction
    // baseline stays bit-identical. A/B is set on an unpublished/offline core
    // until headphone, CPU and device gates approve native publication.
    bool setAmount(float value) noexcept {
        if(!std::isfinite(value)||value<0.f||value>1.f)return false;
        if(amountTarget_==0.f&&amount_==0.f&&value>0.f)reset();
        amountTarget_=value;
        return true;
    }
    bool active() const noexcept {return ready_&&(amount_>0.f||amountTarget_>0.f);}
    float targetAmount() const noexcept {return amountTarget_;}
    Ears process(float left,float right,double azimuth,float spread) noexcept {
        if(!active())return {};
        if(untilGeometry_==0){
            updateGeometry(azimuth,spread);
            untilGeometry_=geometryInterval;
        }
        const float x[2]{std::isfinite(left)?left:0.f,
                         std::isfinite(right)?right:0.f};
        for(int s=0;s<sourceCount;++s){
            // Absorption on the reflected path only; direct measured HRTF
            // stays unchanged. Mild low pass preserves source intelligibility.
            lowpass_[s]+=.27f*(x[s]-lowpass_[s]);
            history_[s][write_]=.70f*x[s]+.30f*lowpass_[s];
        }
        amount_+=amountSlew_*(amountTarget_-amount_);
        if(std::abs(amount_-amountTarget_)<1.e-6f)amount_=amountTarget_;
        Ears output{};
        for(int s=0;s<sourceCount;++s){
            for(int wall=0;wall<wallCount;++wall){
                auto& p=tap_[s][wall];
                p.delayL+=delaySlew_*(p.targetDelayL-p.delayL);
                p.delayR+=delaySlew_*(p.targetDelayR-p.delayR);
                p.gainL+=gainSlew_*(p.targetGainL-p.gainL);
                p.gainR+=gainSlew_*(p.targetGainR-p.gainR);
                output.left+=p.gainL*delayed(s,p.delayL);
                output.right+=p.gainR*delayed(s,p.delayR);
            }
        }
        write_=(write_+1)&(ringFrames-1);
        --untilGeometry_;
        // Preserve the headroom of the native direct path: no artificial
        // direction-dependent boost at FRONT or REAR.
        output.left*=amount_;
        output.right*=amount_;
        return output;
    }
private:
    struct Tap {
        float delayL=1.f,delayR=1.f,gainL=0.f,gainR=0.f;
        float targetDelayL=1.f,targetDelayR=1.f;
        float targetGainL=0.f,targetGainR=0.f;
    };
    float delayed(int source,float delay) const noexcept {
        // Fractional delay interpolated in a fixed-size power-of-two ring.
        const float bounded=std::clamp(delay,1.f,static_cast<float>(ringFrames-3));
        const int integer=static_cast<int>(bounded);
        const float fract=bounded-static_cast<float>(integer);
        const int i0=(write_-integer)&(ringFrames-1);
        const int i1=(i0-1)&(ringFrames-1);
        return history_[source][i0]*(1.f-fract)+history_[source][i1]*fract;
    }
    void updateGeometry(double azimuth,float spread) noexcept {
        constexpr float pi=3.14159265358979323846f;
        constexpr float sourceDistance=1.60f; // meters
        constexpr float halfWidth=3.20f,halfDepth=4.00f;
        constexpr float speedOfSound=343.f;
        constexpr float earHalfTime=.00031f; // max +/-0.31ms
        constexpr std::array<float,wallCount> walls{.74f,.74f,.67f,.67f};
        for(int source=0;source<sourceCount;++source){
            const float degrees=static_cast<float>(azimuth)
                +(source==0?-.5f:.5f)*spread;
            const float rad=degrees*(pi/180.f);
            const float sx=sourceDistance*std::sin(rad);
            const float sy=sourceDistance*std::cos(rad);
            const std::array<std::array<float,2>,wallCount> images{{
                {{-2.f*halfWidth-sx,sy}},{{2.f*halfWidth-sx,sy}},
                {{sx,2.f*halfDepth-sy}},{{sx,-2.f*halfDepth-sy}}
            }};
            for(int wall=0;wall<wallCount;++wall){
                const float x=images[wall][0],y=images[wall][1];
                const float path=std::sqrt(x*x+y*y);
                const float directionX=x/path;
                const float extra=(path-sourceDistance)*rate_/speedOfSound;
                // Constant 0.31ms ear baseline makes differential ITD
                // symmetric while keeping every reflection causal.
                const float common=extra+earHalfTime*rate_;
                const float earDelta=directionX*earHalfTime*rate_;
                const float level=.28f*walls[wall]*(sourceDistance/path);
                auto& tap=tap_[source][wall];
                tap.targetDelayL=std::clamp(common+earDelta,1.f,
                                           static_cast<float>(ringFrames-3));
                tap.targetDelayR=std::clamp(common-earDelta,1.f,
                                           static_cast<float>(ringFrames-3));
                tap.targetGainL=level*(1.f-.13f*directionX);
                tap.targetGainR=level*(1.f+.13f*directionX);
                if(!primed_){
                    tap.delayL=tap.targetDelayL;
                    tap.delayR=tap.targetDelayR;
                    tap.gainL=tap.targetGainL;
                    tap.gainR=tap.targetGainR;
                }
            }
        }
        primed_=true;
    }
    static_assert((ringFrames&(ringFrames-1))==0);
    std::array<std::array<float,ringFrames>,sourceCount> history_{};
    std::array<std::array<Tap,wallCount>,sourceCount> tap_{};
    std::array<float,sourceCount> lowpass_{};
    float rate_=0.f,delaySlew_=0.f,gainSlew_=0.f,amountSlew_=0.f;
    float amount_=0.f,amountTarget_=0.f;
    int write_=0,untilGeometry_=0;
    bool ready_=false,primed_=false;
};
} // namespace armix::sonku
