// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeShowSchema.h"
#include "../engine/core/NativeDryRouteCommand.h"
#include <juce_core/juce_core.h>
#include <cmath>

// P4 stage 3: message-thread native SSOT -> SHOW snapshot. This is deliberately
// a DRY (zero vendor FX) capture: vendor getStateInformation() requires a
// separately qualified processor-quiescence and thread-affinity protocol.
// No UI draft or browser localStorage can become the saved audio truth.
namespace armix::shows {
class NativeLiveShowCapture final {
    static bool numeric(const juce::var& v,double& n,double lo,double hi) {
        if(!(v.isInt()||v.isInt64()||v.isDouble()))return false;
        n=static_cast<double>(v);
        return std::isfinite(n)&&n>=lo&&n<=hi;
    }
    static bool flag(const juce::var& v,bool& b) {
        if(!v.isBool())return false;
        b=static_cast<bool>(v);
        return true;
    }
    static bool integer(const juce::var& v,juce::int64& n) {
        if(!(v.isInt()||v.isInt64()))return false;
        n=static_cast<juce::int64>(v);
        return true;
    }
public:
    // Return empty String on success, otherwise reason. On any failure,
    // preserve the caller's prior accepted snapshot in its entirety.
    static juce::String captureDry(const juce::var& nativeStatus,
                                   const juce::String& cleanName,
                                   SnapshotV2& out) {
        if(!detail::name(cleanName))return "Invalid Show snapshot name";
        if(!nativeStatus.isObject())return "Native audio status unavailable";
        bool outputArmed=true,independent=false;
        if(!flag(nativeStatus.getProperty("monitor",{}),outputArmed) ||
           !flag(nativeStatus.getProperty("adaptiveClockActive",{}),independent) ||
           outputArmed || independent)
            return "Mute native output and use a single DIRECT device before SAVE";
        if(nativeStatus.getProperty("diagnosticRoute",{}).toString()!="NORMAL")
            return "Return diagnostic audio to NORMAL before SAVE";
        // R0 CH1 borrowed processors live outside Routing LAB fxCount.
        // Reject any vendor FX (and absent/invalid count) for DRY-only saves.
        const auto borrowed=nativeStatus.getProperty("chainCount",{});
        if(!borrowed.isInt() || static_cast<int>(borrowed)!=0)
            return "CH1 FX Rack has VST3 state: cannot SAVE as a DRY-only Show";
        const auto lab=nativeStatus.getProperty("dryRoutingLab",{});
        if(!lab.isObject())return "Native Routing LAB has no published session";
        bool active=false,ready=false;
        if(!flag(lab.getProperty("active",{}),active) ||
           !flag(lab.getProperty("ready",{}),ready) || !active || !ready)
            return "Routing LAB is not active and acknowledged";
        juce::int64 epoch=0,rev=0;
        if(!integer(lab.getProperty("epoch",{}),epoch) ||
           !integer(lab.getProperty("revision",{}),rev) ||
           epoch<=0 || rev<=0)
            return "Native route device epoch/revision missing";
        const auto fxCount=lab.getProperty("fxCount",{});
        if(!fxCount.isInt()||static_cast<int>(fxCount)!=0)
            return "Live VST3 requires safe vendor state quiescence; DRY-only SAVE";
        const auto route=lab.getProperty("acceptedRoute",{});
        juce::int64 routeEpoch=0,routeRev=0;
        if(!route.isObject() ||
           !integer(route.getProperty("deviceEpoch",{}),routeEpoch) ||
           !integer(route.getProperty("revision",{}),routeRev) ||
           routeEpoch!=epoch || routeRev+1!=rev)
            return "Native route changed after acceptance; refresh before SAVE";

        armix::mixer::NativeDryRouteCommand parsed;
        juce::String error;
        if(!armix::mixer::NativeDryRouteCommand::decode(route,parsed,error))
            return "Accepted native routing record invalid: "+error;
        const auto rows=route.getProperty("channels",{});
        const auto* channels=rows.getArray();
        const auto controlsValue=lab.getProperty("channelControls",{});
        const auto* controls=controlsValue.getArray();
        const auto busesValue=route.getProperty("auxes",{});
        const auto* buses=busesValue.getArray();
        if(!channels||channels->size()!=armix::mixer::stripCount ||
           !controls||controls->size()!=armix::mixer::stripCount ||
           !buses||buses->size()!=armix::mixer::auxBusCount)
            return "Native controls or four AUX definitions are incomplete";
        // Confirm every physical port still appears in the native enabled
        // mask; never accept arbitrary WebView port numbers as restored IO.
        const auto portsValue=lab.getProperty("ports",{});
        const auto* ports=portsValue.getArray();
        if(!ports)return "Current hardware input mask unavailable";
        auto portEnabled=[ports](int physical) {
            for(const auto& port:*ports) {
                const auto raw=port.getProperty("port",{});
                if(raw.isInt()&&static_cast<int>(raw)==physical)return true;
            }
            return false;
        };
        SnapshotV2 next;
        next.id=juce::Uuid().toDashedString().toLowerCase();
        next.name=cleanName;
        next.sourceBuild="P4_NATIVE_DRY_ACK_10CH_4AUX";
        next.createdMs=next.modifiedMs=juce::Time::currentTimeMillis();
        for(int i=0;i<channels->size();++i) {
            const auto& row=channels->getReference(i);
            const auto& ctrl=controls->getReference(i);
            if(!row.isObject()||!ctrl.isObject() ||
               row.getProperty("fxId",{}).toString().isNotEmpty())
                return "Uncaptured VST3 or incomplete channel state";
            auto& strip=next.strips[static_cast<size_t>(i)];
            const auto& parsedStrip=parsed.channels.stripsAndMaster.strips[i];
            const auto mode=row.getProperty("mode",{}).toString();
            strip.busReturn=(mode=="BUS");
            strip.source=mode=="MONO"?1:mode=="STEREO"?2:0;
            const auto actualFormat=parsed.channels.formats[i];
            strip.channelFormat=actualFormat==armix::mixer::ChannelFormat::mono?1:
                actualFormat==armix::mixer::ChannelFormat::stereo?2:
                (strip.source==2 || strip.busReturn?2:1); // legacy v1 fallback
            strip.inputA=strip.source!=0?parsedStrip.inputA:-1;
            strip.inputB=strip.source==2?parsedStrip.inputB:-1;
            if(strip.source!=0 &&
               (!portEnabled(strip.inputA) ||
                (strip.source==2&&!portEnabled(strip.inputB))))
                return "Enabled hardware port changed during Show capture";
            double gain=0,pan=0;
            if(!numeric(ctrl.getProperty("linear",{}),gain,0,4) ||
               !numeric(ctrl.getProperty("pan",{}),pan,-100,100) ||
               !flag(ctrl.getProperty("mute",{}),strip.mute) ||
               !flag(ctrl.getProperty("solo",{}),strip.solo))
                return "Acknowledged native channel control is invalid";
            strip.fader=gain;
            strip.pan=pan/100.0;
            strip.toMaster=parsed.sends.toMaster[i];
        }
        for(int i=0;i<armix::mixer::auxBusCount;++i) {
            const auto& row=buses->getReference(i);
            if(!row.isObject()||row.getProperty("fxId",{}).toString().isNotEmpty())
                return "Uncaptured AUX VST3 cannot be saved as DRY";
            auto& bus=next.aux[static_cast<size_t>(i)];
            const auto& input=parsed.aux.buses[i];
            bus.enabled=input.enabled;
            bus.toMaster=input.toMaster;
            bus.mute=input.mute;
            bus.gain=input.gain;
        }
        for(int i=0;i<parsed.sends.count;++i) {
            const auto& e=parsed.sends.sends[i];
            if(!e.enabled)continue;
            next.strips[static_cast<size_t>(e.source)].sends.push_back({
                e.destination,e.tap==armix::mixer::SendTap::preFader,e.gain,e.pan
            });
        }
        for(int i=0;i<parsed.aux.count;++i) {
            const auto& e=parsed.aux.sends[i];
            if(!e.enabled)continue;
            next.auxSends.push_back({
                e.source,e.destination,e.tap==armix::mixer::SendTap::preFader,
                e.gain,e.pan
            });
        }
        double masterGain=0;
        if(!numeric(nativeStatus.getProperty("masterLinear",{}),masterGain,0,4) ||
           !flag(nativeStatus.getProperty("masterMute",{}),next.masterMute) ||
           !flag(nativeStatus.getProperty("masterMono",{}),next.masterMono))
            return "Native master controls are incomplete";
        next.masterGain=masterGain;
        SnapshotV2 validated;
        if(!Codec::decode(Codec::encode(next),validated))
            return "Native Show schema rejected the captured routing";
        out=std::move(validated);
        return {};
    }
};
} // namespace armix::shows
