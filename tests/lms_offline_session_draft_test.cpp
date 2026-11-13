// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/lms/LmsOfflineDraft.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <cstdio>
using namespace armix::lms;
int main() {
    SpeakerSystemRequest bound;
    bound.deviceEpoch=71;bound.graphRevision=34;
    bound.sampleRate=48000.;bound.maxFrames=256;
    bound.activeOutputChannels=8;bound.ways=2;bound.stereo=true;
    bound.crossoverHz={1800.};
    bound.verifiedPorts={{"ASIO:PIN-4",4},{"ASIO:PIN-6",6}};
    OutputPatch lo,hi;
    lo.outputId="LOW-L";lo.portIdentity="ASIO:PIN-4";
    lo.section=0;lo.sourceSide=0;lo.mute=true;lo.gainDb=-3.;
    lo.delayMs=1.75;lo.ceilingDbfs=-9.;lo.limiterEnabled=true;
    lo.peq={{110.,.8,-1.5,true}};
    lo.confirmedSpeakerProfile=true;
    lo.voltageModel=DriverVoltageModel{"TEST-PROFILE",2.,22.,8.,18.,40.,500.,150.};
    hi.outputId="HIGH-R";hi.portIdentity="ASIO:PIN-6";
    hi.section=1;hi.sourceSide=1;hi.mute=false;
    hi.gainDb=-8.;hi.polarityInverted=true;
    bound.outputs={lo,hi};
    OfflineSpeakerDraft saved;
    assert(OfflineSpeakerDraftPolicy::detach(bound,saved));
    assert(OfflineSpeakerDraftPolicy::valid(saved));
    assert(saved.outputs.size()==2 && saved.outputs[0].peq.size()==1);
    assert(saved.outputs[0].outputId=="LOW-L" &&
           saved.outputs[1].outputId=="HIGH-R");
    assert(saved.outputs[0].portIdentity.empty() &&
           saved.outputs[1].portIdentity.empty());
    assert(saved.outputs[0].mute && saved.outputs[1].mute);
    assert(!saved.outputs[0].confirmedSpeakerProfile &&
           !saved.outputs[0].voltageModel);
    assert(saved.outputs[0].ceilingDbfs==-9.);
    SpeakerSystemRequest restored;
    assert(OfflineSpeakerDraftPolicy::restoreDisconnected(saved,restored));
    assert(restored.deviceEpoch==0 && restored.graphRevision==0 &&
           restored.activeOutputChannels==0 && restored.verifiedPorts.empty());
    assert(restored.outputs[0].portIdentity.empty() &&
           restored.outputs[1].portIdentity.empty());
    std::unique_ptr<PreparedSpeakerSystem> dsp;
    const auto preparation=PreparedSpeakerSystem::prepare(restored,dsp);
    assert(!preparation.ok() && !dsp); // NEVER auto-publish after cold open
    auto previous=restored;
    auto bad=saved;bad.version=2;
    assert(!OfflineSpeakerDraftPolicy::restoreDisconnected(bad,restored));
    assert(restored.deviceEpoch==previous.deviceEpoch &&
           restored.outputs.size()==previous.outputs.size());
    bad=saved;bad.outputs[0].portIdentity="ASIO:PIN-4";
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[0].mute=false;
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[0].confirmedSpeakerProfile=true;
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[0].voltageModel=lo.voltageModel;
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.crossoverHz={std::numeric_limits<double>::quiet_NaN()};
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[0].peq[0].q=std::numeric_limits<double>::infinity();
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[1].outputId="LOW-L";
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.outputs[1].section=2;
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    bad=saved;bad.stereo=false;
    assert(!OfflineSpeakerDraftPolicy::valid(bad)); // stale right-side route
    bad=saved;bad.outputs[0].outputId="LOW L";
    assert(!OfflineSpeakerDraftPolicy::valid(bad));
    auto untouched=saved;
    bound.graphRevision=0;
    assert(!OfflineSpeakerDraftPolicy::detach(bound,saved));
    assert(saved.outputs[0].gainDb==untouched.outputs[0].gainDb);
    std::puts("PASS LMS M11: persisted-design detaches physical pins/calibration, cold-open unassigned, strict rollback");
}
