// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/lms/NativeSpeakerOutputReservation.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using namespace armix::lms;
static SpeakerSystemRequest speaker() {
    SpeakerSystemRequest s;
    s.deviceEpoch=13;s.graphRevision=28;s.sampleRate=48000.;
    s.maxFrames=256;s.activeOutputChannels=8;s.ways=2;s.stereo=true;
    s.crossoverHz={1200.};
    s.verifiedPorts={{"ASIO#EXACT/1",0},{"ASIO#EXACT/2",1},
       {"ASIO#EXACT/3",2},{"ASIO#EXACT/4",3},
       {"ASIO#EXACT/5",4},{"ASIO#EXACT/6",5},
       {"ASIO#EXACT/7",6},{"ASIO#EXACT/8",7}};
    OutputPatch p;p.outputId="LOW-L";p.portIdentity="ASIO#EXACT/5";
    p.sourceSide=0;p.section=0;p.mute=true;
    s.outputs.push_back(p);
    p.outputId="LOW-R";p.portIdentity="ASIO#EXACT/7";p.sourceSide=1;
    s.outputs.push_back(p);
    return s;
}
static NativePcmOutputInventory current() {
    NativePcmOutputInventory i;
    i.deviceEpoch=13;i.acceptedGraphRevision=28;i.mainChannelId=101;
    i.deviceIdentity="DRIVER#EXACT";i.sampleRate=48000.;
    i.callbackOutputs=8;i.mainL=0;i.mainR=1;
    i.monitorL=2;i.monitorR=3;i.acceptedChannelIdPcm=true;
    i.allOutputsDisarmed=true;
    i.enabledOutputPorts=speaker().verifiedPorts;
    return i;
}
static void gate() {
    auto s=speaker();auto i=current();
    const auto ok=NativeSpeakerOutputReservation::preflight(s,i,101);
    assert(ok.ok() && !ok.mayWritePhysicalAudio);
    assert((ok.reservedCallbackOrdinals==std::vector<int>{4,6}));
    auto rejected=[&](SpeakerReservationFault fault) {
        const auto r=NativeSpeakerOutputReservation::preflight(s,i,101);
        assert(!r.ok()&&r.fault==fault&&!r.mayWritePhysicalAudio);
        assert(r.reservedCallbackOrdinals.empty());
    };
    i.allOutputsDisarmed=false;rejected(SpeakerReservationFault::notAcceptedPcm);
    i.allOutputsDisarmed=true;
    i.acceptedChannelIdPcm=false;rejected(SpeakerReservationFault::notAcceptedPcm);
    i.acceptedChannelIdPcm=true;
    s.deviceEpoch=12;rejected(SpeakerReservationFault::staleDevice);
    s.deviceEpoch=13;
    s.graphRevision=27;rejected(SpeakerReservationFault::staleGraph);
    s.graphRevision=28;
    i.mainChannelId=999;rejected(SpeakerReservationFault::invalidSource);
    i.mainChannelId=101;
    s.sampleRate=96000.;rejected(SpeakerReservationFault::deviceLayoutMismatch);
    s.sampleRate=48000.;
    i.enabledOutputPorts[4].callbackOrdinal=5;
    rejected(SpeakerReservationFault::unknownPort);
    i.enabledOutputPorts[4].callbackOrdinal=4;
    i.otherOccupiedOutputOrdinals={4};
    rejected(SpeakerReservationFault::occupiedOutput);
    i.otherOccupiedOutputOrdinals.clear();
    s.outputs[0].portIdentity="ASIO#EXACT/3"; // MONITOR 2
    rejected(SpeakerReservationFault::occupiedOutput);
    s.outputs[0].portIdentity="ASIO#EXACT/1"; // MAIN 0
    rejected(SpeakerReservationFault::occupiedOutput);
    s.outputs[0].portIdentity="ASIO#EXACT/5";
    s.outputs[1].portIdentity="ASIO#EXACT/5"; // same pin twice
    rejected(SpeakerReservationFault::occupiedOutput);
    s.outputs[1].portIdentity="ASIO#EXACT/7";
    s.outputs[1].mute=false;
    s.outputs[1].confirmedSpeakerProfile=true;s.outputs[1].limiterEnabled=true;
    rejected(SpeakerReservationFault::unprotectedOutput); // hard clip is NOT speaker protection
    s.outputs[1].mute=true;
    const auto final=NativeSpeakerOutputReservation::preflight(s,i,101);
    assert(final.ok()&&!final.mayWritePhysicalAudio);
}
int main() {
    gate();
    std::puts("PASS LMS M3: exact native ChannelId/epoch/ASIO pin reservation, no hardware output authority");
}
