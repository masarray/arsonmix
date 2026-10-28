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
    assert(near(l[0],.5f) && near(r[0],.5f));
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
    // Legacy command without "sends" remains backwards compatible.
    auto legacy=juce::JSON::parse(json.upToFirstOccurrenceOf(",\"sends\"",false,false)+"}");
    assert(NativeDryRouteCommand::decode(legacy,decoded,error));
    assert(decoded.sends.count==0);
    assert(decoded.sends.toMaster[0]==false);
}
