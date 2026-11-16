// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// P9.1+P9.2: one existing 2x2 HRTF callback, no second audio graph.
// A complementary LF handover replaces only the direct HRIR's low portion
// with two WORLD-anchored loudspeaker sources, never headphone-locked L/R.
// Source signals are kept independent (anti-phase is NOT rectified).
// Very small measured-room-inspired axial modal returns are optional and
// separate from the DIRECT bass. This is a parametric model, NOT a measured
// subwoofer cabinet or an asserted dearVR algorithm.
namespace armix::sonku::p92 {
class WorldBassFoundation final {
public:
    struct Ears {float left=0.f,right=0.f;};
    bool prepare(double sampleRate) noexcept {
        if(!std::isfinite(sampleRate) ||
           (std::abs(sampleRate-44100.)>.1 && std::abs(sampleRate-48000.)>.1))
            return false;
        constexpr double pi=3.1415926535897932384626433832795;
        const double cutoff=165.;
        lowAlpha_=static_cast<float>(1.-std::exp(-2.*pi*cutoff/sampleRate));
        roomAlpha_=static_cast<float>(1.-std::exp(-1./(sampleRate*.060)));
        // Angle lookup avoids trig/heap in the 16-frame realtime FIR update.
        for(std::size_t i=0;i<sine_.size();++i)
            sine_[i]=static_cast<float>(std::sin((2.*pi*i)/360.));
#if defined(SONKU_P95_CINEMATIC_RESEARCH) && SONKU_P95_CINEMATIC_RESEARCH
        constexpr std::array<double,7> roomWidths{7.2,14.4,28.8,10.,22.,32.,39.};
#else
        constexpr std::array<double,3> roomWidths{7.2,14.4,28.8};
#endif
        constexpr std::array<double,3> frequencies{48.,75.,112.};
        constexpr std::array<double,3> qualities{2.0,2.8,3.8};
        for(std::size_t room=0;room<roomWidths.size();++room){
            for(std::size_t band=0;band<3;++band){
                // Real axial harmonic: n*c/(2*room-width), rounded to a
                // musically relevant LF cluster. Never invent a subharmonic.
                const double n=std::max(1.,std::round(2.*roomWidths[room]*
                    frequencies[band]/343.));
                const double hz=n*343./(2.*roomWidths[room]);
                resonators_[room][band].coeff=bandpass(sampleRate,hz,qualities[band]);
            }
        }
        reset();
        return true;
    }
    void reset() noexcept {
        for(auto& s:inputLow_)s={};
        for(auto& s:earLow_)s={};
        for(auto& room:resonators_)for(auto& resonator:room)
            resonator.state={};
        weights_={{{.62f,.38f},{.38f,.62f}}};
        roomWeights_={};
        roomGain_=0.f;
    }
    void updateDirections(float leftHead,float rightHead) noexcept {
        const float angles[2]{leftHead,rightHead};
        for(int source=0;source<2;++source){
            const float lean=.27f*lookupSine(angles[source]);
            weights_[source][0]=.5f*(1.f-lean);
            weights_[source][1]=.5f*(1.f+lean);
        }
    }
    Ears process(float inputLeft,float inputRight,float& hrtfLeft,
                 float& hrtfRight,int roomPreset,float roomAmount) noexcept {
        // Two identical minimum-phase lowpasses on BOTH paths: the high
        // direct HRIR remains exactly HRIR - LP(HRIR), without double bass.
        const float inputs[2]{inputLeft,inputRight};
        float lowSrc[2]{};
        for(int c=0;c<2;++c){
            inputLow_[c][0]+=lowAlpha_*(inputs[c]-inputLow_[c][0]);
            inputLow_[c][1]+=lowAlpha_*(inputLow_[c][0]-inputLow_[c][1]);
            lowSrc[c]=inputLow_[c][1];
        }
        float* ears[2]{&hrtfLeft,&hrtfRight};
        float world[2]{};
        for(int ear=0;ear<2;++ear){
            earLow_[ear][0]+=lowAlpha_*(*ears[ear]-earLow_[ear][0]);
            earLow_[ear][1]+=lowAlpha_*(earLow_[ear][0]-earLow_[ear][1]);
            // With the old P8.2 +0.82 shelf disabled, a stereo 2x LF
            // return matches the measured input RMS within ~0.5dB for
            // our 70/116Hz + 5.2kHz reference at the protected P9 gain.
            // The shared FIR output remains a single stereo signal.
            world[ear]=2.f*(weights_[0][ear]*lowSrc[0]+
                           weights_[1][ear]*lowSrc[1]);
            *ears[ear]=*ears[ear]-earLow_[ear][1]+world[ear];
        }
        const int room=std::clamp(roomPreset-1,0,
            static_cast<int>(resonators_.size())-1);
        const float target=roomPreset>0?
            std::clamp(roomAmount,0.f,1.f):0.f;
        roomGain_+=roomAlpha_*(target-roomGain_);
        Ears modal{};
        constexpr std::array<float,3> modeWeights{.14f,.105f,.075f};
        for(std::size_t r=0;r<resonators_.size();++r){
            const float active=static_cast<int>(r)==room?1.f:0.f;
            roomWeights_[r]+=roomAlpha_*(active-roomWeights_[r]);
            for(int b=0;b<3;++b){
                // Tiny modal bandpass; finite decay, zero at DC, no phase
                // cancellation of the direct LOW, no positive feedback.
                auto& resonator=resonators_[r][b];
                const float l=resonator.state[0].run(resonator.coeff,world[0]);
                const float rr=resonator.state[1].run(resonator.coeff,world[1]);
                const float level=roomWeights_[r]*roomGain_*modeWeights[b];
                modal.left+=level*l;
                modal.right+=level*rr;
            }
        }
        return modal;
    }
private:
    struct Coeff {float b0=0.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;};
    struct State {
        float z1=0.f,z2=0.f;
        float run(const Coeff& c,float input) noexcept {
            const float y=c.b0*input+z1;
            z1=c.b1*input-c.a1*y+z2;
            z2=c.b2*input-c.a2*y;
            return y;
        }
    };
    struct Resonator {Coeff coeff{};std::array<State,2> state{};};
    static Coeff bandpass(double rate,double hz,double q) noexcept {
        constexpr double pi=3.1415926535897932384626433832795;
        const double w=2.*pi*hz/rate;
        const double a=std::sin(w)/(2.*q);
        const double inv=1./(1.+a);
        return {static_cast<float>(a*inv),0.f,
                static_cast<float>(-a*inv),
                static_cast<float>(-2.*std::cos(w)*inv),
                static_cast<float>((1.-a)*inv)};
    }
    float lookupSine(float degrees) const noexcept {
        if(!std::isfinite(degrees))return 0.f;
        float bounded=degrees;
        while(bounded<0.f)bounded+=360.f;
        while(bounded>=360.f)bounded-=360.f;
        const int lo=static_cast<int>(bounded);
        const float frac=bounded-lo;
        return sine_[lo]*(1.f-frac)+sine_[lo+1]*frac;
    }
    std::array<std::array<float,2>,2> weights_{{{{.62f,.38f}},{{.38f,.62f}}}};
    std::array<std::array<float,2>,2> inputLow_{},earLow_{};
    std::array<std::array<Resonator,3>,
#if defined(SONKU_P95_CINEMATIC_RESEARCH) && SONKU_P95_CINEMATIC_RESEARCH
      7
#else
      3
#endif
      > resonators_{};
    std::array<float,361> sine_{};
    std::array<float,
#if defined(SONKU_P95_CINEMATIC_RESEARCH) && SONKU_P95_CINEMATIC_RESEARCH
      7
#else
      3
#endif
      > roomWeights_{};
    float roomGain_=0.f,lowAlpha_=0.f,roomAlpha_=0.f;
};
} // namespace armix::sonku::p92
