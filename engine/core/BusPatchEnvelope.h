// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <atomic>
namespace armix::mixer {
// One transient envelope on the already-rendered hardware/MAIN tap.
// CONTROL owns requests; AUDIO owns gain and acknowledges after publication.
// No allocation, lock, device reopen or second DSP pass.
class BusPatchEnvelope final {
public:
    enum Phase : int { idle=0, fadeOut=1, silent=2, fadeIn=3 };
    struct Block { int phase=idle; bool complete=false; };
    bool begin() noexcept {
        int expected=idle;
        return phase_.compare_exchange_strong(expected,fadeOut,std::memory_order_acq_rel);
    }
    bool ready() const noexcept {return phase_.load(std::memory_order_acquire)==silent;}
    void finish(bool resume) noexcept {
        // No new output permission: fade-in only follows existing native gates.
        int current=phase_.load(std::memory_order_acquire);
        while(current==fadeOut||current==silent)
            if(phase_.compare_exchange_weak(current,resume?fadeIn:idle,std::memory_order_acq_rel))return;
    }
    void reset() noexcept {phase_.store(idle,std::memory_order_release);}
    Block process(float* const* outputs,int channels,float* extraL,float* extraR,
                  int frames,double rate,bool rendered,bool audible) noexcept {
        const int phase=phase_.load(std::memory_order_acquire);
        if(phase==idle){gain_=1.f;return {};}
        if(!rendered||frames<=0)return {phase,false};
        const float step=1.f/std::max(1.f,static_cast<float>(rate*0.005));
        if(phase==silent||(phase==fadeIn&&!audible))gain_=0.f;
        for(int i=0;i<frames;++i){
            if(phase==fadeOut)gain_=std::max(0.f,gain_-step);
            else if(phase==fadeIn&&audible)gain_=std::min(1.f,gain_+step);
            if(outputs)for(int c=0;c<channels;++c)if(outputs[c])outputs[c][i]*=gain_;
            if(extraL)extraL[i]*=gain_;
            if(extraR)extraR[i]*=gain_;
        }
        return {phase,(phase==fadeOut&&gain_<=0.f)||(phase==fadeIn&&audible&&gain_>=1.f)};
    }
    void acknowledge(Block b) noexcept {
        if(!b.complete)return;
        int expected=b.phase;
        if(b.phase==fadeOut)phase_.compare_exchange_strong(expected,silent,std::memory_order_acq_rel);
        else if(b.phase==fadeIn)phase_.compare_exchange_strong(expected,idle,std::memory_order_acq_rel);
    }
private:
    std::atomic<int> phase_{idle};
    float gain_=1.f; // AUDIO only
};
} // namespace armix::mixer
