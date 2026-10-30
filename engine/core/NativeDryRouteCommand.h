// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelSendPlan.h"
#include "AuxBusPlan.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <cstdint>

// P1G control thread: decode the entire Routing LAB transaction exactly once.
// GUI selections are untrusted drafts. Audio RT never parses JSON or allocates.
namespace armix::mixer {
struct NativeDryRouteCommand final {
    ChannelRoutingConfig channels {};
    SendRequest sends {};
    AuxBusRequest aux {};

    static bool decode(const juce::var& request,
                       NativeDryRouteCommand& destination,
                       juce::String& problem) {
        problem.clear();
        if(!request.isObject()) {problem="Invalid routing command";return false;}
        const auto rawRows=request.getProperty("channels",juce::var());
        auto* rows=rawRows.getArray();
        if(!rows || rows->size()!=stripCount) {
            problem="Exactly 10 channel assignments are required";return false;
        }
        // R5/MS1 wire version 2 separates physical source from processing format.
        // Legacy v1 (missing schemaVersion) remains readable for existing Shows.
        const auto rawVersion=request.getProperty("schemaVersion",juce::var());
        const int version=rawVersion.isVoid()?1:rawVersion.isInt()
            ? static_cast<int>(rawVersion):-1;
        if(version!=1 && version!=2) {
            problem="Unsupported routing schemaVersion (expected 1 or 2)";return false;
        }
        NativeDryRouteCommand candidate;
        std::array<juce::String,stripCount> modeNames {};
        for(int i=0;i<stripCount;++i) {
            const auto& row=rows->getReference(i);
            if(!row.isObject()){problem="Invalid channel assignment";return false;}
            const auto mode=row.getProperty("mode",{}).toString();
            modeNames[i]=mode;
            // Legacy source mode remains the decoder's cross-version codec;
            // V2 requires independent typed fields and rejects contradictions.
            const auto sourceKind=row.getProperty("sourceKind",{}).toString();
            const auto sourceFormat=row.getProperty("sourceFormat",{}).toString();
            const auto channelFormat=row.getProperty("channelFormat",{}).toString();
            if(version==2){
                const juce::String expectedKind=mode=="BUS"?"BUS":
                    mode=="NONE"?"NONE":"PHYSICAL";
                const juce::String expectedSourceFormat=
                    (mode=="MONO"||mode=="STEREO")?mode:"NONE";
                if(sourceKind!=expectedKind || sourceFormat!=expectedSourceFormat ||
                   (channelFormat!="MONO" && channelFormat!="STEREO")){
                    problem="CH "+juce::String(i+1)+
                        ": sourceKind/sourceFormat/channelFormat mismatch";
                    return false;
                }
            } else if(row.hasProperty("channelFormat") ||
                      row.hasProperty("sourceKind") || row.hasProperty("sourceFormat")){
                problem="Typed channel fields require schemaVersion 2";return false;
            }
            const auto left=row.getProperty("left",-1);
            const auto right=row.getProperty("right",-1);
            const auto gain=row.getProperty("gain",.25);
            const auto pan=row.getProperty("pan",0.0);
            const auto muted=row.getProperty("mute",false);
            const auto soloed=row.getProperty("solo",false);
            const auto main=row.getProperty("main",true);
            if(!left.isInt()||!right.isInt()||
               !(gain.isDouble()||gain.isInt())||
               !(pan.isDouble()||pan.isInt())||
               !muted.isBool()||!soloed.isBool()||!main.isBool()) {
                problem="Invalid channel port, level, pan or switch type";return false;
            }
            const float level=static_cast<float>(static_cast<double>(gain));
            const float balance=static_cast<float>(static_cast<double>(pan));
            if(!std::isfinite(level)||level<0.f||level>4.f||
               !std::isfinite(balance)||balance< -1.f||balance>1.f) {
                problem="Channel linear gain must be 0..4 and pan -1..1";return false;
            }
            auto& c=candidate.channels.stripsAndMaster.strips[i];
            c.faderLinear=level;c.pan=balance;
            c.mute=bool(muted);c.solo=bool(soloed);
            candidate.sends.toMaster[i]=bool(main);
            if(mode=="NONE" || mode=="BUS") {
                c.source=SourceMode::disconnected;
                candidate.channels.formats[i]=mode=="BUS"
                    ? ChannelFormat::stereo:ChannelFormat::followSource;
            } else if(mode=="MONO"){
                c.source=SourceMode::mono;c.inputA=static_cast<int>(left);
                candidate.channels.formats[i]=ChannelFormat::mono;
            } else if(mode=="STEREO"){
                c.source=SourceMode::stereo;
                c.inputA=static_cast<int>(left);c.inputB=static_cast<int>(right);
                candidate.channels.formats[i]=ChannelFormat::stereo;
            } else {
                problem="Channel must be NONE, MONO, STEREO or BUS";return false;
            }
            if(version==2)
                candidate.channels.formats[i]=channelFormat=="MONO"
                    ? ChannelFormat::mono:ChannelFormat::stereo;
        }
        // Dedicated AUX 1..4 are independent from the ten physical INPUTs.
        // Old sessions without auxes remain backward compatible and silent.
        const auto rawAux=request.getProperty("auxes",juce::var());
        if(!rawAux.isVoid()){
            auto* auxRows=rawAux.getArray();
            if(!auxRows||auxRows->size()!=auxBusCount){
                problem="Exactly four dedicated AUX definitions are required";
                return false;
            }
            for(int b=0;b<auxBusCount;++b){
                const auto& row=auxRows->getReference(b);
                if(!row.isObject()){problem="Invalid AUX return";return false;}
                const auto enabled=row.getProperty("enabled",false);
                const auto master=row.getProperty("main",true);
                const auto mute=row.getProperty("mute",false);
                const auto level=row.getProperty("gain",1.0);
                if(!enabled.isBool()||!master.isBool()||!mute.isBool()||
                   !(level.isDouble()||level.isInt())){
                    problem="Invalid AUX enable, main, mute or level";return false;
                }
                const float gain=static_cast<float>(static_cast<double>(level));
                if(!std::isfinite(gain)||gain<0.f||gain>4.f){
                    problem="AUX gain must be 0..4";return false;
                }
                candidate.aux.buses[b]={bool(enabled),bool(master),bool(mute),gain};
            }
        }
        const auto rawSends=request.getProperty("sends",juce::var());
        if(!rawSends.isVoid() && !rawSends.isArray()){
            problem="Sends must be an array";return false;
        }
        auto* sendArray=rawSends.getArray();
        if(sendArray) {
            if(sendArray->size()>maxChannelSends){
                problem="Maximum 32 internal sends";return false;
            }
            for(int i=0;i<sendArray->size();++i) {
                const auto& raw=sendArray->getReference(i);
                if(!raw.isObject()){problem="Invalid send row";return false;}
                const auto source=raw.getProperty("from",-1);
                const auto dest=raw.getProperty("to",-1);
                const auto tap=raw.getProperty("tap",{}).toString();
                const auto gain=raw.getProperty("gain",.25);
                const auto pan=raw.getProperty("pan",0.0);
                if(!source.isInt()||!dest.isInt()||
                   !(gain.isInt()||gain.isDouble())||
                   !(pan.isInt()||pan.isDouble())) {
                    problem="Invalid send source, destination or gain";return false;
                }
                if(tap!="PRE" && tap!="POST"){
                    problem="Send tap must be PRE or POST";return false;
                }
                const int from=static_cast<int>(source),to=static_cast<int>(dest);
                if(from<0||from>=stripCount||to<0||to>=stripCount+auxBusCount){
                    problem="Send requires CH1..10 source and CH/AUX destination";return false;
                }
                if(modeNames[from]=="NONE"||
                   (to<stripCount&&modeNames[to]=="NONE")||
                   (to>=stripCount&&!candidate.aux.buses[to-stripCount].enabled)){
                    problem="Enable a sender and set the recipient to BUS or an assigned channel";return false;
                }
                const float sendGain=static_cast<float>(static_cast<double>(gain));
                const float sendPan=static_cast<float>(static_cast<double>(pan));
                if(!std::isfinite(sendGain)||sendGain<0.f||sendGain>1.f||
                   !std::isfinite(sendPan)||sendPan< -1.f||sendPan>1.f){
                    problem="Send level 0..1, pan -1..1";return false;
                }
                if(to>=stripCount){
                    auto& s=candidate.aux.sends[candidate.aux.count++];
                    s.source=from;s.destination=to-stripCount;
                    s.tap=tap=="PRE"?SendTap::preFader:SendTap::postFader;
                    s.gain=sendGain;s.pan=sendPan;s.enabled=true;
                }else{
                    auto& s=candidate.sends.sends[candidate.sends.count++];
                    s.source=from;s.destination=to;
                    s.tap=tap=="PRE"?SendTap::preFader:SendTap::postFader;
                    s.gain=sendGain;s.pan=sendPan;s.enabled=true;
                }
            }
        }
        // Rejection never mutates the destination. Explicit DAG validation
        // below occurs via PreparedRenderGraph::compile during apply().
        destination=candidate;
        return true;
    }
};
} // namespace armix::mixer
