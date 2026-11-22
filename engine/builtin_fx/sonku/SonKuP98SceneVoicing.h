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
    // 4/5 are opt-in ImmerSIB-only profile IDs. Existing SONK v12 IDs
    // 0..3 retain byte-for-byte matching coefficients and meaning.
    enum : int { Neutral=0, Signature=1, LiveStudio=2, Cinema=3,
                 ImmerSIBDetail=4, ImmerSIBImpact=5 };
    // Opt-in V7, before prepare; old SonKu and published ImmerSIB voicings
    // remain EXACTLY the old profile at zero. No extra IIR / delay.
    void enableImmerSIBMusicalBalance(bool enabled) noexcept {
        musicalBalanceV7_=enabled;
    }
    bool prepare(double sampleRate) noexcept {
        if (!std::isfinite(sampleRate) ||
            (std::abs(sampleRate-44100.)>.1 && std::abs(sampleRate-48000.)>.1))
            return false;
        constexpr double pi=3.1415926535897932384626433832795;
        punchCoeff_=bandpass(sampleRate,86.,.85);
        voiceCoeff_=bandpass(sampleRate,2050.,.92);
        // Product-only voicing 4/5: broad mid-vocal chest/body anchor,
        // and upper-mid masking relief. All coefficients prepared OFF RT.
        bodyCoeff_=bandpass(sampleRate,285.,.72);
        edgeCoeff_=bandpass(sampleRate,3450.,.78);
        bassAlpha_=static_cast<float>(1.-std::exp(-2.*pi*130./sampleRate));
        airAlpha_=static_cast<float>(1.-std::exp(-2.*pi*5200./sampleRate));
        slew_=static_cast<float>(1.-std::exp(-1./(sampleRate*.055)));
        reset();
        return true;
    }
    void reset() noexcept {
        gains_={};center1_={};center2_={};punch_={};voice_={};
        bass_={};air_={};earlyAir_={};sidePresence1_={};
        sidePresence2_={};sideAirLow_=0.f;
        bodyCenter_={};sideEdge_={};earEdge_={};
        warmStereo_={};warmStereoGain_=0.f;
    }
    static constexpr bool valid(int scene) noexcept {
        return scene>=Neutral && scene<=ImmerSIBImpact;
    }
    // Only explicit preset changes set an ID. 55ms interpolates changes in
    // gains, with no per-sample coefficient recompute, memory allocation or
    // trigonometry. Silent profile defaults never alter P9.7 float order.
    Stereo source(float left,float right,int scene,bool enabled) noexcept {
        auto target=curves_[enabled && valid(scene)?scene:Neutral];
        // Owner listening v5: +3.3dB at 272Hz and distinct cuts
        // around 2.2/3.5/5.5k made music comfortable. V6 already corrects
        // DIRECT HRTF at 2.2k. V7 uses existing 285Hz BPF coefficients
        // INDEPENDENTLY on L and R (pan preserving), plus the existing
        // paired EAR 3.45k de-harsh to avoid another global EQ graph.
        // Original 55ms slew prevents sudden changes. No dry/side injection.
        const bool privateBalance=musicalBalanceV7_ && enabled &&
            (scene==ImmerSIBDetail || scene==ImmerSIBImpact);
        if(privateBalance) {
            // Broad 3.45k existing ear contour, helps 2.5..6k without
            // new global EQ, time-varying detector or directional HRIR.
            target[10]+=.24f;
        }
        // Warm body MUST NOT add equally to L and R source MID:
        // a hard-left guitar would acquire a NEW right speaker channel.
        // Use two independent states and ONE shared original BPF coeff so
        // L and R receive exactly the SAME linear warm transfer: panned
        // instrument position, source L/R phase and anti-phase are kept.
        const float warmTarget=privateBalance?.22f:0.f;
        warmStereoGain_+=slew_*(warmTarget-warmStereoGain_);
        if(std::abs(warmStereoGain_-warmTarget)<1.e-6f)
            warmStereoGain_=warmTarget;
        for(unsigned i=0;i<gains_.size();++i){
            gains_[i]+=slew_*(target[i]-gains_[i]);
            if(std::abs(gains_[i]-target[i])<1.e-6f)gains_[i]=target[i];
        }
        const float center=.5f*(left+right);
        const float band=center2_.run(voiceCoeff_,center1_.run(voiceCoeff_,center));
        // ImmerSIB selective M/S masking relief BEFORE measured HRIR:
        // suppress only competing SIDE presence (not stems or vocals),
        // and retain/enlarge lateral high-air image >5.2kHz. The two
        // original KEMAR speaker paths remain the sole binaural renderer.
        // Legacy voicing 0..3 has zero extra gains: same PCM arithmetic.
        const float side=.5f*(left-right);
        const float sideBand=sidePresence2_.run(voiceCoeff_,
            sidePresence1_.run(voiceCoeff_,side));
        sideAirLow_+=airAlpha_*(side-sideAirLow_);
        // Center-dominant material (often lead vocal, NOT a detected
        // vocal stem) receives broad 285Hz body before virtual speakers.
        // Instruments in the stereo difference are softly notched around
        // 3.45kHz, where guitars/cymbals can mask a center vocal.
        const float body=bodyCenter_.run(bodyCoeff_,center);
        const float edge=sideEdge_.run(edgeCoeff_,side);
        if(gains_[0]==0.f&&gains_[6]==0.f&&gains_[7]==0.f&&
           gains_[8]==0.f&&gains_[9]==0.f)
            return {left,right}; // profiles 0..3: source stays identical
        const float centerBoost=gains_[0]*band+gains_[8]*body;
        const float lateralAir=gains_[7]*(side-sideAirLow_);
        const float maskedSide=gains_[6]*sideBand+gains_[9]*edge;
        if(warmStereoGain_!=0.f) {
            // Already-prepared 285Hz BPF coeff; only two per-source state
            // variables, no new filter design or new FIR/FX graph.
            const float warmL=warmStereo_[0].run(bodyCoeff_,left);
            const float warmR=warmStereo_[1].run(bodyCoeff_,right);
            return {left+centerBoost-maskedSide+lateralAir+
                        warmStereoGain_*warmL,
                    right+centerBoost+maskedSide-lateralAir+
                        warmStereoGain_*warmR};
        }
        return {left+centerBoost-maskedSide+lateralAir,
                right+centerBoost+maskedSide-lateralAir};
    }
    Stereo early(float left,float right) noexcept {
        const std::array<float,2> x{left,right};
        float out[2]{};
        for(int c=0;c<2;++c){
            earlyAir_[c]+=airAlpha_*(x[c]-earlyAir_[c]);
            out[c]=gains_[4]==0.f?x[c]:
                x[c]+gains_[4]*(x[c]-earlyAir_[c]);
        }
        // Slightly enrich only the ALREADY directional KEMAR early-image
        // interaural high-air field (>5.2kHz). The original center (often
        // vocal) remains unchanged. No separate HRTF, delay or stem guess.
        if(gains_[11]!=0.f) {
            const float lateralAir=.5f*((x[0]-earlyAir_[0])-
                                        (x[1]-earlyAir_[1]));
            const float spread=gains_[11]*lateralAir;
            out[0]+=spread;
            out[1]-=spread;
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
            const float harsh=earEdge_[c].run(edgeCoeff_,x[c]);
            if(gains_[1]==0.f&&gains_[2]==0.f&&gains_[3]==0.f&&
               gains_[5]==0.f&&gains_[10]==0.f){
                out[c]=x[c];
            }else{
                // Avoid the old cumulative 2-5kHz presence/air lifts.
                // A small broad EAR cut after the original measured HRIR
                // helps reduce fatigue, keeping no extra crossfeed/delay.
                out[c]=x[c]+gains_[1]*bass_[c]+gains_[2]*kick+
                       gains_[3]*vocal+gains_[5]*(x[c]-air_[c])-
                       gains_[10]*harsh;
            }
        }
        return {out[0],out[1]};
    }
