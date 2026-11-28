// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
namespace armix::mixer {
// One transient envelope on the already-rendered hardware/MAIN tap.
// CONTROL owns requests; AUDIO owns gain and acknowledges after publication.
// No allocation, lock, device reopen or second DSP pass.
class BusPatchEnvelope final {
public:
    enum Phase : int { idle=0, fadeOut=1, silent=2, fadeIn=3 };
    struct Block { int phase=idle; bool complete=false; std::uint64_t token=0; };
    bool begin() noexcept {
        auto expected=state_.load(std::memory_order_acquire);
        if(phaseOf(expected)!=idle)return false;
        return state_.compare_exchange_strong(expected,nextGeneration(expected)|fadeOut,std::memory_order_acq_rel);
    }
    bool ready() const noexcept {return phaseOf(state_.load(std::memory_order_acquire))==silent;}
    void finish(bool resume) noexcept {
        // No new output permission: fade-in only follows existing native gates.
        auto current=state_.load(std::memory_order_acquire);
        const auto generation=current&~phaseMask;
        while((current&~phaseMask)==generation&&(phaseOf(current)==fadeOut||phaseOf(current)==silent))
            if(state_.compare_exchange_weak(current,generation|(resume?fadeIn:idle),std::memory_order_acq_rel))return;
    }
    void reset() noexcept {
        auto current=state_.load(std::memory_order_acquire);
        while(!state_.compare_exchange_weak(current,nextGeneration(current),std::memory_order_acq_rel)){}
    }
    Block process(float* const* outputs,int channels,float* extraL,float* extraR,
                  int frames,double rate,bool rendered,bool audible,
                  float* programmeL=nullptr,float* programmeR=nullptr) noexcept {
        const auto token=state_.load(std::memory_order_acquire);
        const int phase=phaseOf(token);
        const auto generation=token&~phaseMask;
        // A reset followed by begin may happen without an intervening idle block.
        if(audioGeneration_!=generation){audioGeneration_=generation;gain_=1.f;}
        if(phase==idle){gain_=1.f;return {idle,false,token};}
        if(!rendered||frames<=0)return {phase,false,token};
        const float step=1.f/std::max(1.f,static_cast<float>(rate*0.005));
        if(phase==silent||(phase==fadeIn&&!audible))gain_=0.f;
        for(int i=0;i<frames;++i){
            if(phase==fadeOut)gain_=std::max(0.f,gain_-step);
            else if(phase==fadeIn&&audible)gain_=std::min(1.f,gain_+step);
            if(outputs)for(int c=0;c<channels;++c)if(outputs[c])outputs[c][i]*=gain_;
            if(extraL)extraL[i]*=gain_;
            if(extraR)extraR[i]*=gain_;
            if(programmeL)programmeL[i]*=gain_;
            if(programmeR)programmeR[i]*=gain_;
        }
        return {phase,(phase==fadeOut&&gain_<=0.f)||(phase==fadeIn&&audible&&gain_>=1.f),token};
    }
    void acknowledge(Block b) noexcept {
        if(!b.complete||phaseOf(b.token)!=b.phase)return;
        auto expected=b.token;
        const auto generation=expected&~phaseMask;
        // A delayed receipt must never acknowledge a subsequent transaction.
        if(b.phase==fadeOut)state_.compare_exchange_strong(expected,generation|silent,std::memory_order_acq_rel);
        else if(b.phase==fadeIn)state_.compare_exchange_strong(expected,generation|idle,std::memory_order_acq_rel);
    }
private:
    static constexpr std::uint64_t phaseMask=3;
    static constexpr int phaseOf(std::uint64_t token) noexcept {return static_cast<int>(token&phaseMask);}
    static constexpr std::uint64_t nextGeneration(std::uint64_t token) noexcept {return (token&~phaseMask)+4;}
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"PCM envelope state must be lock-free");
    std::atomic<std::uint64_t> state_{idle};
    std::uint64_t audioGeneration_=0; // AUDIO only
    float gain_=1.f; // AUDIO only
};
} // namespace armix::mixer
