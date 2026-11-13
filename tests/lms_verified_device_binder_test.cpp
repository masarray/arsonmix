// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/lms/NativeSpeakerDeviceBinder.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using namespace armix::lms;
static TrustedSpeakerDeviceView device(){
    TrustedSpeakerDeviceView d;
    d.deviceEpoch=13;d.graphRevision=7;d.mainChannelId=99;
    d.backend="ASIO";d.deviceIdentity="ASIO:driver-GUID-OR-PIN-PROVENANCE";
    d.sampleRate=48000.;d.callbackOutputs=8;
    d.mainL=0;d.mainR=1;d.monitorL=2;d.monitorR=3;
    d.acceptedPcm=d.exactDeviceMask=d.allOutputsDisarmed=true;
    for(int i=0;i<8;++i)
        d.actualEnabledPorts.push_back({"ASIO:pin-physical-"+std::to_string(i*2),i});
    return d;
}
static SpeakerSystemRequest proposal(const TrustedSpeakerDeviceView& d){
    SpeakerSystemRequest p;
    p.deviceEpoch=d.deviceEpoch;p.graphRevision=d.graphRevision;
    p.sampleRate=d.sampleRate;p.activeOutputChannels=d.callbackOutputs;
    p.maxFrames=256;p.ways=2;p.stereo=true;p.crossoverHz={1400.};
    // Browser forges a DIFFERENT callback ordinal for the genuine identity:
    p.verifiedPorts={{d.actualEnabledPorts[4].identity,0}};
    OutputPatch a;
    a.outputId="LOW L";a.portIdentity=d.actualEnabledPorts[4].identity;
    a.sourceSide=0;a.section=0;a.mute=true;
    p.outputs.push_back(a);
    return p;
}
int main(){
    auto d=device();auto p=proposal(d);
    SpeakerBoundDraft out;
    const auto accepted=NativeSpeakerDeviceBinder::bind(p,d,99,out);
    assert(accepted.ok());
    assert(out.verifiedInventory.enabledOutputPorts.size()==8);
    assert(out.verifiedRequest.verifiedPorts.size()==8);
    assert(out.verifiedRequest.verifiedPorts[4].callbackOrdinal==4);
    assert(p.verifiedPorts[0].callbackOrdinal==0);
    assert(out.verifiedInventory.mainL==0&&!out.verifiedInventory.mayWritePhysicalAudio);
    const auto original=out.verifiedRequest.outputs[0].portIdentity;
    auto reject=[&](SpeakerBindFault wanted){
        const auto res=NativeSpeakerDeviceBinder::bind(p,d,99,out);
        assert(!res.ok()&&res.fault==wanted);
        assert(out.verifiedRequest.outputs[0].portIdentity==original);
        assert(out.verifiedInventory.enabledOutputPorts[4].callbackOrdinal==4);
    };
    d.exactDeviceMask=false;reject(SpeakerBindFault::unverifiedDevice);d.exactDeviceMask=true;
    d.allOutputsDisarmed=false;reject(SpeakerBindFault::unverifiedDevice);d.allOutputsDisarmed=true;
    d.backend="Windows Audio";reject(SpeakerBindFault::unverifiedDevice);d.backend="ASIO";
    p.deviceEpoch=12;reject(SpeakerBindFault::staleDevice);p.deviceEpoch=13;
    p.graphRevision=6;reject(SpeakerBindFault::staleGraph);p.graphRevision=7;
    d.mainChannelId=98;reject(SpeakerBindFault::invalidSource);d.mainChannelId=99;
    p.sampleRate=44100.;reject(SpeakerBindFault::changedFormat);p.sampleRate=48000.;
    p.outputs[0].mute=false;p.outputs[0].limiterEnabled=true;
    p.outputs[0].confirmedSpeakerProfile=true;
    reject(SpeakerBindFault::invalidLoudspeakerDraft);
    p.outputs[0].mute=true;
    p.outputs[0].portIdentity="DEMO-04";
    reject(SpeakerBindFault::invalidPhysicalPin);
    p.outputs[0].portIdentity=d.actualEnabledPorts[0].identity;
    reject(SpeakerBindFault::invalidPhysicalPin); // MAIN collision
    p.outputs[0].portIdentity=d.actualEnabledPorts[4].identity;
    d.otherOccupiedOrdinals={4};reject(SpeakerBindFault::invalidPhysicalPin);
    d.otherOccupiedOrdinals.clear();
    d.actualEnabledPorts[5].callbackOrdinal=4;
    reject(SpeakerBindFault::invalidPhysicalPin);
    d.actualEnabledPorts[5].callbackOrdinal=5;
    assert(NativeSpeakerDeviceBinder::bind(p,d,99,out).ok());
    assert(!NativeSpeakerOutputReservation::preflight(
       out.verifiedRequest,out.verifiedInventory,99).mayWritePhysicalAudio);
    std::puts("PASS LMS M6 verified device binder: browser ordinals discarded, actual ASIO pins fenced, rollback, physical writes forbidden");
}
