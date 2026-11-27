// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <juce_core/juce_core.h>
#include <memory>
namespace armix::builtin_fx {
// One strict CONTROL-only codec for portable storage and native edit ingress.
struct AntiNoizPortableSettings {
    using Core=anti_noiz::Core;
    static const char* modeName(Core::Mode m) noexcept {
        switch(m){
            case Core::Mode::Voice:return "Voice";
            case Core::Mode::Singing:return "Singing";
            case Core::Mode::Music:return "Music";
            default:return "Auto";
        }
    }
    static bool parseSettings(const juce::var& v,Core::Settings& out) {
        if(!v.isObject())return false;
        const auto count=v.getDynamicObject()->getProperties().size();
        if(count!=5&&count!=18&&count!=21&&count!=24)return false;
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
        if(count>=21&&(!flag("spectralEnabled",s.spectralEnabled)||!number("artifactControl",0,1,s.artifactControl)||!number("vocalProtection",0,1,s.vocalProtection)))return false;
        if(count==24&&(!flag("smartVoice",s.manual.smartVoice)||!number("smartMarginDb",3,18,s.manual.smartMarginDb)||!number("smartMaxLiftDb",0,24,s.manual.smartMaxLiftDb)))return false;
        s.enabled=static_cast<bool>(enabled);if(!Core::validSettings(s))return false;out=s;return true;
    }
    static juce::var settings(Core::Settings s) {
        auto p=std::make_unique<juce::DynamicObject>();
        p->setProperty("mode",modeName(s.mode));p->setProperty("cleanDb",s.cleanDb);
        p->setProperty("silenceDb",s.silenceDb);p->setProperty("musicProtect",s.musicProtect);
        p->setProperty("enabled",s.enabled);
        p->setProperty("spectralEnabled",s.spectralEnabled);p->setProperty("artifactControl",s.artifactControl);p->setProperty("vocalProtection",s.vocalProtection);
        p->setProperty("spectralManual",s.spectralManual);p->setProperty("noiseOffsetDb",s.noiseOffsetDb);
        p->setProperty("smartVoice",s.manual.smartVoice);p->setProperty("smartMarginDb",s.manual.smartMarginDb);p->setProperty("smartMaxLiftDb",s.manual.smartMaxLiftDb);
        p->setProperty("manualExpander",s.manual.enabled);p->setProperty("gate",s.manual.gate);
        p->setProperty("thresholdDb",s.manual.thresholdDb);p->setProperty("ratio",s.manual.ratio);p->setProperty("rangeDb",s.manual.rangeDb);
        p->setProperty("attackMs",s.manual.attackMs);p->setProperty("holdMs",s.manual.holdMs);p->setProperty("releaseMs",s.manual.releaseMs);
        p->setProperty("kneeDb",s.manual.kneeDb);p->setProperty("sidechainHpHz",s.manual.hpHz);p->setProperty("sidechainLpHz",s.manual.lpHz);
        return juce::var(p.release());
    }
};
}
