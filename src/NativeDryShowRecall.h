// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeShowSchema.h"
#include "../engine/core/NativeDryRouteCommand.h"
#include <juce_core/juce_core.h>
#include <cmath>

// P4 stage 4: PREPARE ONLY. Turns a verified persisted DRY Show into
// an explicit new native routing transaction against the *current* live
// epoch and hardware mask. NEVER starts a device, unmutes, or applies a
// route. The caller must use LiveAudioEngine::applyDryRoutingLab to publish.
namespace armix::shows {
class NativeDryShowRecall final {
    static juce::var object() {return juce::var(new juce::DynamicObject());}
    static void put(juce::var& obj,const char* key,juce::var value) {
        obj.getDynamicObject()->setProperty(juce::Identifier(key),std::move(value));
    }
    static bool epoch(const juce::var& value,juce::int64& number) {
        if(!(value.isInt()||value.isInt64()))return false;
        number=static_cast<juce::int64>(value);
        return number>0;
    }
public:
    static juce::String prepare(const SnapshotV2& saved,
                                const juce::var& status,
                                juce::var& command,
                                bool allowVerifiedFx=false) {
        SnapshotV2 canonical;
        if(!Codec::decode(Codec::encode(saved),canonical))
            return "Saved Show failed native schema validation";
        if(!status.isObject() ||
           !status.getProperty("monitor",{}).isBool() ||
           static_cast<bool>(status.getProperty("monitor",{})) ||
           !status.getProperty("adaptiveClockActive",{}).isBool() ||
           static_cast<bool>(status.getProperty("adaptiveClockActive",{})) ||
           status.getProperty("diagnosticRoute",{}).toString()!="NORMAL")
            return "MUTE OUTPUT and choose a verified DIRECT device before Show recall";
        // Another native CH1 rack cannot be silently overwritten by DRY recall.
        // The route FX counter excludes the borrowed CH1 vendor graph.
        const auto borrowed=status.getProperty("chainCount",{});
        if(!borrowed.isInt() || static_cast<int>(borrowed)!=0)
            return "Remove CH1 vendor FX before restoring a DRY-only Show";
        const auto lab=status.getProperty("dryRoutingLab",{});
        if(!lab.isObject() ||
           !lab.getProperty("ready",{}).isBool() ||
           !static_cast<bool>(lab.getProperty("ready",{})) ||
           !lab.getProperty("active",{}).isBool() ||
           !static_cast<bool>(lab.getProperty("active",{})))
            return "Select a verified DIRECT native 10CH device before recall";
        const auto fx=lab.getProperty("fxCount",{});
        if(!fx.isInt() || (!allowVerifiedFx && static_cast<int>(fx)!=0))
            return "Stop native VST3 inserts before restoring a DRY Show";
        juce::int64 currentEpoch=0,currentRevision=0;
        if(!epoch(lab.getProperty("epoch",{}),currentEpoch) ||
           !epoch(lab.getProperty("revision",{}),currentRevision))
            return "Current native device epoch and revision are missing";
        const auto portsValue=lab.getProperty("ports",{});
        const auto* ports=portsValue.getArray();
        if(!ports || ports->isEmpty())return "Verified hardware input mask unavailable";
        auto available=[ports](int target) {
            if(target<0)return false;
            for(const auto& p:*ports) {
                const auto n=p.getProperty("port",{});
                if(n.isInt()&&static_cast<int>(n)==target)return true;
            }
            return false;
        };
        if(canonical.masterPan!=0.0 || canonical.masterGain>2.0 ||
           !std::isfinite(canonical.masterGain))
            return "Saved MASTER pan/gain is unsupported by this native mixer";
        juce::Array<juce::var> channels,auxes,sends;
        for(int i=0;i<armix::mixer::stripCount;++i) {
            const auto& ch=canonical.strips[static_cast<size_t>(i)];
            if(!ch.fx.empty()&&!allowVerifiedFx)
                return "Vendor VST3 state recall is not yet supported";
            const juce::String mode=ch.source==1?"MONO":ch.source==2?
                "STEREO":ch.busReturn?"BUS":"NONE";
            if(ch.source>0 &&
               (!available(ch.inputA) || (ch.source==2&&!available(ch.inputB))))
                return "Saved physical input port is not enabled on current device; remap manually";
            auto v=object();
            put(v,"mode",mode);
            // Build native routing command V2: a mono physical microphone
            // may restore a STEREO processing channel without inventing R.
            put(v,"sourceKind",mode=="BUS"?"BUS":mode=="NONE"?"NONE":"PHYSICAL");
            put(v,"sourceFormat",mode=="MONO"||mode=="STEREO"?mode:"NONE");
            put(v,"channelFormat",ch.channelFormat==2?"STEREO":"MONO");
            put(v,"left",ch.inputA);
            put(v,"right",ch.inputB);
            put(v,"gain",ch.fader);
            put(v,"pan",ch.pan);
            put(v,"mute",ch.mute);
            put(v,"solo",ch.solo);
            put(v,"main",ch.toMaster);
            put(v,"fxId",ch.fx.empty()?juce::String():ch.fx.front().catalogId);
            if(allowVerifiedFx) {
                juce::Array<juce::var> ids;
                for(const auto& ref:ch.fx)ids.add(ref.catalogId);
                put(v,"fxIds",juce::var(ids));
            }
            channels.add(v);
            for(const auto& s:ch.sends) {
                auto row=object();
                put(row,"from",i);
                put(row,"to",s.destination);
                put(row,"tap",s.preFader?"PRE":"POST");
                put(row,"gain",s.gain);
                put(row,"pan",s.pan);
                sends.add(row);
            }
        }
        for(int b=0;b<armix::mixer::auxBusCount;++b) {
            const auto& bus=canonical.aux[static_cast<size_t>(b)];
            if(!bus.fx.empty()&&!allowVerifiedFx)
                return "Vendor AUX VST3 state recall is not supported";
            auto row=object();
            put(row,"enabled",bus.enabled);
            put(row,"main",bus.toMaster);
            put(row,"mute",bus.mute);
            put(row,"gain",bus.gain);
            put(row,"fxId",bus.fx.empty()?juce::String():bus.fx.front().catalogId);
            if(allowVerifiedFx) {
                juce::Array<juce::var> ids;
                for(const auto& ref:bus.fx)ids.add(ref.catalogId);
                put(row,"fxIds",juce::var(ids));
            }
            auxes.add(row);
        }
        for(const auto& s:canonical.auxSends) {
            auto row=object();
            put(row,"from",s.source);
            put(row,"to",armix::mixer::stripCount+s.destination);
            put(row,"tap",s.preFader?"PRE":"POST");
            put(row,"gain",s.gain);
            put(row,"pan",s.pan);
            sends.add(row);
        }
        auto proposed=object();
        put(proposed,"schemaVersion",2);
        put(proposed,"deviceEpoch",currentEpoch);
        put(proposed,"revision",currentRevision);
        // MS3A integration: Show FX/routing recall is NOT a device patch
        // migration. Retain the current verified physical MAIN destination
        // pair, never silently reset to hardware outputs 1/2. Physical pin
        // assignments are deliberately NOT serialized into portable Shows.
        const auto liveMain=lab.getProperty("mainOutputs",juce::var());
        if(!liveMain.isVoid()) {
            const auto* pair=liveMain.getArray();
            const auto* valid=lab.getProperty("outputPorts",juce::var()).getArray();
            if(!pair||pair->size()!=2||!valid)
                return "Current physical MAIN output patch is unavailable";
            const auto left=pair->getReference(0),right=pair->getReference(1);
            if(!left.isInt()||!right.isInt() ||
               static_cast<int>(left)==static_cast<int>(right))
                return "Current physical MAIN output pair is invalid";
            bool foundLeft=false,foundRight=false;
            for(const auto& pin:*valid) {
                if(pin.isInt()) {
                    foundLeft|=static_cast<int>(pin)==static_cast<int>(left);
                    foundRight|=static_cast<int>(pin)==static_cast<int>(right);
                }
            }
            if(!foundLeft||!foundRight)
                return "Current MAIN output pin is not enabled on this device";
            put(proposed,"mainOutputs",liveMain);
        }
        put(proposed,"channels",juce::var(channels));
        put(proposed,"auxes",juce::var(auxes));
        put(proposed,"sends",juce::var(sends));
        armix::mixer::NativeDryRouteCommand decoded;
        juce::String error;
        if(!armix::mixer::NativeDryRouteCommand::decode(proposed,decoded,error))
            return "Saved Show cannot be applied to this routing engine: "+error;
        // No partial mutation of caller's command on any failure.
        command=std::move(proposed);
        return {};
    }
};
} // namespace armix::shows
