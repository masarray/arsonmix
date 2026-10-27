// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace armix::dsp {
// RT-only instance per physical FX slot. The control thread publishes ONLY
// the requested bypass flag; no locks/heap/plugin GUI access in audio callback.
//
// Zero-latency processors: 8ms linear wet/dry crossfade (no gain jump).
// Latency-bearing processors: fade-out / source switch at exact silence /
// fade-in. Do not sum time-misaligned dry and wet and introduce comb filtering.
// A delayed plugin may still need a latency-aligned bypass for seamless tails;
// this intentionally prioritizes click avoidance and zero extra latency.
class FxBypassEnvelope final
{
public:
    struct Gains {float wet=0.f, dry=1.f;};
    void prepare(bool bypass, double sampleRate, int pluginLatency) noexcept
    {
        const double rate=std::isfinite(sampleRate)?std::clamp(sampleRate,8000.,384000.):48000.;
        const double length=std::clamp(rate*0.008,64.,4096.);
        step_=static_cast<float>(1.0/length);
        latency_=std::max(0,pluginLatency);
        wet_=bypass?0.f:1.f;
        currentWet_=!bypass;
        targetWet_=!bypass;
        amplitude_=1.f;
        stage_=Stage::steady;
    }
    void request(bool bypass) noexcept
    {
        const bool wantWet=!bypass;
        if(wantWet==targetWet_)return;
        targetWet_=wantWet;
        if(latency_==0)return;
        if(stage_==Stage::steady)stage_=Stage::down;
        else if(stage_==Stage::down && targetWet_==currentWet_)stage_=Stage::up;
        else if(stage_==Stage::up && targetWet_!=currentWet_)stage_=Stage::down;
    }
    bool transitioning() const noexcept
    {
        return latency_==0 ? wet_!=(targetWet_?1.f:0.f) : stage_!=Stage::steady;
    }
    bool shouldProcessWet() const noexcept
    {
        return latency_==0 ? wet_>0.f||targetWet_ :
               currentWet_||targetWet_||stage_!=Stage::steady;
    }
    Gains next() noexcept
    {
        if(latency_==0)
        {
            const float target=targetWet_?1.f:0.f;
            if(wet_<target)wet_=std::min(target,wet_+step_);
            else if(wet_>target)wet_=std::max(target,wet_-step_);
            return {wet_,1.f-wet_};
        }
        if(stage_==Stage::down)
        {
            amplitude_=std::max(0.f,amplitude_-step_);
            if(amplitude_<=0.f)
            {
                amplitude_=0.f;
                currentWet_=targetWet_;
                stage_=Stage::up;
            }
        }
        else if(stage_==Stage::up)
        {
            amplitude_=std::min(1.f,amplitude_+step_);
            if(amplitude_>=1.f)stage_=Stage::steady;
        }
        return currentWet_?Gains{amplitude_,0.f}:Gains{0.f,amplitude_};
    }
    bool fullyBypassed() const noexcept {return !transitioning() && !shouldProcessWet();}
private:
    enum class Stage : unsigned char {steady,down,up};
    Stage stage_=Stage::steady;
    float wet_=1.f, amplitude_=1.f, step_=1.f/384.f;
    int latency_=0;
    bool currentWet_=true,targetWet_=true;
};
}
