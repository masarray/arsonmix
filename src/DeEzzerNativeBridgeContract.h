// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/builtin_fx/de_ezzer/DeEzzerInsert.h"
#include <juce_core/juce_core.h>
#include <memory>
namespace armix::builtin_fx::de_ezzer::bridge {
// CONTROL thread only. Never convert identity/sequence/frame counters to JS doubles.
inline bool decimal(const juce::var& raw,std::uint64_t& out) {
    if(!raw.isString())return false;
    const auto s=raw.toString();
    if(s.isEmpty()||s.length()>20||(s.length()>1&&s[0]=='0'))return false;
    std::uint64_t v=0;
    for(auto c:s){
        if(c<'0'||c>'9')return false;
        const auto d=static_cast<std::uint64_t>(c-'0');
        if(v>(std::numeric_limits<std::uint64_t>::max()-d)/10)return false;
        v=v*10+d;
    }
    out=v;return true;
}
inline juce::String decimal(std::uint64_t v) {return juce::String(std::to_string(v));}
inline bool decode(const juce::var& raw,Command& out) {
    if(!raw.isObject())return false;
    Command c;
    const auto id=raw.getProperty("id",{}),s=raw.getProperty("settings",{});
    if(!id.isObject()||!s.isObject()||!decimal(id.getProperty("instance",{}),c.id.instance)||
       !decimal(id.getProperty("epoch",{}),c.id.epoch)||!decimal(raw.getProperty("sequence",{}),c.sequence)||
       !c.id.instance||!c.id.epoch||!c.sequence||raw.getProperty("audition",{})!=juce::var("Normal"))return false;
    auto number=[&](const char* k,float& v,double hi){
        const auto x=s.getProperty(k,{});
        if(!x.isDouble()&&!x.isInt())return false;
        const double d=static_cast<double>(x);
        if(!std::isfinite(d)||d<0||d>hi)return false;
        v=static_cast<float>(d);return true;
    };
    const auto enabled=s.getProperty("enabled",{}),mode=s.getProperty("mode",{}),shape=s.getProperty("shape",{});
    if(!number("amount",c.settings.amount,1)||!number("sensitivity",c.settings.sensitivity,1)||
       !number("rangeDb",c.settings.rangeDb,18)||!enabled.isBool()||!mode.isString()||!shape.isString())return false;
    c.settings.enabled=static_cast<bool>(enabled);
    if(mode==juce::var("Auto"))c.settings.mode=Mode::Auto;
    else if(mode==juce::var("Voice"))c.settings.mode=Mode::Voice;else return false;
    if(shape==juce::var("Shelf"))c.settings.shape=Shape::Shelf;
    else if(shape==juce::var("Focused"))c.settings.shape=Shape::Focused;else return false;
    if(!StateCodec::valid(c.settings))return false;
    out=c;return true;
}
inline juce::var encode(Identity id) {
    auto o=std::make_unique<juce::DynamicObject>();
    o->setProperty("instance",decimal(id.instance));o->setProperty("epoch",decimal(id.epoch));
    return juce::var(o.release());
}
inline juce::var encode(Settings s) {
    auto o=std::make_unique<juce::DynamicObject>();
    o->setProperty("amount",s.amount);o->setProperty("sensitivity",s.sensitivity);
    o->setProperty("rangeDb",s.rangeDb);o->setProperty("enabled",s.enabled);
    o->setProperty("mode",s.mode==Mode::Auto?"Auto":"Voice");
    o->setProperty("shape",s.shape==Shape::Shelf?"Shelf":"Focused");
    return juce::var(o.release());
}
inline juce::var encode(const Snapshot& s) {
    auto o=std::make_unique<juce::DynamicObject>();
    o->setProperty("id",encode(s.id));o->setProperty("appliedSequence",decimal(s.appliedSequence));
    o->setProperty("frames",decimal(s.frames));o->setProperty("settings",encode(s.settings));
    o->setProperty("audition","Normal");o->setProperty("gainReductionDb",s.gainReductionDb);
    o->setProperty("lowerWeight",s.lowerWeight);o->setProperty("droppedTelemetry",static_cast<juce::int64>(s.droppedTelemetry));
    return juce::var(o.release());
}
inline juce::var encode(const Ack& a) {
    auto o=std::make_unique<juce::DynamicObject>();
    o->setProperty("requested",encode(a.requested));o->setProperty("sequence",decimal(a.sequence));
    const char* code="Invalid";
    switch(a.code){case AckCode::Applied:code="Applied";break;case AckCode::WrongIdentity:code="WrongIdentity";break;
        case AckCode::StaleSequence:code="StaleSequence";break;case AckCode::Invalid:break;}
    o->setProperty("code",code);o->setProperty("actual",encode(a.actual));
    return juce::var(o.release());
}
} // namespace armix::builtin_fx::de_ezzer::bridge
