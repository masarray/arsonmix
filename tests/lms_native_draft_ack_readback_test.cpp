// SPDX-License-Identifier: AGPL-3.0-only
// One accepted native ChannelId PCM graph owns the speaker design admission.
// This test never opens, borrows, or writes an ASIO hardware output buffer.
#include "../engine/core/DynamicNativeSessionPublisher.h"
#if defined(NDEBUG)
#undef NDEBUG // Release/CTest must execute every safety assertion.
#endif
#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string>
using namespace armix::mixer;
using namespace armix::lms;

static ChannelEdit create(int at,ChannelRole role,const char* name){
    ChannelEdit e;
    e.kind=ChannelEdit::Kind::add;e.position=static_cast<std::size_t>(at);
    e.role=role;e.format=ProcessingFormat::stereo;e.text=name;return e;
}
static SpeakerSystemRequest design(std::uint64_t epoch,std::uint64_t graph){
    SpeakerSystemRequest s;
    s.deviceEpoch=epoch;s.graphRevision=graph;
    s.sampleRate=48000.;s.maxFrames=256;
    s.activeOutputChannels=8;s.ways=2;s.stereo=true;
    s.crossoverHz={1400.};
    for(int i=0;i<8;++i)s.verifiedPorts.push_back(
        {"ASIO-DEVICE-IDENTITY:PIN-"+std::to_string(i),i});
    OutputPatch a,b;
    a.outputId="HF-L";a.portIdentity=s.verifiedPorts[4].identity;
    a.sourceSide=0;a.section=1;a.mute=true;
    b.outputId="HF-R";b.portIdentity=s.verifiedPorts[6].identity;
    b.sourceSide=1;b.section=1;b.mute=true;
    s.outputs={a,b};
    return s;
}
static NativePcmOutputInventory actual(const SpeakerSystemRequest& s,
                                       ChannelId main){
    NativePcmOutputInventory p;
    p.deviceEpoch=s.deviceEpoch;
    p.acceptedGraphRevision=s.graphRevision;
    p.mainChannelId=main.value;
    p.deviceIdentity="ASIO-DEVICE-IDENTITY";
    p.sampleRate=s.sampleRate;
    p.callbackOutputs=s.activeOutputChannels;
    p.mainL=0;p.mainR=1;p.monitorL=2;p.monitorR=3;
    p.allOutputsDisarmed=true;p.acceptedChannelIdPcm=true;
    p.enabledOutputPorts=s.verifiedPorts;
    return p;
}
int main() {
    constexpr std::uint64_t epoch=9876;
    DynamicChannelRoster roster;
    const std::array<ChannelEdit,2> add{
        create(0,ChannelRole::input,"VOICE"),
        create(1,ChannelRole::main,"MAIN")
    };
    const auto made=roster.edit(0,add);
    assert(made.ok()&&made.createdIds.size()==2);
    const auto voice=made.createdIds[0],main=made.createdIds[1];
    ChannelEdit wire;
    wire.kind=ChannelEdit::Kind::setPrimaryOutput;wire.id=voice;wire.target=main;
    assert(roster.edit(roster.revision(),std::span<const ChannelEdit>(&wire,1)).ok());
    DynamicGraphRequest request;
    request.physicalInputs=2;request.maxFrames=256;
    request.sources={{voice,0,1,true}};
    DynamicNativeSessionPublisher native;
    assert(native.commit(roster,request,0,epoch,epoch,true).ok());
    auto s=design(epoch,1);
    auto p=actual(s,main);
    auto accepted=native.acceptSpeakerDraft(s,p,main,0);
    assert(accepted.ok()&&accepted.designRevision==1);
    assert(!accepted.hardwareApplied&&accepted.preparedScratchBytes>0);
    auto* read=native.speakerDraftReadback(epoch,1);
    assert(read&&read->validatedDraft&&!read->hardwareApplied);
    assert(read->designRevision==1&&read->graphRevision==1);
    assert(read->deviceEpoch==epoch&&read->mainSource==main);
    assert(read->reservedOutputOrdinals.size()==2);
    assert(read->reservedOutputOrdinals[0]==4&&read->reservedOutputOrdinals[1]==6);
    assert(read->design.crossoverHz==s.crossoverHz);
    assert(read->design.outputs[0].portIdentity==s.outputs[0].portIdentity);
    // Replay of stale browser revision cannot stomp the first acceptance.
    const auto stale=native.acceptSpeakerDraft(s,p,main,0);
    assert(stale.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::staleDraft);
    assert(stale.designRevision==1&&!stale.validatedDraft);
    assert(native.speakerDraftReadback(epoch,1)->designRevision==1);
    // Native source authority belongs to MAIN, never an arbitrary sender.
    const auto notMain=native.acceptSpeakerDraft(s,p,voice,1);
    assert(notMain.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::invalidSource);
    assert(native.speakerDraftReadback(epoch,1)->designRevision==1);
    // Unsafe user unmute + checkbox limiter is not an approved protection.
    s.outputs[0].mute=false;
    s.outputs[0].limiterEnabled=true;
    s.outputs[0].confirmedSpeakerProfile=true;
    const auto unsafe=native.acceptSpeakerDraft(s,p,main,1);
    assert(unsafe.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::invalidReservation);
    assert(unsafe.reservationFault==SpeakerReservationFault::unprotectedOutput);
    assert(!unsafe.hardwareApplied);
    assert(native.speakerDraftReadback(epoch,1)->designRevision==1);
    s.outputs[0].mute=true;
    // A physically occupied MAIN or MONITOR socket is not a speaker target.
    s.outputs[0].portIdentity=s.verifiedPorts[0].identity;
    const auto collision=native.acceptSpeakerDraft(s,p,main,1);
    assert(collision.reservationFault==SpeakerReservationFault::occupiedOutput);
    s.outputs[0].portIdentity=s.verifiedPorts[4].identity;
    // Rejected DSP parameters cannot corrupt the accepted draft.
    s.crossoverHz={30000.};
    const auto badFilter=native.acceptSpeakerDraft(s,p,main,1);
    assert(badFilter.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::invalidDsp);
    assert(badFilter.planFault==SpeakerPlanFault::invalidCrossover);
    s.crossoverHz={1800.};
    s.maxFrames=128;
    const auto badQuantum=native.acceptSpeakerDraft(s,p,main,1);
    assert(badQuantum.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::invalidDsp);
    s.maxFrames=256;
    // A CONTROL-supplied manufacturer/measurement voltage model can be
    // prepared and revisioned, but it NEVER grants ASIO activation.
    DriverVoltageModel voltage;
    voltage.profileId="TEST-ONLY";
    voltage.dacFullScaleVrms=2.;
    voltage.amplifierVoltageGain=25.;
    voltage.continuousDriverVrms=12.;
    voltage.driverPeakVolts=21.;
    voltage.minimumHighpassHz=80.;
    s.outputs[0].voltageModel=voltage;
    // Operator design revision advances ONLY after full validation and prep.
    const auto updated=native.acceptSpeakerDraft(s,p,main,1);
    assert(updated.ok()&&updated.designRevision==2);
    assert(native.speakerDraftReadback(epoch,1)->design.crossoverHz[0]==1800.);
    const auto* calibratedDraft=native.speakerDraftReadback(epoch,1);
    assert(calibratedDraft->design.outputs[0].voltageModel.has_value());
    assert(calibratedDraft->design.outputs[0].voltageModel->profileId=="TEST-ONLY");
    const auto diagnostic=native.speakerDiagnosticReadback(epoch,1);
    assert(diagnostic.available && !diagnostic.hardwareApplied &&
           diagnostic.designRevision==2);
    s.outputs[0].mute=false;
    const auto stillNotArmed=native.acceptSpeakerDraft(s,p,main,2);
    assert(stillNotArmed.fault==DynamicNativeSessionPublisher::SpeakerDraftFault::invalidReservation);
    assert(stillNotArmed.reservationFault==SpeakerReservationFault::unprotectedOutput);
    assert(native.speakerDraftReadback(epoch,1)->designRevision==2);
    s.outputs[0].mute=true;
    assert(!native.speakerDraftReadback(epoch+1,1));
    assert(!native.speakerDraftReadback(epoch,2));
    // Accepted structural PCM revision invalidates ALL previously prepared
    // speaker state; even if driver output ordinals happen to be identical.
    const auto nextGraph=native.commit(roster,request,1,epoch,epoch,true);
    assert(nextGraph.ok()&&nextGraph.revision==2);
    assert(!native.speakerDraftReadback(epoch,1));
    assert(!native.speakerDraftReadback(epoch,2));
    s.graphRevision=2;p.acceptedGraphRevision=2;
    const auto fresh=native.acceptSpeakerDraft(s,p,main,0);
    assert(fresh.ok()&&fresh.designRevision==1);
    native.unpublish();
    assert(!native.speakerDraftReadback(epoch,2));
    std::puts("PASS LMS M5: same-native-PCM draft ACK/readback, revision rollback, pin safety, structural revoke, hardware disabled");
}