private:
    // channels: center-source, bass body, bass kick, vocal presence,
    // first-image air, final-ear air, SIDE presence masking relief,
    // SIDE high-air field, MID chest body, SIDE harsh relief, EAR
    // de-harsh and lateral EARLY-reflection air. Prepared filters only; still no FFT, lookahead or stems.
    // Signature is designed for high clarity AND physical music body.
    // Studio adjustment deliberately gentler than Cinema. Venue profile
    // is ALL ZEROS: its approved P9.7 acoustic graph is immutable.
    static constexpr std::array<std::array<float,12>,6> curves_{{
        // Columns: upper-mid source; LF; kick; ear-presence; early air;
        // ear air; SIDE 2.05k; SIDE HF; center 285Hz body;
        // SIDE 3.45k masking; EAR 3.45k fatigue; EARLY lateral air.
        {{0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f}}, // Venue EXACT
        {{.22f,.15f,.17f,.15f,.16f,.070f,0.f,0.f,0.f,0.f,0.f,0.f}}, // Signature
        {{.035f,.045f,.055f,.045f,.04f,.020f,0.f,0.f,0.f,0.f,0.f,0.f}}, // Live Studio
        {{.075f,.15f,.15f,.060f,.065f,.035f,0.f,0.f,0.f,0.f,0.f,0.f}}, // Cinema
        // ImmerSIB-only: warmer front-center body and controlled upper
        // mids, not another 2k/5k treble boost. LF concert impact kept.
        {{.09f,.11f,.11f,.035f,.025f,.000f,.19f,.015f,.36f,.23f,.13f,.18f}}, // Detail Warm
        {{.08f,.22f,.20f,.030f,.025f,.000f,.16f,.015f,.29f,.20f,.12f,.14f}}  // Impact Warm
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
    Coeff punchCoeff_{},voiceCoeff_{},bodyCoeff_{},edgeCoeff_{};
    Filter center1_{},center2_{};
    std::array<Filter,2> punch_{};
    std::array<std::array<Filter,2>,2> voice_{};
    std::array<float,2> bass_{},air_{},earlyAir_{};
    Filter sidePresence1_{},sidePresence2_{},bodyCenter_{},sideEdge_{};
    std::array<Filter,2> earEdge_{};
    std::array<Filter,2> warmStereo_{}; // V7 actual L/R source only
    float sideAirLow_=0.f;
    std::array<float,12> gains_{};
    float bassAlpha_=0.f,airAlpha_=0.f,slew_=0.f;
    bool musicalBalanceV7_=false; // explicit ImmerSIB-only user A/B
    float warmStereoGain_=0.f;      // original 55ms gain slew
};
} // namespace armix::sonku::p98
