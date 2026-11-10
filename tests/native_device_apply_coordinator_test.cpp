// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/NativeDeviceApplyCoordinator.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using Coordinator=armix::device::NativeDeviceApplyCoordinator;
using Setup=juce::AudioDeviceManager::AudioDeviceSetup;

static juce::var config(const juce::String& json) {
    auto v=juce::JSON::parse(json);
    assert(v.isObject());
    return v;
}

int main() {
    // Nested JUCE driver modal loops must reject another concurrent APPLY
    // and release exactly once, including moved lease ownership.
    Coordinator owner;
    assert(!owner.busy());
    {
        auto outer=owner.tryBegin();
        assert(outer && owner.busy());
        assert(!owner.tryBegin());
        auto moved=std::move(outer);
        assert(!outer && moved);
        assert(!owner.tryBegin());
    }
    assert(!owner.busy());
    auto reacquired=owner.tryBegin();
    assert(reacquired && owner.busy());

    const auto valid=config(R"({"backend":"ASIO","captureDevice":"ASIO Test",
        "playbackDevice":"ASIO Test","sampleRate":48000,"bufferSize":256,
        "assignSelectedPlaybackPinsToMain":true,
        "confirmRetainedPinMapping":false})");
    const auto parsed=Coordinator::parse(valid);
    assert(parsed);
    const auto& request=parsed.value;
    assert(request.backend=="ASIO" && request.rate==48000. &&
           request.block==256. && request.assignMain);
    assert(Coordinator::validateNames(request,{"ASIO Test"},{"ASIO Test"},false).isEmpty());
    assert(Coordinator::validateNames(request,{"ASIO Test"},{"Other"},false).isNotEmpty());
    assert(Coordinator::validateNames(request,{"Different"},{"ASIO Test"},false).isNotEmpty());
    auto outputOnly=config(R"({"backend":"Windows Audio",
        "captureDevice":"","playbackDevice":"Speakers",
        "sampleRate":0,"bufferSize":0})");
    auto output=Coordinator::parse(outputOnly);
    assert(output && output.value.rate==0. && output.value.block==0.);
    assert(Coordinator::validateNames(output.value,{},{"Speakers"},true).isEmpty());
    assert(Coordinator::validateNames(output.value,{},{"Speakers"},false).isNotEmpty());
    for(const auto& invalid : {
        "{}", R"({"backend":"ASIO","playbackDevice":"X","sampleRate":"48000","bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":4000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":1.5})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":8193})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":256,"assignSelectedPlaybackPinsToMain":"yes"})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":256,"confirmRetainedPinMapping":1})"
    }) {
        assert(!Coordinator::parse(juce::JSON::parse(juce::String(invalid))));
    }

    auto input=juce::JSON::parse("[0,2]");
    auto outputs=juce::JSON::parse("[1,3]");
    juce::BigInteger inMask,outMask;
    assert(Coordinator::parsePinMasks(input,outputs,4,4,false,inMask,outMask));
    assert(inMask[0] && inMask[2] && outMask[1] && outMask[3]);
    assert(!Coordinator::parsePinMasks(input,outputs,4,4,true,inMask,outMask));
    assert(Coordinator::parsePinMasks(juce::JSON::parse("[]"),outputs,4,4,true,inMask,outMask));
    assert(inMask.isZero());
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[0]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[3,3]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[1,-1]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[0,5]"),4,4,false,inMask,outMask));
    juce::Array<juce::var> huge;
    huge.add(juce::var(static_cast<juce::int64>(1ull<<33)));
    huge.add(juce::var(1));
    assert(!Coordinator::parsePinMasks(input,juce::var(huge),4,4,false,inMask,outMask));
    // An invalid request never partially overwrites the previous masks.
    assert(inMask.isZero() && outMask[1] && outMask[3]);

    Setup candidate;
    candidate.inputDeviceName="ASIO Test";
    candidate.outputDeviceName="ASIO Test";
    candidate.sampleRate=48000;
    candidate.bufferSize=256;
    candidate.useDefaultInputChannels=false;
    candidate.useDefaultOutputChannels=false;
    candidate.inputChannels.setBit(0);
    candidate.outputChannels.setBit(0);
    candidate.outputChannels.setBit(1);
    Coordinator::AppliedReadback observed;
    observed.backend="ASIO";
    observed.actual=candidate;
    observed.open=true;
    observed.activeInputs=candidate.inputChannels;
    observed.activeOutputs=candidate.outputChannels;
    observed.deviceRate=48000.;
    observed.deviceBuffer=256;
    assert(Coordinator::matchesApplied(request,candidate,observed));
    observed.actual.outputChannels.clearBit(1);
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.actual=candidate;
    observed.deviceBuffer=128;
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceBuffer=256;
    observed.backend="Windows Audio";
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.backend="ASIO";
    observed.open=false;
    assert(!Coordinator::matchesApplied(request,candidate,observed));

    Coordinator::RollbackReadback rollback;
    rollback.open=true;
    rollback.backend="ASIO";
    rollback.actual=candidate;
    rollback.inputNames={"Mic 1","Mic 2"};
    rollback.outputNames={"Monitor L","Monitor R"};
    assert(Coordinator::matchesRollback(candidate,"ASIO",rollback.inputNames,
                                        rollback.outputNames,rollback));
    rollback.outputNames={"Monitor R","Monitor L"};
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        {"Mic 1","Mic 2"},{"Monitor L","Monitor R"},rollback));
    rollback.outputNames={"Monitor L","Monitor R"};
    rollback.open=false;
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    std::cout<<"Native device Apply preflight/rollback: PASS\n";
}
