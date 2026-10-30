// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/NativeDryRouteCommand.h"
#include "../engine/core/PreparedMixerBlock.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cmath>
using namespace armix::mixer;
static bool near(float a,float b){return std::abs(a-b)<0.00001f;}
int main(){
    juce::String json=R"({"channels":[)";
    for(int i=0;i<stripCount;++i) {
        if(i)json<<",";
        if(i==0)json<<R"({"mode":"MONO","left":2,"right":-1,"gain":0,"pan":0,"main":false,"solo":false,"mute":false})";
        else if(i==1)json<<R"({"mode":"BUS","left":-1,"right":-1,"gain":1,"pan":0,"main":true,"solo":false,"mute":false})";
        else if(i==2)json<<R"({"mode":"BUS","left":-1,"right":-1,"gain":1,"pan":0,"main":true,"solo":false,"mute":false})";
        else json<<R"({"mode":"NONE","left":-1,"right":-1,"gain":0.25,"main":true})";
    }
    json<<R"(],"sends":[{"from":0,"to":1,"tap":"PRE","gain":0.5},{"from":0,"to":2,"tap":"POST","gain":0.5}]})";
    auto request=juce::JSON::parse(json);
    assert(request.isObject());
    NativeDryRouteCommand decoded;juce::String error;
    assert(NativeDryRouteCommand::decode(request,decoded,error));
    assert(decoded.sends.count==2);
    assert(!decoded.sends.toMaster[0]);
    assert(decoded.channels.formats[1]==ChannelFormat::stereo);
    assert(decoded.channels.stripsAndMaster.strips[0].faderLinear==0.f);
    constexpr std::array ports{2,5,9};
    PreparedRenderGraph graph;RenderError fault;
    assert(PreparedRenderGraph::compile(decoded.channels,ports,decoded.sends,graph,fault));
    PreparedMixerBlock block(graph,64);
    const std::array<float,2> physical{1.f,.5f};
    const std::array<float,2> empty{0.f,0.f};
    const float* inputs[]={physical.data(),empty.data(),empty.data()};
    std::array<float,2> l{},r{};
    const auto noFx=[](int,float*,float*,int) noexcept {};
    assert(block.process(inputs,3,l.data(),r.data(),2,noFx));
    // PRE is independent from source fader=0, POST is silent.
    assert(near(l[0],.5f*std::sqrt(.5f)) && near(r[0],.5f*std::sqrt(.5f)));
    assert(graph.sends.receivesInternalAudio(1));
    assert(graph.sends.receivesInternalAudio(2));

    // A cycle is rejected by the actual graph compiler, not the GUI.
    auto cyclic=decoded;
    cyclic.sends.sends[cyclic.sends.count++]={1,0,SendTap::postFader,.5f,0.f,true};
    const auto previous=graph;
    assert(!PreparedRenderGraph::compile(cyclic.channels,ports,cyclic.sends,graph,fault));
    assert(fault.fault==RenderFault::invalidSendRouting);
    assert(fault.sends.fault==SendFault::cycleDetected);
    assert(graph.sends.edgeCount==previous.sends.edgeCount);

    // Native decoder does not partially mutate committed configuration.
    const auto good=decoded;
    auto invalid=juce::JSON::parse(json.replace("PRE","INVALID_TAP"));
    assert(!NativeDryRouteCommand::decode(invalid,decoded,error));
    assert(decoded.sends.count==good.sends.count);
    assert(decoded.sends.sends[0].tap==SendTap::preFader);
    json=json.replace("INVALID_TAP","PRE");
    invalid=juce::JSON::parse(json.replace("\"to\":1","\"to\":9"));
    assert(!NativeDryRouteCommand::decode(invalid,decoded,error)); // CH10 NONE
    json=json.replace("\"to\":9","\"to\":1");
    invalid=juce::JSON::parse(json.replace("\"gain\":0.5","\"gain\":999"));
    assert(!NativeDryRouteCommand::decode(invalid,decoded,error));
    assert(decoded.sends.count==2);
    // P1K native JSON: AUX1 is an additional stereo return (index 10),
    // not one of the 10 physical/BUS channel rows. Resolve by native index.
    auto dedicated=juce::JSON::parse(json);
    juce::Array<juce::var> auxRows;
    for(int b=0;b<auxBusCount;++b){
        auto bus=std::make_unique<juce::DynamicObject>();
        bus->setProperty("enabled",b==0);
        bus->setProperty("main",true);
        bus->setProperty("mute",false);
        bus->setProperty("gain",1.0);
        auxRows.add(juce::var(bus.release()));
    }
    dedicated.getDynamicObject()->setProperty("auxes",juce::var(auxRows));
    const auto rawSends=dedicated.getProperty("sends",juce::var());
    assert(rawSends.isArray());
    rawSends.getArray()->getReference(0).getDynamicObject()->setProperty("to",10);
    assert(NativeDryRouteCommand::decode(dedicated,decoded,error));
    assert(decoded.aux.count==1&&decoded.sends.count==1);
    assert(decoded.aux.sends[0].destination==0);
    assert(decoded.aux.sends[0].tap==SendTap::preFader);
    assert(PreparedRenderGraph::compile(
        decoded.channels,ports,decoded.sends,graph,fault,decoded.aux));
    PreparedMixerBlock dedicatedBlock(graph,64);
    assert(dedicatedBlock.process(inputs,3,l.data(),r.data(),2,noFx));
    assert(near(l[0],.5f*std::sqrt(.5f))&&near(r[0],.5f*std::sqrt(.5f))); // mono AUX PRE at -3dB
    // Invalid destination or disabled AUX must fail with unchanged result.
    auxRows.getReference(0).getDynamicObject()->setProperty("enabled",false);
    assert(!NativeDryRouteCommand::decode(dedicated,decoded,error));
    assert(decoded.aux.count==1);
    auxRows.getReference(0).getDynamicObject()->setProperty("enabled",true);
    rawSends.getArray()->getReference(0).getDynamicObject()->setProperty("to",14);
    assert(!NativeDryRouteCommand::decode(dedicated,decoded,error));
    assert(decoded.aux.count==1);
    // Older native commands still decode; AUX remains off by default.
    // Legacy command without "sends" remains backwards compatible.
    auto legacy=juce::JSON::parse(json.upToFirstOccurrenceOf(",\"sends\"",false,false)+"}");
    assert(NativeDryRouteCommand::decode(legacy,decoded,error));
    assert(decoded.sends.count==0);
    assert(decoded.sends.toMaster[0]==false);

    // MS1 versioned live native decode: source physical format and channel
    // processing format are distinct. The SAME existing prepared graph
    // executes this PCM, including a mono-source-to-stereo channel and a
    // mono BUS receiver (with no fabricated capture pin).
    auto typed=juce::JSON::parse(json);
    typed.getDynamicObject()->setProperty("schemaVersion",2);
    for(int i=0;i<stripCount;++i){
        auto& row=typed.getProperty("channels",juce::var())
                           .getArray()->getReference(i);
        const auto mode=row.getProperty("mode",{}).toString();
        row.getDynamicObject()->setProperty("sourceKind",mode=="BUS"?"BUS":
            mode=="NONE"?"NONE":"PHYSICAL");
        row.getDynamicObject()->setProperty("sourceFormat",
            (mode=="MONO"||mode=="STEREO")?mode:"NONE");
        row.getDynamicObject()->setProperty("channelFormat",
            i==0?"STEREO":i==1?"MONO":"STEREO");
    }
    NativeDryRouteCommand v2;
    assert(NativeDryRouteCommand::decode(typed,v2,error));
    assert(v2.channels.stripsAndMaster.strips[0].source==SourceMode::mono);
    assert(v2.channels.formats[0]==ChannelFormat::stereo);
    assert(v2.channels.stripsAndMaster.strips[1].source==SourceMode::disconnected);
    assert(v2.channels.formats[1]==ChannelFormat::mono);
    assert(v2.channels.formats[2]==ChannelFormat::stereo);
    assert(PreparedRenderGraph::compile(v2.channels,ports,v2.sends,graph,fault));
    assert(graph.channels.effectiveFormats[0]==ChannelFormat::stereo);
    assert(graph.channels.effectiveFormats[1]==ChannelFormat::mono);
    assert(graph.scheduled[1] && graph.sends.receivesInternalAudio(1));
    PreparedMixerBlock formatBlock(graph,64);
    assert(formatBlock.process(inputs,3,l.data(),r.data(),2,noFx));
    // Source physical[0]=1: PRE 0.5 into mono receiver; mono pan center
    // should be sqrt(1/2), not un-normalized stereo BALANCE.
    assert(near(l[0],0.5f*0.70710678118f));
    assert(near(r[0],0.5f*0.70710678118f));

    // Fail-closed: one type mismatch, old decoded plan unchanged.
    const auto before=v2;
    auto* ch0=typed.getProperty("channels",juce::var())
                    .getArray()->getReference(0).getDynamicObject();
    ch0->setProperty("sourceKind","BUS");
    assert(!NativeDryRouteCommand::decode(typed,v2,error));
    assert(v2.channels.formats[0]==before.channels.formats[0]);
    ch0->setProperty("sourceKind","PHYSICAL");
    ch0->setProperty("channelFormat","WIDE");
    assert(!NativeDryRouteCommand::decode(typed,v2,error));
    ch0->setProperty("channelFormat","STEREO");
    ch0->removeProperty("channelFormat");
    assert(!NativeDryRouteCommand::decode(typed,v2,error));
    ch0->setProperty("channelFormat","STEREO");
    ch0->setProperty("sourceFormat","STEREO"); // physical mono source
    assert(!NativeDryRouteCommand::decode(typed,v2,error));
    assert(v2.channels.formats[0]==ChannelFormat::stereo);
    // v1 rejects accidental typed partial fields so older payloads cannot
    // silently believe a channel format was applied.
    typed.getDynamicObject()->setProperty("schemaVersion",1);
    assert(!NativeDryRouteCommand::decode(typed,v2,error));
    assert(v2.channels.formats[1]==ChannelFormat::mono);
}
