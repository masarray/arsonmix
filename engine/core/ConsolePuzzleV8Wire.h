// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// One native wire serializer shared by JUCE control-thread producer and
// deterministic S0 fixture generator. NO DSP or callback ownership here.
// Physical ports and PCM measurements MUST come from actual native status.
namespace armix::console {
struct PuzzleV8Pin final {
    int port=-1,callback=-1;
    std::string label;
    bool hasRawPeak=false;
    double rawPeak=0.0;
};
struct PuzzleV8Snapshot final {
    std::string epoch="0",backend,captureDevice,playbackDevice;
    std::string graphRevision="0",rosterRevision="0",legacyRoutingRevision="0";
    int sampleRate=0,bufferFrames=0;
    bool outputArmed=false,rawCapturePresent=false;
    bool mainPcmPresent=false,monitorPcmPresent=false;
    double rawCapturePeak=0.0;
    bool legacyActive=false,legacyRouteAccepted=false;
    std::vector<PuzzleV8Pin> inputs,outputs;
    // Installed M4 callback does NOT own a ChannelId graph yet.
    bool productionCallbackLive=false;
};
struct PuzzleV8Ack final {
    std::string requestId,status="REJECTED",deviceEpoch="0";
    std::string graphRevision="0",rosterRevision="0";
    std::string errorCode="UNIVERSAL_CALLBACK_NOT_READY";
};
class PuzzleV8Wire final {
    static std::string quote(const std::string& in) {
        std::string out="\"";
        static constexpr char hex[]="0123456789abcdef";
        for(const unsigned char c:in) {
            switch(c) {
                case '"':out+="\\\"";break;
                case '\\':out+="\\\\";break;
                case '\n':out+="\\n";break;
                case '\r':out+="\\r";break;
                case '\t':out+="\\t";break;
                default:
                    if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
                    else out+=static_cast<char>(c);
            }
        }
        return out+'"';
    }
    static const char* bit(bool b) noexcept{return b?"true":"false";}
    static std::string number(double n) {
        if(!std::isfinite(n)||n<0.)return "null";
        char buffer[80]{};
        std::snprintf(buffer,sizeof(buffer),"%.9g",n);
        return buffer;
    }
    static std::string pins(const std::vector<PuzzleV8Pin>& list) {
        std::string s="[";
        for(const auto& p:list) {
            if(p.port<0||p.callback<0)continue;
            if(s.size()>1)s+=',';
            s+="{\"port\":"+std::to_string(p.port)+",\"callback\":"+
               std::to_string(p.callback)+",\"label\":"+quote(p.label);
            if(p.hasRawPeak && std::isfinite(p.rawPeak) && p.rawPeak>=0.) {
                s+=",\"rawPeak\":"+number(p.rawPeak)+
                   ",\"rawPresent\":"+bit(p.rawPeak>0.0005);
            }
            s+='}';
        }
        return s+"]";
    }
    static std::string enabled(const std::vector<PuzzleV8Pin>& list) {
        std::string s="[";
        for(const auto& p:list){
            if(p.port<0||p.callback<0)continue;
            if(s.size()>1)s+=',';
            s+=std::to_string(p.port);
        }
        return s+"]";
    }
public:
    static std::string snapshot(const PuzzleV8Snapshot& s) {
        // Complete v8 mandatory wire even while callback is not universal.
        // ChannelId and revisions are decimal strings; no JSON double.
        return "{\"schemaVersion\":6,\"productionCallbackLive\":"+
            std::string(bit(s.productionCallbackLive))+
            ",\"device\":{\"epoch\":"+quote(s.epoch)+
            ",\"backend\":"+quote(s.backend)+
            ",\"captureDevice\":"+quote(s.captureDevice)+
            ",\"playbackDevice\":"+quote(s.playbackDevice)+
            ",\"inputEndpoints\":"+pins(s.inputs)+
            ",\"outputEndpoints\":"+pins(s.outputs)+
            ",\"enabledInputs\":"+enabled(s.inputs)+
            ",\"enabledOutputs\":"+enabled(s.outputs)+
            ",\"sampleRate\":"+(s.sampleRate>0?std::to_string(s.sampleRate):"null")+
            ",\"bufferFrames\":"+(s.bufferFrames>0?std::to_string(s.bufferFrames):"null")+
            ",\"outputArmed\":"+bit(s.outputArmed)+
            "},\"graph\":{\"revision\":"+quote(s.graphRevision)+
            ",\"rosterRevision\":"+quote(s.rosterRevision)+
            ",\"channels\":[],\"destinations\":[],\"capabilities\":{"
            "\"callbackOwnsChannelIds\":false,\"fullVendorFxMigration\":false,"
            "\"showAndOutputParity\":false,\"physicalCaptureMapped\":false,"
            "\"dynamicChannels\":false,\"groups\":false,\"fxReturns\":false,"
            "\"monitor\":false,\"sendsOnFaders\":false,\"supportedOps\":[]}},"
            "\"telemetry\":{\"rawCapturePresent\":"+bit(s.rawCapturePresent)+
            ",\"rawCapturePeak\":"+number(s.rawCapturePeak)+
            ",\"mainPcmPresent\":"+bit(s.mainPcmPresent)+
            ",\"monitorPcmPresent\":"+bit(s.monitorPcmPresent)+
            ",\"channelMeterRmsPeak\":[]},\"legacyM4\":{\"active\":"+
            bit(s.legacyActive)+",\"routeAccepted\":"+bit(s.legacyRouteAccepted)+
            ",\"routingRevision\":"+quote(s.legacyRoutingRevision)+"}}";
    }
    static std::string ack(const PuzzleV8Ack& a) {
        return "{\"requestId\":"+quote(a.requestId)+
            ",\"status\":"+quote(a.status)+
            ",\"deviceEpoch\":"+quote(a.deviceEpoch)+
            ",\"graphRevision\":"+quote(a.graphRevision)+
            ",\"rosterRevision\":"+quote(a.rosterRevision)+
            ",\"errorCode\":"+quote(a.errorCode)+"}";
    }
    static std::string fixture(const PuzzleV8Snapshot& s,const PuzzleV8Ack& a) {
        return "{\"producer\":\"native-cpp-v8-s0-unready\",\"snapshot\":"+
               snapshot(s)+",\"ack\":"+ack(a)+"}\n";
    }
};
} // namespace armix::console
