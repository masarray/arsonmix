// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GuitarFactoryTones.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Portable, allocation-free M1 DSP. No second audio callback or vendor model.
// Cabinet uses a *generated original FIR*, NOT a sampled commercial cab IR;
// neural amp captures/hi-fi IR selection are explicitly later milestones.
namespace armix::guitarfx {
class GuitarRigCore final {
public:
    static constexpr int cabinetTaps=65;
    static constexpr int cabinetLatencySamples=(cabinetTaps-1)/2;
    struct Coefficients {
        float gateThreshold=0.001f;
        float gateCloseThreshold=0.0007f;
        float gateAttack=0.5f,gateRelease=0.999f,gateGainSmooth=0.995f;
        float driveGain=2.f;
        float ampGain=4.f,ampTonePole=0.5f,ampVolume=0.7f;
    };

    static constexpr Program startingProgram() noexcept {
        return factoryTone(0);
    }

    // M1 supports exactly the four stages represented by its native
    // hardware editor. The generic versioned Program can express richer
    // future graphs, but this DSP never silently accepts a mismatched UI.
    static constexpr bool supportsProgram(const Program& p) noexcept {
        return validate(p)==ProgramError::none && p.count==4 &&
            p.blocks[0].module==Module::gate &&
            p.blocks[1].module==Module::overdrive &&
            p.blocks[2].module==Module::amplifier &&
            p.blocks[3].module==Module::cabinet;
    }

    // CONTROL only: validate complete program, construct FIR/filter constants,
    // and reset all audio histories before publication. On failure, reject
    // without mutating an existing ready core.
    bool prepare(double rate,const Program& program) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.||
           !supportsProgram(program))return false;
        for(std::size_t i=0;i<program.count;++i)
            if(program.blocks[i].enabled && !supported(program.blocks[i].module))
                return false;
        std::array<float,cabinetTaps> nextIR{};
        if(!makeCabinetIR(rate,nextIR))return false;
        std::array<Coefficients,maxBlocks> nextCoeff{};
        for(std::size_t i=0;i<program.count;++i){
            const Block& block=program.blocks[i];
            auto& c=nextCoeff[i];
            const float param=block.values[0];
            if(block.module==Module::gate){
                const float db=-80.f+50.f*param;
                c.gateThreshold=static_cast<float>(std::pow(10.,db/20.));
                c.gateCloseThreshold=c.gateThreshold*0.65f;
                c.gateAttack=static_cast<float>(std::exp(-1./(0.002*rate)));
                c.gateRelease=static_cast<float>(std::exp(-1./(0.075*rate)));
                c.gateGainSmooth=static_cast<float>(std::exp(-1./(0.0025*rate)));
            } else if(block.module==Module::overdrive){
                c.driveGain=1.f+18.f*param*param;
            } else if(block.module==Module::amplifier) {
                c.ampGain=1.f+22.f*param*param;
                const float cutoff=3200.f+4600.f*block.values[1];
                c.ampTonePole=static_cast<float>(
                    std::exp(-2.*pi*cutoff/rate));
                c.ampVolume=0.3f+0.9f*block.values[2];
            }
        }
        program_=program;
        coeff_=nextCoeff;
        ir_=nextIR;
        lane_={};
        ready_=true;
        return true;
    }
    void reset() noexcept {lane_={};}
    bool isReady() const noexcept {return ready_;}
    const Program& program() const noexcept {return program_;}
    static bool supported(Module m) noexcept {
        return m==Module::gate||m==Module::overdrive||
               m==Module::amplifier||m==Module::cabinet;
    }
    // PCM callback only: no locks, heap, IR generation, logging or syscalls.
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right)return;
        for(std::size_t i=0;i<frames;++i){
            left[i]=render(0,left[i]);
            right[i]=render(1,right[i]);
        }
    }
