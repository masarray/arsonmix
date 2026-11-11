// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

// Extracted from ENGINE #266 with explicit callback admission gate.
// Single JUCE CONTROL-thread adapter: installed PCM/ChannelId readback ->
// approved /preview/ public schema v8. No new renderer, callback or DSP.
namespace armix::console {
inline bool nativeV8Decimal(const juce::var& value) {
    if(!value.isString())return false;
    const auto s=value.toString().toStdString();
    if(s.empty()||s.size()>20||(s.size()>1&&s[0]=='0'))return false;
    std::uint64_t n=0;
    for(char c:s) {
        if(c<'0'||c>'9')return false;
        auto d=static_cast<unsigned>(c-'0');
        if(n>(std::numeric_limits<std::uint64_t>::max()-d)/10)return false;
        n=n*10+d;
    }
    return n!=0;
}
inline bool nativeV8Number(const juce::var& value,double& n) {
    if(!value.isInt()&&!value.isInt64()&&!value.isDouble())return false;
    n=static_cast<double>(value);
    return std::isfinite(n);
}
// A structurally correct bus is NOT proof that its IDs own the installed
// JUCE callback. The real PCM owner must explicitly admit the producer.
// G0 must pass false and keep the accepted GUI graph unmodified.
enum class NativeGraphProjection { installed, retained };

inline bool projectNativeGraph(juce::var& snapshot,const juce::var& bus,
                               NativeGraphProjection mode) {
    const bool installed=mode==NativeGraphProjection::installed;
    const auto device=snapshot.getProperty("device",{});
    if(!nativeV8Decimal(device.getProperty("epoch",{})))return false;
    const auto rawRows=bus.getProperty("channels",{}),rawEdges=bus.getProperty("edges",{});
    const auto* rows=rawRows.getArray();const auto* edges=rawEdges.getArray();
    const auto rev=bus.getProperty("graphRevision",{}),roster=bus.getProperty("rosterRevision",{});
    if(!rows||rows->isEmpty()||!edges||!nativeV8Decimal(rev)||
       !nativeV8Decimal(roster))return false;
    juce::Array<juce::var> channels,wires,destinations;
    std::unordered_set<std::string> ids,edgeIds,fxInstanceTokens;
    auto meters=std::make_unique<juce::DynamicObject>();
    juce::String mainId;
    for(const auto& row:*rows) {
        const auto id=row.getProperty("id",{}),name=row.getProperty("name",{});
        const auto role=row.getProperty("role","").toString();
        const auto format=row.getProperty("format","").toString();
        const auto out=row.getProperty("primaryOutput",{});
        const auto fader=row.getProperty("fader",{}),pan=row.getProperty("pan",{});
        const auto mute=row.getProperty("mute",{}),solo=row.getProperty("solo",{});
        const auto inserts=row.getProperty("inserts",{});
        double linear=0.,p=0.;
        if(!nativeV8Decimal(id)||!ids.insert(id.toString().toStdString()).second ||
           !name.isString()||name.toString().isEmpty()||
           (role!="INPUT"&&role!="GROUP"&&role!="FX_RETURN"&&role!="MAIN"&&role!="MONITOR")||
           (format!="MONO"&&format!="STEREO")||
           !out.isString()||!nativeV8Number(fader,linear)||
           !nativeV8Number(pan,p)||linear<0.||linear>4.||p< -1.||p>1.||
           !mute.isBool()||!solo.isBool()||!inserts.isArray()||
           inserts.getArray()->size()>8)return false;
        if(role=="MAIN"){if(mainId.isNotEmpty())return false;mainId=id.toString();}
        auto ch=std::make_unique<juce::DynamicObject>();
        ch->setProperty("id",id);ch->setProperty("name",name);
        ch->setProperty("role",role);ch->setProperty("processingFormat",format);
        if(out.toString()!="0") {
            if(!nativeV8Decimal(out)||out.toString()==id.toString())return false;
            ch->setProperty("primaryOutId",out);
        }
        const auto source=row.getProperty("source",{});
        if(source.isObject())ch->setProperty("source",source);
        auto controls=std::make_unique<juce::DynamicObject>();
        const double db=linear>0.?20.*std::log10(linear):-60.;
        controls->setProperty("faderDb",juce::jlimit(-60.,12.,db));
        controls->setProperty("pan",p);
        controls->setProperty("mute",mute);controls->setProperty("solo",solo);
        ch->setProperty("controls",juce::var(controls.release()));
        juce::Array<juce::var> fx;
        for(const auto& item:*inserts.getArray()){
            // This integration/ header is the one included by the live native
            // runtime snapshot. The src/ counterpart is NOT its replacement.
            // id is the installed processor token; catalogId is the vendor
            // identity used for the command and subsequent ACK/readback.
            const auto token=item.getProperty("instanceId",{}),label=item.getProperty("name",{});
            const auto catalog=item.getProperty("id",{});
            const auto bypass=item.getProperty("bypassed",{});
            if(!token.isString()||token.toString().isEmpty()||
               !catalog.isString()||catalog.toString().isEmpty()||
               catalog.toString().getNumBytesAsUTF8()>256||
               !label.isString()||!bypass.isBool()||
               !fxInstanceTokens.insert(token.toString().toStdString()).second)
                return false;
            auto insert=std::make_unique<juce::DynamicObject>();
            insert->setProperty("id",token);insert->setProperty("catalogId",catalog);
            insert->setProperty("name",label);
            insert->setProperty("bypassed",bypass);
            const auto latency=item.getProperty("latencySamples",{});
            if(latency.isInt())insert->setProperty("latencySamples",latency);
            const auto changed=item.getProperty("latencyChanged",{});
            if(changed.isBool())insert->setProperty("latencyChanged",changed);
            fx.add(juce::var(insert.release()));
        }
        ch->setProperty("inserts",juce::var(fx));
        const auto rackBypassed=row.getProperty("fxRackBypassed",{});
        if(!rackBypassed.isVoid()) {
            if(!rackBypassed.isBool())return false;
            ch->setProperty("fxRackBypassed",rackBypassed);
        }
        ch->setProperty("sends",juce::var(juce::Array<juce::var>{}));
        { // INPUT channels can receive WIRE PCM as well as physical capture.
            auto dst=std::make_unique<juce::DynamicObject>();
            dst->setProperty("id",id);dst->setProperty("name",name);
            dst->setProperty("role",role);
            destinations.add(juce::var(dst.release()));
        }
        double left=0.,right=0.;
        if(installed && nativeV8Number(row.getProperty("peakL",{}),left) &&
           nativeV8Number(row.getProperty("peakR",{}),right) &&
           left>=0.&&right>=0.){
            auto peak=std::make_unique<juce::DynamicObject>();
            peak->setProperty("peakL",left);peak->setProperty("peakR",right);
            meters->setProperty(juce::Identifier(id.toString()),juce::var(peak.release()));
        }
        channels.add(juce::var(ch.release()));
    }
    if(mainId.isEmpty())return false;
    for(const auto& channel:channels) {
        const auto out=channel.getProperty("primaryOutId",{});
        if(out.isString()&&!ids.count(out.toString().toStdString()))return false;
    }
    for(const auto& edge:*edges) {
        const auto id=edge.getProperty("edgeId",{}),from=edge.getProperty("from",{}),
                   to=edge.getProperty("to",{}),gain=edge.getProperty("gainLinear",{});
        const auto tap=edge.getProperty("tap","").toString();
        const auto primary=edge.getProperty("primaryOutput",{});
        double linear=0.;
        if(!nativeV8Decimal(id)||!nativeV8Decimal(from)||!nativeV8Decimal(to)||
           from.toString()==to.toString()||
           !edgeIds.insert(id.toString().toStdString()).second||
           !ids.count(from.toString().toStdString())||
           !ids.count(to.toString().toStdString())||
           from.toString()==mainId||(tap!="PRE_FADER"&&tap!="POST_FADER")||
           !nativeV8Number(gain,linear)||linear<0.||linear>4.||
           !primary.isBool())return false;
        auto w=std::make_unique<juce::DynamicObject>();
        w->setProperty("edgeId",id);w->setProperty("from",from);w->setProperty("to",to);
        w->setProperty("tap",tap);w->setProperty("gainLinear",linear);
        w->setProperty("primaryOutput",primary);
        w->setProperty("enabled",true);w->setProperty("signalKind","AUDIO");
        wires.add(juce::var(w.release()));
        if(!static_cast<bool>(primary))for(auto& channel:channels) {
            if(channel.getProperty("id","").toString()!=from.toString())continue;
            auto send=std::make_unique<juce::DynamicObject>();
            send->setProperty("id",id);send->setProperty("targetId",to);
            send->setProperty("tap",tap);send->setProperty("gainLinear",linear);
            const auto list=channel.getProperty("sends",{});
            if(auto* a=list.getArray())a->add(juce::var(send.release()));
            break;
        }
    }
    const auto graph=snapshot.getProperty("graph",{});
    const auto telemetry=snapshot.getProperty("telemetry",{});
    auto* g=graph.getDynamicObject();auto* t=telemetry.getDynamicObject();
    if(!g||!t||!graph.getProperty("capabilities",{}).isObject())return false;
    // All validation completed before anything becomes authoritative.
    g->setProperty("revision",rev);g->setProperty("rosterRevision",roster);
    const auto latency=bus.getProperty("latencySamples",{});
    if(latency.isInt())g->setProperty("latencySamples",latency);
    g->setProperty("mainChannelId",mainId);
    g->setProperty("channels",juce::var(channels));
    g->setProperty("destinations",juce::var(destinations));
    g->setProperty("edges",juce::var(wires));
    if(auto* caps=graph.getProperty("capabilities",{}).getDynamicObject()){
        caps->setProperty("callbackOwnsChannelIds",installed);
        juce::Array<juce::var> ops;
        if(installed)ops.add("APPLY_WIRE_TRANSACTION");
        const auto actual=installed?bus.getProperty("supportedOps",{}):juce::var{};
        if(const auto* list=actual.getArray())
            if(list->contains(juce::var("NATIVE_SET_WIRE_GAIN")))ops.add("SET_WIRE_GAIN");
        caps->setProperty("supportedOps",juce::var(ops));
    }
    t->setProperty("channelMeterRmsPeak",juce::var(meters.release()));
    // productionCallbackLive deliberately stays FALSE until full FAT.
    return true;
}
inline bool projectApprovedNativeV8(juce::var& snapshot,const juce::var& bus,
                                    bool installedChannelIdCallbackAdmitted) {
    return installedChannelIdCallbackAdmitted &&
           projectNativeGraph(snapshot,bus,NativeGraphProjection::installed);
}
// Legacy callers supplied a bus from the installed producer, which exposes
// its WIRE operation. The retained producer supplies an empty operation list.
// New callers must pass admission explicitly from the owning engine status.
inline bool projectApprovedNativeV8(juce::var& snapshot,const juce::var& bus) {
    const auto supported=bus.getProperty("supportedOps",{});
    const auto* ops=supported.getArray();
    return projectApprovedNativeV8(snapshot,bus,
        ops && ops->contains(juce::var("NATIVE_APPLY_WIRE_TRANSACTION")));
}
} // namespace armix::console
