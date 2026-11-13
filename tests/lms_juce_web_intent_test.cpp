// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/NativeLmsWebIntent.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
using namespace armix::lms;
using namespace armix::lms::web;
int main(){
    const auto valid=juce::JSON::parse(R"JSON({
      "schema":"arsonmix.lms.native-design-intent.v1",
      "requestId":"lms-validate-4",
      "deviceEpoch":"21","graphRevision":"42",
      "expectedDesignRevision":"0",
      "sampleRate":48000,"maxFrames":256,"activeOutputChannels":8,
      "design":{"schema":"arsonmix.lms.draft.v1","applied":false,
        "ways":2,"stereo":true,"cutoffsHz":[1400],
        "outputs":[{"outputId":"HF-L","portIdentity":"ASIO/driver/4/OUT 5",
          "sourceSide":0,"section":1,"gainDb":-5.5,"delayMs":0.34,
          "muted":true,"polarityInverted":true,"limiterEnabled":false,
          "peq":[{"frequencyHz":1000,"q":1.5,"gainDb":2.3,"enabled":true}]}]}}
    )JSON");
    OfflineIntent accepted;
    assert(decode(valid,accepted));
    assert(accepted.requestId=="lms-validate-4");
    assert(accepted.proposed.deviceEpoch==21);
    assert(accepted.proposed.graphRevision==42);
    assert(accepted.proposed.activeOutputChannels==8);
    assert(accepted.proposed.outputs.size()==1);
    assert(accepted.proposed.outputs[0].polarityInverted);
    assert(accepted.proposed.outputs[0].mute);
    assert(accepted.proposed.outputs[0].ceilingDbfs==-6.);
    assert(!accepted.proposed.outputs[0].voltageModel.has_value());
    assert(!accepted.proposed.outputs[0].confirmedSpeakerProfile);
    assert(accepted.proposed.outputs[0].peq.size()==1);
    assert(accepted.proposed.outputs[0].peq[0].enabled);
    auto withCeiling=juce::JSON::parse(juce::JSON::toString(valid));
    auto ceilingDesign=withCeiling.getProperty("design",{});
    auto ceilingRows=ceilingDesign.getProperty("outputs",{});
    auto& ceilingRow=ceilingRows.getArray()->getReference(0);
    ceilingRow.getDynamicObject()->setProperty("limiterThresholdDb",-12.5);
    OfflineIntent ceilingIntent;
    assert(decode(withCeiling,ceilingIntent));
    assert(ceilingIntent.proposed.outputs[0].ceilingDbfs==-12.5);
    ceilingRow.getDynamicObject()->setProperty("limiterThresholdDb",-61.);
    assert(!decode(withCeiling,ceilingIntent));
    ceilingRow.getDynamicObject()->setProperty("limiterThresholdDb","-12");
    assert(!decode(withCeiling,ceilingIntent));
    ceilingRow.getDynamicObject()->setProperty("limiterThresholdDb",-12.5);
    ceilingRow.getDynamicObject()->setProperty("voltageModel",true);
    assert(!decode(withCeiling,ceilingIntent));
    auto reject=[&](const char* key,juce::var value){
        auto changed=juce::JSON::parse(juce::JSON::toString(valid));
        changed.getDynamicObject()->setProperty(key,value);
        OfflineIntent other;
        assert(!decode(changed,other));
    };
    reject("deviceEpoch","0");
    reject("deviceEpoch","021");
    reject("deviceEpoch","18446744073709551616");
    reject("graphRevision","4e2");
    reject("expectedDesignRevision","-1");
    reject("requestId","multi line\nbad");
    auto invalid=juce::JSON::parse(juce::JSON::toString(valid));
    invalid.getProperty("design",{}).getDynamicObject()->
        setProperty("applied",true);
    OfflineIntent unchanged;
    assert(!decode(invalid,unchanged));
    assert(accepted.proposed.deviceEpoch==21);
    TrustedSpeakerDeviceView inspected;
    inspected.deviceEpoch=21;inspected.graphRevision=42;
    inspected.mainChannelId=999;inspected.backend="ASIO";
    inspected.deviceIdentity="ASIO/driver";inspected.sampleRate=48000;
    inspected.maxFrames=256;inspected.callbackOutputs=8;
    inspected.mainL=0;inspected.mainR=1;
    inspected.monitorL=2;inspected.monitorR=3;
    inspected.allOutputsDisarmed=true;
    for(int i=0;i<8;++i)
        inspected.actualEnabledPorts.push_back({"ASIO/driver/"+std::to_string(i),i});
    const auto snapshot=inspection(inspected,nullptr);
    assert(snapshot.getProperty("ready",{})==juce::var(true));
    assert(snapshot.getProperty("hardwareApplied",{})==juce::var(false));
    assert(snapshot.getProperty("designRevision","")=="0");
    const auto ports=snapshot.getProperty("ports",{}).getArray();
    assert(ports&&ports->size()==8);
    assert(ports->getReference(0).getProperty("occupied",{})==juce::var(true));
    assert(ports->getReference(4).getProperty("occupied",{})==juce::var(false));
    armix::mixer::DynamicNativeSessionPublisher::SpeakerDraftReadback read;
    read.deviceEpoch=21;read.graphRevision=42;read.designRevision=3;
    read.mainSource=armix::mixer::ChannelId{999};
    read.design=accepted.proposed;
    const auto roundTrip=inspection(inspected,&read);
    const auto verified=roundTrip.getProperty("validatedDesign",{});
    const auto outRows=verified.getProperty("outputs",{}).getArray();
    assert(outRows&&outRows->size()==1);
    const auto& row=outRows->getReference(0);
    assert(row.getProperty("gainDb",{})==juce::var(-5.5));
    assert(row.getProperty("delayMs",{})==juce::var(0.34));
    assert(row.getProperty("polarityInverted",{})==juce::var(true));
    assert(row.getProperty("limiterEnabled",{})==juce::var(false));
    assert(row.getProperty("limiterThresholdDb",{})==juce::var(-6.));
    assert(row.getProperty("voltageModelPresent",{})==juce::var(false));
    const auto peq=row.getProperty("peq",{}).getArray();
    assert(peq&&peq->size()==1);
    const auto& eq=peq->getReference(0);
    assert(eq.getProperty("frequencyHz",{})==juce::var(1000.));
    assert(eq.getProperty("q",{})==juce::var(1.5));
    assert(eq.getProperty("gainDb",{})==juce::var(2.3));
    assert(eq.getProperty("enabled",{})==juce::var(true));
    assert(roundTrip.getProperty("hardwareApplied",{})==juce::var(false));
    std::puts("PASS LMS: strict JUCE message-thread schema, no fake hardware ACK, bounded device readback");
}