private:
    static constexpr double pi=3.14159265358979323846;
    struct BlockState {
        float envelope=0.f,gateGain=0.f;
        float driveIn=0.f,driveOut=0.f;
        float ampTone=0.f,ampIn=0.f,ampOut=0.f;
        bool gateOpen=false;
    };
    struct ChannelState {
        std::array<BlockState,maxBlocks> modules{};
        std::array<float,cabinetTaps> cabDelay{};
        std::size_t cabCursor=0;
    };
    // Bounded 65-tap symmetric, band-limited handcrafted speaker coloration.
    // Latency is exactly the integer group delay (32 samples at all rates).
    // It is NOT an acoustic capture of any named cabinet.
    static bool makeCabinetIR(double rate,
                               std::array<float,cabinetTaps>& dst) noexcept {
        constexpr double low=95.,high=4900.;
        constexpr double test=1000.;
        const int center=cabinetLatencySamples;
        for(int i=0;i<cabinetTaps;++i){
            const int n=i-center;
            const double ideal=n==0?2.*(high-low)/rate:
                (std::sin(2.*pi*high*n/rate)-
                 std::sin(2.*pi*low*n/rate))/(pi*n);
            const double window=0.5-0.5*std::cos(
                2.*pi*static_cast<double>(i)/(cabinetTaps-1));
            dst[static_cast<std::size_t>(i)]=static_cast<float>(ideal*window);
        }
        // Normalize at 1kHz rather than DC (this is a band-pass speaker).
        double real=0.,imag=0.;
        for(int i=0;i<cabinetTaps;++i){
            const double phase=2.*pi*test*i/rate;
            real+=dst[static_cast<std::size_t>(i)]*std::cos(phase);
            imag-=dst[static_cast<std::size_t>(i)]*std::sin(phase);
        }
        const double magnitude=std::sqrt(real*real+imag*imag);
        if(!std::isfinite(magnitude)||magnitude<1.e-5)return false;
        for(auto& value:dst)value=static_cast<float>(value/magnitude);
        return true;
    }
    static float saturate(float x) noexcept {
        // Smooth, bounded asymptotic overdrive; no tanh in PCM callback.
        return x/(1.f+std::fabs(x));
    }
    float render(std::size_t channel,float input) noexcept {
        auto& lane=lane_[channel];
        float sample=std::isfinite(input)?input:0.f;
        for(std::size_t index=0;index<program_.count;++index){
            const Block& block=program_.blocks[index];
            if(!block.enabled && block.module!=Module::cabinet)continue;
            auto& history=lane.modules[index];
            const Coefficients& c=coeff_[index];
            switch(block.module){
                case Module::gate: {
                    const float magnitude=std::fabs(sample);
                    const float k=magnitude>history.envelope?
                        c.gateAttack:c.gateRelease;
                    history.envelope=k*history.envelope+(1.f-k)*magnitude;
                    if(history.gateOpen){
                        if(history.envelope<c.gateCloseThreshold)
                            history.gateOpen=false;
                    }else if(history.envelope>c.gateThreshold)history.gateOpen=true;
                    const float target=history.gateOpen?1.f:0.f;
                    history.gateGain=c.gateGainSmooth*history.gateGain+
                                     (1.f-c.gateGainSmooth)*target;
                    sample*=history.gateGain;
                    break;
                }
                case Module::overdrive: {
                    // One-pole DC-bounded brightening before asymmetrical softclip.
                    const float hp=sample-history.driveIn+0.993f*history.driveOut;
                    history.driveIn=sample;history.driveOut=hp;
                    const float driven=c.driveGain*hp;
                    sample=1.65f*(saturate(driven+0.08f)-saturate(0.08f));
                    break;
                }
                case Module::amplifier: {
                    const float hp=sample-history.ampIn+0.997f*history.ampOut;
                    history.ampIn=sample;history.ampOut=hp;
                    const float bias=0.08f;
                    const float driven=c.ampGain*hp;
                    const float shaped=2.2f*
                        (saturate(driven+bias)-saturate(bias));
                    history.ampTone=c.ampTonePole*history.ampTone+
                                    (1.f-c.ampTonePole)*shaped;
                    sample=history.ampTone*c.ampVolume;
                    break;
                }
                case Module::cabinet: {
                    lane.cabDelay[lane.cabCursor]=sample;
                    if(block.enabled){
                        float result=0.f;
                        std::size_t cursor=lane.cabCursor;
                        for(float tap:ir_){
                            result+=tap*lane.cabDelay[cursor];
                            if(cursor==0)cursor=cabinetTaps-1;
                            else --cursor;
                        }
                        sample=result;
                    }else{
                        // Same fixed PDC when CAB bypass is changed offline.
                        const auto delayed=(lane.cabCursor+
                            cabinetTaps-cabinetLatencySamples)%cabinetTaps;
                        sample=lane.cabDelay[delayed];
                    }
                    lane.cabCursor=(lane.cabCursor+1)%cabinetTaps;
                    break;
                }
                default: break; // Unsupported enabled modules fail preparation.
            }
        }
        if(!std::isfinite(sample))return 0.f;
        // Prevent a malformed input transient from blasting output.
        return sample>1.25f?1.25f:sample< -1.25f?-1.25f:sample;
    }
    Program program_{};
    std::array<Coefficients,maxBlocks> coeff_{};
    std::array<float,cabinetTaps> ir_{};
    std::array<ChannelState,2> lane_{};
    bool ready_=false;
};
} // namespace armix::guitarfx
