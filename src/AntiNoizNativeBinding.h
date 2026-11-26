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
        if(!v.isObject())return false;
        const auto count=v.getDynamicObject()->getProperties().size();
        if(count!=5&&count!=18&&count!=21)return false;
        const auto mode=v.getProperty("mode",{}),enabled=v.getProperty("enabled",{});
        if(!mode.isString()||!enabled.isBool())return false;
        Core::Settings s;
        const auto name=mode.toString();
        if(name=="Auto")s.mode=Core::Mode::AutoProtect;
        else if(name=="Voice")s.mode=Core::Mode::Voice;
        else if(name=="Singing")s.mode=Core::Mode::Singing;
        else if(name=="Music")s.mode=Core::Mode::Music;
        else return false;
        auto number=[&](const char* key,float minimum,float maximum,float& value){
            const auto n=v.getProperty(key,{});
            if(!n.isInt()&&!n.isDouble())return false;
            const double d=static_cast<double>(n);
            if(!std::isfinite(d)||d<minimum||d>maximum)return false;
            value=static_cast<float>(d);return true;
        };
        if(!number("cleanDb",0,count==5?24.f:40.f,s.cleanDb)||!number("silenceDb",0,48,s.silenceDb)||
           !number("musicProtect",0,1,s.musicProtect))return false;
        auto flag=[&](const char* key,bool& value){const auto x=v.getProperty(key,{});if(!x.isBool())return false;value=static_cast<bool>(x);return true;};
        if(count>=18&&(!flag("spectralManual",s.spectralManual)||!flag("manualExpander",s.manual.enabled)||!flag("gate",s.manual.gate)||
           !number("noiseOffsetDb",-20,20,s.noiseOffsetDb)||!number("thresholdDb",-80,0,s.manual.thresholdDb)||
           !number("ratio",1,20,s.manual.ratio)||!number("rangeDb",0,96,s.manual.rangeDb)||
           !number("attackMs",.5f,20,s.manual.attackMs)||!number("holdMs",0,500,s.manual.holdMs)||!number("releaseMs",10,1000,s.manual.releaseMs)||
           !number("kneeDb",0,18,s.manual.kneeDb)||!number("sidechainHpHz",20,500,s.manual.hpHz)||!number("sidechainLpHz",1000,20000,s.manual.lpHz)))return false;
        if(count==21&&(!flag("spectralEnabled",s.spectralEnabled)||!number("artifactControl",0,1,s.artifactControl)||!number("vocalProtection",0,1,s.vocalProtection)))return false;
        s.enabled=static_cast<bool>(enabled);if(!Core::validSettings(s))return false;out=s;return true;
    }
    static juce::var settings(Core::Settings s) {
        auto p=std::make_unique<juce::DynamicObject>();
        p->setProperty("mode",modeName(s.mode));p->setProperty("cleanDb",s.cleanDb);
        p->setProperty("silenceDb",s.silenceDb);p->setProperty("musicProtect",s.musicProtect);
        p->setProperty("enabled",s.enabled);
        p->setProperty("spectralEnabled",s.spectralEnabled);p->setProperty("artifactControl",s.artifactControl);p->setProperty("vocalProtection",s.vocalProtection);
        p->setProperty("spectralManual",s.spectralManual);p->setProperty("noiseOffsetDb",s.noiseOffsetDb);
        p->setProperty("manualExpander",s.manual.enabled);p->setProperty("gate",s.manual.gate);
        p->setProperty("thresholdDb",s.manual.thresholdDb);p->setProperty("ratio",s.manual.ratio);p->setProperty("rangeDb",s.manual.rangeDb);
        p->setProperty("attackMs",s.manual.attackMs);p->setProperty("holdMs",s.manual.holdMs);p->setProperty("releaseMs",s.manual.releaseMs);
        p->setProperty("kneeDb",s.manual.kneeDb);p->setProperty("sidechainHpHz",s.manual.hpHz);p->setProperty("sidechainLpHz",s.manual.lpHz);
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
        p->setProperty("spectralGain",s.spectralGain);p->setProperty("residualGain",s.residualGain);p->setProperty("detectorLevel",s.detectorLevel);p->setProperty("audition",s.audition);
        // CONTROL thread: logarithms and JSON allocation never reach audio.
        const auto& spec=s.spectrum;
        if(spec.bins==129||spec.bins==257){
            auto graph=std::make_unique<juce::DynamicObject>();
            graph->setProperty("binHz",spec.binHz);graph->setProperty("bins",spec.bins);
            graph->setProperty("candidate",spec.candidate);graph->setProperty("progress",spec.progress);
            auto curve=[&](const auto& data,bool valid){
                juce::Array<juce::var> points;
                if(valid){points.ensureStorageAllocated(spec.bins);
                    for(int k=0;k<spec.bins;++k){
                        const float db=std::isfinite(data[k])?10.f*std::log10(std::max(1.e-12f,data[k])):-120.f;
                        points.add(juce::var(double(std::clamp(db,-120.f,48.f))));
                    }
                }
                return juce::var(points);
            };
            graph->setProperty("input",curve(spec.input,s.serial!=0&&spec.inputValid&&!s.rackBypassed));
            graph->setProperty("noise",curve(spec.noise,s.serial!=0&&spec.noiseValid));
            p->setProperty("spectrum",juce::var(graph.release()));
        }
        return juce::var(p.release());
    }
};
}
