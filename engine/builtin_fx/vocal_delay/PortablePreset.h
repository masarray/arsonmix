// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
#include <juce_core/juce_core.h>
namespace armix::builtin_fx::vocal_delay {
// Portable sound, not transport/performance/graph state. Never persist HIT,
// internal power, rack bypass, source ChannelId or raw delay history.
struct PortablePreset final {
    static juce::var encode(const Parameters& p){
        auto o=std::make_unique<juce::DynamicObject>();
        o->setProperty("timeMs",double(p.timeMs));o->setProperty("feedback",double(p.feedback));
        o->setProperty("echo",double(p.echo));o->setProperty("highPassHz",double(p.highPassHz));
        o->setProperty("lowPassHz",double(p.lowPassHz));o->setProperty("duck",double(p.duck));
        o->setProperty("duckAttackMs",double(p.duckAttackMs));o->setProperty("duckReleaseMs",double(p.duckReleaseMs));
        o->setProperty("mode",int(p.mode));o->setProperty("style",int(p.style));
        o->setProperty("adaptiveFocus",p.adaptiveFocus);o->setProperty("spillover",p.spillover);
        if(p.smartStage)o->setProperty("smartStage",true);
        if(p.throwMode!=ThrowMode::off)o->setProperty("throwMode",int(p.throwMode));
        o->setProperty("bpmQuarter",int(p.bpmQuarter));o->setProperty("division",int(p.division));
        return juce::var(o.release());
    }
    static bool decode(const juce::var& v,Parameters& out){
        if(!v.isObject())return false;
        const auto count=v.getDynamicObject()->getProperties().size();
        if(count<14||count>16)return false;
        auto p=Parameters{};
        const auto number=[&](const char* k,float& target){
            const auto x=v.getProperty(k,{});if(!x.isInt()&&!x.isDouble())return false;
            const auto d=double(x);if(!std::isfinite(d)||std::abs(d)>20000.)return false;
            target=float(d);return true;};
        const auto integer=[&](const char* k,int max,int& target){
            const auto x=v.getProperty(k,{});if(!x.isInt())return false;
            target=int(x);return target>=0&&target<=max;};
        const auto boolean=[&](const char* k,bool& target){
            const auto x=v.getProperty(k,{});if(!x.isBool())return false;target=bool(x);return true;};
        int mode=0,style=0,bpm=0,division=0;
        if(!number("timeMs",p.timeMs)||!number("feedback",p.feedback)||!number("echo",p.echo)||
           !number("highPassHz",p.highPassHz)||!number("lowPassHz",p.lowPassHz)||!number("duck",p.duck)||
           !number("duckAttackMs",p.duckAttackMs)||!number("duckReleaseMs",p.duckReleaseMs)||
           !integer("mode",2,mode)||!integer("style",4,style)||!integer("bpmQuarter",960,bpm)||
           !integer("division",6,division)||!boolean("adaptiveFocus",p.adaptiveFocus)||
           !boolean("spillover",p.spillover))return false;
        if(count>=15&&(!boolean("smartStage",p.smartStage)||!p.smartStage))return false;
        if(count==16){int modeValue=0;if(!integer("throwMode",8,modeValue)||modeValue==0)return false;p.throwMode=static_cast<ThrowMode>(modeValue);}
        p.mode=static_cast<StereoMode>(mode);p.style=static_cast<EchoStyle>(style);
        p.bpmQuarter=static_cast<std::uint16_t>(bpm);p.division=static_cast<std::uint8_t>(division);
        if(!valid(p))return false;
        p.enabled=out.enabled;p.hit=false;out=p;return true;
    }
};
} // namespace
