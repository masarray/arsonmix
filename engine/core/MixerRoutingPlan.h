// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Phase P1: prepared, device-independent 10-strip + MASTER routing primitive.
// NO connection to the live audio callback yet. All topology changes must be
// compiled on the control thread and published using a quiescent/epoch handoff.
// Only read/accumulate/master belong in the realtime callback.
namespace armix::mixer {

constexpr int stripCount = 10;

enum class SourceMode { disconnected, mono, stereo };

struct StripConfig final {
    SourceMode source = SourceMode::disconnected;
    int inputA = -1, inputB = -1; // physical input indices, never GUI labels
    float faderLinear = 1.0f;     // UI dB conversion happens on control thread
    float pan = 0.0f;             // -1 = left, 0 = center, +1 = right
    bool mute = false, solo = false;
};
struct MasterConfig final {
    float gainLinear = 1.0f;
    float pan = 0.0f;
    bool mute = false, mono = false;
};
struct Configuration final {
    std::array<StripConfig,stripCount> strips {};
    MasterConfig master {};
};
enum class Fault { none, invalidInputCount, inputOutOfRange, duplicateStereoInput,
                   invalidSourceMode, invalidGain, invalidPan };
struct Error final {
    Fault code = Fault::none;
    int strip = -1; // -1 denotes master/device rather than a source strip
};

struct CompiledStrip final {
    int inputA = -1, inputB = -1;
    SourceMode mode = SourceMode::disconnected;
    bool active = false; // muted, disconnected and solo-excluded are never routed
    float leftGain = 0.f, rightGain = 0.f;
};

struct Plan final {
    std::array<CompiledStrip,stripCount> strips {};
    int physicalInputs = 0;
    float masterGain = 1.f, masterLeftBalance = 1.f, masterRightBalance = 1.f;
    bool masterMono = false;
    int activeStrips = 0;

    // Control thread ONLY. Rejection leaves previous plan untouched.
    // Unassigned strips are legal and always silent, never fake VST buses.
    static bool compile(const Configuration& cfg, int inputCount, Plan& destination,
                        Error& error) noexcept {
        error = {};
        if (inputCount < 0 || inputCount > 256) {
            error = {Fault::invalidInputCount,-1};
            return false;
        }
        if (!validGain(cfg.master.gainLinear)) {
            error = {Fault::invalidGain,-1}; return false;
        }
        if (!validPan(cfg.master.pan)) {
            error = {Fault::invalidPan,-1}; return false;
        }
        bool anySolo = false;
        for (int i=0;i<stripCount;++i) {
            const auto& c=cfg.strips[i];
            if (c.source!=SourceMode::disconnected &&
                c.source!=SourceMode::mono && c.source!=SourceMode::stereo) {
                error={Fault::invalidSourceMode,i};return false;
            }
            if (!validGain(c.faderLinear)) { error={Fault::invalidGain,i};return false; }
            if (!validPan(c.pan)) { error={Fault::invalidPan,i};return false; }
            if (c.source == SourceMode::disconnected) continue;
            if (c.inputA<0 || c.inputA>=inputCount ||
                (c.source==SourceMode::stereo && (c.inputB<0 || c.inputB>=inputCount))) {
                error={Fault::inputOutOfRange,i};return false;
            }
            if (c.source==SourceMode::stereo && c.inputA==c.inputB) {
                error={Fault::duplicateStereoInput,i};return false;
            }
            if (c.solo && !c.mute) anySolo = true;
        }
        Plan candidate;
        candidate.physicalInputs = inputCount;
        candidate.masterGain = cfg.master.mute?0.f:cfg.master.gainLinear;
        candidate.masterMono = cfg.master.mono;
        candidate.masterLeftBalance = cfg.master.pan>0.f?1.f-cfg.master.pan:1.f;
        candidate.masterRightBalance = cfg.master.pan<0.f?1.f+cfg.master.pan:1.f;
        for (int i=0;i<stripCount;++i) {
            const auto& c=cfg.strips[i];
            if (c.source==SourceMode::disconnected) continue;
            auto& p=candidate.strips[i];
            p.mode=c.source;
            p.inputA=c.inputA;
            p.inputB=c.source==SourceMode::stereo?c.inputB:c.inputA;
            p.active=!c.mute && (!anySolo || c.solo) && c.faderLinear>0.f;
            if (!p.active) continue;
            ++candidate.activeStrips;
            if (c.source==SourceMode::mono) {
                // Equal-power mono panning, computed exactly once per topology edit.
                p.leftGain=c.faderLinear*std::sqrt(0.5f*(1.f-c.pan));
                p.rightGain=c.faderLinear*std::sqrt(0.5f*(1.f+c.pan));
            } else {
                // Stereo BALANCE, not an accidental crossfeed or stereo collapse.
                p.leftGain=c.faderLinear*(c.pan>0.f?1.f-c.pan:1.f);
                p.rightGain=c.faderLinear*(c.pan<0.f?1.f+c.pan:1.f);
            }
        }
        destination=candidate;
        return true;
    }

    // RT: read one physical input source into preallocated stereo FX scratch.
    // Stereo processors can consume both channels before accumulate().
    bool extract(int strip,const float* const* inputs,int availableInputs,
                 float* left,float* right,int frames) const noexcept {
        if (!left || !right || frames<=0 || strip<0 || strip>=stripCount) return false;
        const auto& route=strips[strip];
        const bool valid=route.active && inputs &&
            route.inputA>=0 && route.inputA<availableInputs &&
            route.inputB>=0 && route.inputB<availableInputs &&
            inputs[route.inputA] && inputs[route.inputB];
        if (!valid) {
            std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
            return false;
        }
        std::copy_n(inputs[route.inputA],frames,left);
        std::copy_n(inputs[route.inputB],frames,right);
        return true;
    }

    // RT: mix one channel's POST-FX scratch onto the master accumulator.
    void accumulate(int strip,const float* left,const float* right,
                    float* masterL,float* masterR,int frames) const noexcept {
        if (strip<0 || strip>=stripCount || frames<=0 ||
            !left || !right || !masterL || !masterR) return;
        const auto& p=strips[strip];
        if (!p.active) return;
        for(int i=0;i<frames;++i) {
            masterL[i]+=left[i]*p.leftGain;
            masterR[i]+=right[i]*p.rightGain;
        }
    }
    static void clearMaster(float* left,float* right,int frames) noexcept {
        if(frames<=0||!left||!right)return;
        std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
    }
    // RT: apply master controls AFTER the planned MASTER insert chain.
    void finishMaster(float* left,float* right,int frames) const noexcept {
        if(frames<=0||!left||!right)return;
        for(int i=0;i<frames;++i) {
            const float l=left[i]*masterLeftBalance;
            const float r=right[i]*masterRightBalance;
            if(masterMono) left[i]=right[i]=0.5f*(l+r)*masterGain;
            else {left[i]=l*masterGain;right[i]=r*masterGain;}
        }
    }
private:
    static bool validGain(float v) noexcept { return std::isfinite(v)&&v>=0.f&&v<=4.f; }
    static bool validPan(float v) noexcept { return std::isfinite(v)&&v>=-1.f&&v<=1.f; }
};
} // namespace armix::mixer
