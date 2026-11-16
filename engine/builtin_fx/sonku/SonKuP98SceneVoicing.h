// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// P9.8 PRIVATE named-scene voicings, never a second PCM graph or fake stem
// separator. All bands are processed on world-positioned KEMAR ears or
// genuine directional first images; center emphasis enters the two measured
// loudspeaker HRIRs BEFORE ear rendering. Mode 0 = exact P9.7 Venue/Custom.
// This class never guesses a preset from distance/reverb/gain floats.
namespace armix::sonku::p98 {
class SceneVoicing final {
public:
    struct Stereo {float left=0.f,right=0.f;};
    // These IDs are stable native-serialized values, NOT native ROOM sizes.
    enum : int { Neutral=0, Signature=1, LiveStudio=2, Cinema=3 };
    bool prepare(double sampleRate) noexcept {
        if (!std::isfinite(sampleRate) ||
            (std::abs(sampleRate-44100.)>.1 && std::abs(sampleRate-48000.)>.1))
            return false;
        constexpr double pi=3.1415926535897932384626433832795;
        punchCoeff_=bandpass(sampleRate,86.,.85);
        voiceCoeff_=bandpass(sampleRate,2050.,.92);
        bassAlpha_=static_cast<float>(1.-std::exp(-2.*pi*130./sampleRate));
        airAlpha_=static_cast<float>(1.-std::exp(-2.*pi*5200./sampleRate));
        slew_=static_cast<float>(1.-std::exp(-1./(sampleRate*.055)));
        reset();
        return true;
    }
    void reset() noexcept {
        gains_={};center1_={};center2_={};punch_={};voice_={};
        bass_={};air_={};earlyAir_={};
    }
    static constexpr bool valid(int scene) noexcept {
        return scene>=Neutral && scene<=Cinema;
    }
    // Only explicit preset changes set an ID. 55ms interpolates changes in
    // gains, with no per-sample coefficient recompute, memory allocation or
    // trigonometry. Silent profile defaults never alter P9.7 float order.
    Stereo source(float left,float right,int scene,bool enabled) noexcept {
        const auto target=curves_[enabled && valid(scene)?scene:Neutral];
        for(unsigned i=0;i<gains_.size();++i){
            gains_[i]+=slew_*(target[i]-gains_[i]);
            if(std::abs(gains_[i]-target[i])<1.e-6f)gains_[i]=target[i];
        }
        const float center=.5f*(left+right);
        const float band=center2_.run(voiceCoeff_,center1_.run(voiceCoeff_,center));
        if(gains_[0]==0.f)return {left,right};
        const float delta=gains_[0]*band;
        return {left+delta,right+delta};
    }
    Stereo early(float left,float right) noexcept {
        const std::array<float,2> x{left,right};
        float out[2]{};
        for(int c=0;c<2;++c){
            earlyAir_[c]+=airAlpha_*(x[c]-earlyAir_[c]);
            out[c]=gains_[4]==0.f?x[c]:
                x[c]+gains_[4]*(x[c]-earlyAir_[c]);
        }
        return {out[0],out[1]};
    }
    Stereo finish(float left,float right) noexcept {
        const std::array<float,2> x{left,right};
        float out[2]{};
        for(int c=0;c<2;++c){
            const float kick=punch_[c].run(punchCoeff_,x[c]);
            const float vocal=voice_[c][1].run(voiceCoeff_,
                              voice_[c][0].run(voiceCoeff_,x[c]));
            bass_[c]+=bassAlpha_*(x[c]-bass_[c]);
            air_[c]+=airAlpha_*(x[c]-air_[c]);
            if(gains_[1]==0.f&&gains_[2]==0.f&&gains_[3]==0.f&&
               gains_[5]==0.f){
                out[c]=x[c];
            }else{
                out[c]=x[c]+gains_[1]*bass_[c]+gains_[2]*kick+
                       gains_[3]*vocal+gains_[5]*(x[c]-air_[c]);
            }
        }
        return {out[0],out[1]};
    }
private:
    // channels: center-source, bass body, bass kick, vocal presence,
    // real first-image air, already-binaural direct/final air.
    // Signature is designed for high clarity AND physical music body.
    // Studio adjustment deliberately gentler than Cinema. Venue profile
    // is ALL ZEROS: its approved P9.7 acoustic graph is immutable.
    static constexpr std::array<std::array<float,6>,4> curves_{{
        {{0.f,0.f,0.f,0.f,0.f,0.f}},        // Venue / Custom: EXACT
        {{.22f,.15f,.17f,.15f,.16f,.070f}}, // Signature flagship
        {{.035f,.045f,.055f,.045f,.04f,.020f}}, // Live Studio subtle
        {{.075f,.15f,.15f,.060f,.065f,.035f}}  // Cinema LF weight
    }};
    struct Coeff {float b0=0,b1=0,b2=0,a1=0,a2=0;};
    struct Filter {
        float z1=0,z2=0;
        float run(const Coeff& c,float x) noexcept {
            const float y=c.b0*x+z1;
            z1=c.b1*x-c.a1*y+z2;
            z2=c.b2*x-c.a2*y;
            return y;
        }
    };
    static Coeff bandpass(double rate,double hz,double q) noexcept {
        constexpr double pi=3.1415926535897932384626433832795;
        const double w=2.*pi*hz/rate;
        const double a=std::sin(w)/(2.*q);
        const double inv=1./(1.+a);
        return {static_cast<float>(a*inv),0.f,static_cast<float>(-a*inv),
                static_cast<float>(-2.*std::cos(w)*inv),
                static_cast<float>((1.-a)*inv)};
    }
    Coeff punchCoeff_{},voiceCoeff_{};
    Filter center1_{},center2_{};
    std::array<Filter,2> punch_{};
    std::array<std::array<Filter,2>,2> voice_{};
    std::array<float,2> bass_{},air_{},earlyAir_{};
    std::array<float,6> gains_{};
    float bassAlpha_=0.f,airAlpha_=0.f,slew_=0.f;
};
} // namespace armix::sonku::p98
