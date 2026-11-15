// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// P9.1+P9.2: music-level foundation and gently coupled, spatialized LF room
// modes. The measured 2x2 HRIR remains the ONLY ear renderer. Room modes
// excite each WORLD-anchored source BEFORE the existing FIR; no unpositioned
// headphone sub-bus, L/R panner, fake LF oscillator or second PCM graph.
//
// This is a bounded parametric research model, NOT a measured BRIR or a
// reproduction of proprietary speaker-cabinet/room DSP. No allocation, log,
// exp, trig, IO, locks or policy decisions in process(). All fixed room-mode
// biquad coefficients and music calibration are computed at prepare().
namespace armix::sonku::p92 {
class MusicBassFoundation final {
public:
    using Bank=const std::int16_t (*)[2][hrir::taps];
    struct Sources {float left=0.f,right=0.f;};
    static constexpr int modeCount=3;
    bool prepare(double rate,Bank bank) noexcept {
        if(!bank||!std::isfinite(rate)||
           (std::abs(rate-44100.)>.1&&std::abs(rate-48000.)>.1))return false;
        rate_=static_cast<float>(rate);
        constexpr double pi=3.1415926535897932384626433832795;
        // Average the measured HRTF magnitude at representative music
        // frequencies/near-front directions. One stable broadband scalar,
        // not time-varying AGC that pumps with the music or head rotation.
        constexpr std::array<double,4> frequencies{250.,600.,1200.,2500.};
        constexpr std::array<int,3> directions{0,6,9};
        double power=0.;
        int count=0;
        for(int d:directions)for(int ear=0;ear<2;++ear)
            for(double frequency:frequencies) {
                const double omega=2.*pi*frequency/rate;
                double re=0.,im=0.;
                for(int tap=0;tap<hrir::taps;++tap) {
                    const double sample=bank[d][ear][tap]/32768.;
                    re+=sample*std::cos(omega*tap);
                    im-=sample*std::sin(omega*tap);
                }
                power+=re*re+im*im;
                ++count;
            }
        const double meanMagnitude=std::sqrt(power/count);
        if(!std::isfinite(meanMagnitude)||meanMagnitude<.01)return false;
        // In the native 2-source FIR each speaker contributes 0.5. This
        // calibrates broadband level for independent stereo programme, with
        // upper headroom bound. A fixed gain is NOT a loudness-match claim.
        directMakeup_=static_cast<float>(std::clamp(
            1./(.7071067811865476*meanMagnitude),1.0,1.90));
        bassShelfAlpha_=static_cast<float>(
            1.-std::exp(-2.*pi*105./rate));
        roomSlew_=static_cast<float>(1.-std::exp(-1./(rate*.045)));
        // Prepared modal frequencies are actual axial eigenfrequencies of
        // the three configured rooms: f=c*n/(2*dimension), with even n so
        // a central listener is not on a zero-pressure node.
        constexpr std::array<std::array<float,3>,3> dimensions{{
            {{7.2f,7.2f,3.2f}},
            {{14.4f,14.4f,4.8f}},
            {{28.8f,28.8f,7.2f}}
        }};
        constexpr std::array<float,3> desiredHz{46.f,71.f,96.f};
        constexpr std::array<float,3> quality{7.5f,9.f,10.f};
        for(int room=0;room<3;++room)
            for(int mode=0;mode<modeCount;++mode) {
                auto& program=programs_[room][mode];
                program.axis=mode==2?2:mode; // X, Y, height
                program.dimension=dimensions[room][program.axis];
                const int harmonic=std::max(2,2*static_cast<int>(std::lround(
                    desiredHz[mode]*program.dimension/343.)));
                program.harmonic=harmonic;
                program.frequency=343.f*harmonic/(2.f*program.dimension);
                if(program.frequency>=rate*.45f)return false;
                const double omega=2.*pi*program.frequency/rate;
                const double sn=std::sin(omega);
                const double alpha=sn/(2.*quality[mode]);
                const double inv=1./(1.+alpha);
                // RBJ constant-0dB-peak bandpass: no DC, no subharmonic
                // synthesis, bounded output and damping Q<10.
                program.b0=static_cast<float>(alpha*inv);
                program.b2=-program.b0;
                program.a1=static_cast<float>(-2.*std::cos(omega)*inv);
                program.a2=static_cast<float>((1.-alpha)*inv);
            }
        reset();
        return true;
    }
    void reset() noexcept {
        for(auto& perSource:state_)
            for(auto& state:perSource)state={};
        bassState_.fill(0.f);
        room_=0;
        roomBlend_=targetBlend_=0.f;
        for(auto& perSource:coupling_)perSource.fill(0.f);
    }
    float directMakeup() const noexcept{return directMakeup_;}
    void updateScene(int room,float wet,float distance,float worldCenter,
                     float spread) noexcept {
        const int next=(room>=1&&room<=3)?room:0;
        // A different room program cannot reuse the old resonator history.
        // Only 12 float delays are cleared at most once per scene switch.
        if(next!=room_) {
            room_=next;
            for(auto& perSource:state_)
                for(auto& state:perSource)state={};
            roomBlend_=0.f;
        }
        targetBlend_=room_?std::clamp(wet,0.f,1.f)*.17f:0.f;
        if(!room_)return;
        constexpr float pi=3.14159265358979323846f;
        const float azimuths[2]{worldCenter-spread*.5f,
                                worldCenter+spread*.5f};
        const auto& programs=programs_[room_-1];
        for(int source=0;source<2;++source) {
            const float r=azimuths[source]*(pi/180.f);
            const float x=distance*std::sin(r);
            const float y=distance*std::cos(r);
            for(int mode=0;mode<modeCount;++mode) {
                const auto& m=programs[mode];
                const float displacement=m.axis==0?x:m.axis==1?y:0.f;
                // Listener at room centre: even-mode listener coupling
                // = (+/-)1. Source coupling must vary with its world
                // position, not headYaw, and is bounded by |1|.
                const float phase=m.harmonic*pi*.5f+
                    m.harmonic*pi*displacement/m.dimension;
                const float listener=std::cos(m.harmonic*pi*.5f);
                coupling_[source][mode]=listener*std::cos(phase);
            }
        }
    }
    Sources process(float left,float right) noexcept {
        if(!room_&&roomBlend_<=1.e-6f)return {};
        roomBlend_+=roomSlew_*(targetBlend_-roomBlend_);
        if(roomBlend_<1.e-6f&&targetBlend_==0.f)roomBlend_=0.f;
        const float input[2]{std::isfinite(left)?left:0.f,
                             std::isfinite(right)?right:0.f};
        float out[2]{0.f,0.f};
        if(!room_)return {};
        for(int source=0;source<2;++source)
            for(int mode=0;mode<modeCount;++mode) {
                const auto& p=programs_[room_-1][mode];
                auto& state=state_[source][mode];
                const float y=p.b0*input[source]+state.z1;
                state.z1=-p.a1*y+state.z2;
                state.z2=p.b2*input[source]-p.a2*y;
                // Each mode <=0.17/3 of source amplitude at unity peak;
                // independent stereo channels are never summed in this
                // source program. Both are spatialized later by the ONE FIR.
                out[source]+=coupling_[source][mode]*y/float(modeCount);
            }
        return {roomBlend_*out[0],roomBlend_*out[1]};
    }
    void enhanceLowBody(float& left,float& right) noexcept {
        // Identical minimum-phase, broadband low shelf on each already-
        // spatialized ear. It preserves measured ear timing and does not
        // mix original dry headphone bass into the world-locked transfer.
        bassState_[0]+=bassShelfAlpha_*(left-bassState_[0]);
        bassState_[1]+=bassShelfAlpha_*(right-bassState_[1]);
        left+=.14f*bassState_[0];
        right+=.14f*bassState_[1];
    }
    float modeFrequency(int room,int mode) const noexcept {
        return room>=1&&room<=3&&mode>=0&&mode<modeCount?
            programs_[room-1][mode].frequency:0.f;
    }
private:
    struct State {float z1=0.f,z2=0.f;};
    struct Program {
        float b0=0.f,b2=0.f,a1=0.f,a2=0.f;
        float dimension=1.f,frequency=0.f;
        int axis=0,harmonic=2;
    };
    std::array<std::array<Program,modeCount>,3> programs_{};
    std::array<std::array<State,modeCount>,2> state_{};
    std::array<std::array<float,modeCount>,2> coupling_{};
    std::array<float,2> bassState_{};
    float rate_=48000.f,directMakeup_=1.f,bassShelfAlpha_=0.f;
    float roomSlew_=0.f,roomBlend_=0.f,targetBlend_=0.f;
    int room_=0;
};
} // namespace armix::sonku::p92
