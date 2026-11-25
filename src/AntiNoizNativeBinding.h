// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/builtin_fx/AntiNoizProcessor.h"
#include "NativeFxControlRequest.h"
#include <cmath>
namespace armix::builtin_fx {
// CONTROL only: strict typed parameters, durable readback and actual observed
// audio telemetry. No graph authority or callback is owned here.
struct AntiNoizNativeBinding final {
    using Core=AntiNoizProcessor::Core;
    using Snapshot=AntiNoizProcessor::Snapshot;
    static const char* modeName(Core::Mode m) noexcept {
        switch(m){
            case Core::Mode::Voice:return "Voice";
            case Core::Mode::Singing:return "Singing";
            case Core::Mode::Music:return "Music";
            default:return "Auto";
        }
    }
    static const char* learnName(AntiNoizProcessor::LearnStatus s) noexcept {
        switch(s){
            case AntiNoizProcessor::LearnStatus::Learning:return "Learning";
            case AntiNoizProcessor::LearnStatus::Accepted:return "Accepted";
            case AntiNoizProcessor::LearnStatus::Rejected:return "Rejected";
            case AntiNoizProcessor::LearnStatus::Cancelled:return "Cancelled";
            default:return "Idle";
        }
    }
    static bool parseSettings(const juce::var& v,Core::Settings& out) {
        if(!v.isObject()||v.getDynamicObject()->getProperties().size()!=5)return false;
        const auto mode=v.getProperty("mode",{}),enabled=v.getProperty("enabled",{});
        if(!mode.isString()||!enabled.isBool())return false;
        Core::Settings s;
        const auto name=mode.toString();
        if(name=="Auto")s.mode=Core::Mode::AutoProtect;
        else if(name=="Voice")s.mode=Core::Mode::Voice;
        else if(name=="Singing")s.mode=Core::Mode::Singing;
        else if(name=="Music")s.mode=Core::Mode::Music;
        else return false;
        auto number=[&](const char* key,float maximum,float& value){
            const auto n=v.getProperty(key,{});
            if(!n.isInt()&&!n.isDouble())return false;
            const double d=static_cast<double>(n);
            if(!std::isfinite(d)||d<0.||d>maximum)return false;
            value=static_cast<float>(d);return true;
        };
        if(!number("cleanDb",24,s.cleanDb)||!number("silenceDb",48,s.silenceDb)||
           !number("musicProtect",1,s.musicProtect))return false;
        s.enabled=static_cast<bool>(enabled);out=s;return true;
    }
    static juce::var settings(Core::Settings s) {
        auto p=std::make_unique<juce::DynamicObject>();
        p->setProperty("mode",modeName(s.mode));p->setProperty("cleanDb",s.cleanDb);
        p->setProperty("silenceDb",s.silenceDb);p->setProperty("musicProtect",s.musicProtect);
        p->setProperty("enabled",s.enabled);
        return juce::var(p.release());
    }
    static juce::var observation(Snapshot s) {
        auto p=std::make_unique<juce::DynamicObject>();
        p->setProperty("serial",juce::String(std::to_string(s.serial)));
        p->setProperty("profile",s.profile);p->setProperty("learning",s.learning);
        p->setProperty("learnStatus",learnName(s.learnStatus));
        p->setProperty("rackBypassed",s.rackBypassed);
        p->setProperty("effectiveMode",modeName(s.effectiveMode));
        p->setProperty("latency",s.latency);
        // Minimum spectral gain is not broadband reduction. Label it
        // explicitly; residual gain is the linked expander gain.
        p->setProperty("spectralGain",s.spectralGain);p->setProperty("residualGain",s.residualGain);
        return juce::var(p.release());
    }
};
}
