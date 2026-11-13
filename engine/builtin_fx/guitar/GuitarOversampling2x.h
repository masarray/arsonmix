// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Bounded polyphase 2x anti-alias stage for nonlinear guitar Drive/Amp.
// Fixed 33-tap symmetric interpolation and decimation FIR, each with 16
// high-rate samples of group delay. Combined = 32 high-rate / 2 = exactly
// 16 BASE-RATE samples, including 2x bypass. Prepared on CONTROL only.
// No allocation, coefficient generation, file I/O, locks, or branches on
// a mutable quality setting in the realtime callback.
namespace armix::guitarfx {
class GuitarOversampling2x final {
public:
    static constexpr int taps=33;
    static constexpr int latencySamples=16;
    static constexpr int historySamples=(taps+1)/2;
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        constexpr double pi=3.14159265358979323846;
        // At 44.1/48k the passband is wide enough for guitar harmonics;
        // the measured cabinet itself already rolls off well below 14kHz.
        // A conservative transition band prevents 27kHz products folding
        // into 21kHz on the 48kHz output.
        const double cutoff=std::min(14000.,rate*0.30);
        const double fsHigh=rate*2.;
        const double fc=cutoff/fsHigh;
        std::array<float,taps> next{};
        double sum=0.;
        for(int i=0;i<taps;++i){
            const int n=i-(taps-1)/2;
            const double sinc=n==0?2.*fc:
                std::sin(2.*pi*fc*n)/(pi*n);
            const double window=0.54-0.46*std::cos(
                2.*pi*i/(taps-1));
            next[static_cast<std::size_t>(i)]=
                static_cast<float>(sinc*window);
            sum+=next[static_cast<std::size_t>(i)];
        }
        if(!std::isfinite(sum)||sum<=0.)return false;
        for(auto& v:next)v=static_cast<float>(v/sum);
        for(int i=0;i<(taps-1)/2;++i)
            next[static_cast<std::size_t>(taps-1-i)]=
                next[static_cast<std::size_t>(i)];
        kernel_=next;
        rate_=rate;
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        low_={};high_={};direct_={};
        lowCursor_=highCursor_=directCursor_=0;
    }
    bool inheritHistory(const GuitarOversampling2x& previous) noexcept {
        if(!ready_||!previous.ready_||rate_!=previous.rate_)return false;
        low_=previous.low_;high_=previous.high_;
        direct_=previous.direct_;
        lowCursor_=previous.lowCursor_;
        highCursor_=previous.highCursor_;
        directCursor_=previous.directCursor_;
        return true;
    }
    // Two output samples are interpolated from one input. Polyphase 17+16
    // multiplications, instead of two wasteful 33-tap zero-stuffed loops.
    void upsamplePair(float x,float& even,float& odd) noexcept {
        low_[lowCursor_]=x;
        float a=0.f,b=0.f;
        std::size_t pos=lowCursor_;
        for(int tap=0;tap<historySamples;++tap){
            const float previous=low_[pos];
            a+=kernel_[static_cast<std::size_t>(2*tap)]*previous;
            if(tap<historySamples-1)
                b+=kernel_[static_cast<std::size_t>(2*tap+1)]*previous;
            pos=pos==0?historySamples-1:pos-1;
        }
        lowCursor_=lowCursor_+1==historySamples?0:lowCursor_+1;
        even=a*2.f;
        odd=b*2.f;
    }
    // Decimator output taken at the EVEN high-rate phase. Combined 2x
    // interpolation+decimation FIR group delay is exactly 16 base frames.
    float decimateEven(float shapedEven,float shapedOdd) noexcept {
        const float out=downFir(shapedEven);
        (void)downFir(shapedOdd);
        return out;
    }
    // When BOTH Drive and Amp are off, preserve a real (unfiltered) dry
    // signal with identical PDC. No 2x lowpass should color this bypass.
    float bypassDelay(float dry) noexcept {
        const float out=direct_[directCursor_];
        direct_[directCursor_]=dry;
        directCursor_=directCursor_+1==latencySamples?0:directCursor_+1;
        return out;
    }
private:
    float downFir(float input) noexcept {
        high_[highCursor_]=input;
        std::size_t pos=highCursor_;
        float result=0.f;
        for(float tap:kernel_){
            result+=tap*high_[pos];
            pos=pos==0?taps-1:pos-1;
        }
        highCursor_=highCursor_+1==taps?0:highCursor_+1;
        return result;
    }
    std::array<float,taps> kernel_{};
    std::array<float,historySamples> low_{};
    std::array<float,taps> high_{};
    std::array<float,latencySamples> direct_{};
    std::size_t lowCursor_=0,highCursor_=0,directCursor_=0;
    double rate_=0.;
    bool ready_=false;
};
static_assert(GuitarOversampling2x::latencySamples==16);
} // namespace armix::guitarfx
