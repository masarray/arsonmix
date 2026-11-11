// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
// O1: deterministic buffer-level proof. This does not certify physical FAT.
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include "../engine/core/LiveOutputRouteGate.h"
#include "../engine/core/NativeOutputTransition.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
using namespace armix::mixer;

static bool near(float a,float b,float eps=0.0001f) {
    return std::abs(a-b)<eps;
}
static ChannelEdit add(std::size_t position,ChannelRole role,const char* name) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::add;
    e.position=position;e.role=role;e.format=ProcessingFormat::stereo;
    e.text=name;return e;
}
static ChannelEdit out(ChannelId from,ChannelId to) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;
    e.id=from;e.target=to;return e;
}
struct Harness {
    static constexpr int frames=64;
    DynamicNativeSessionPublisher publisher;
    DynamicChannelRoster roster;
    DynamicGraphRequest request;
    ChannelId voice{},music{},busA{},busB{},main{};
    std::array<std::array<float,frames>,4> input{};
    std::array<std::array<float,frames>,8> output{};
    std::array<const float*,4> inputPointers{};
    std::array<float*,8> outputPointers{};
    int callsA=0,callsB=0;
    std::uint64_t epoch=9157,revision=1;

    Harness(bool sourceOnly=false) {
        const std::array<ChannelEdit,5> create{
            add(0,ChannelRole::input,"VOICE"),
            add(1,ChannelRole::input,"MUSIC"),
            add(2,ChannelRole::group,"BUS A"),
            add(3,ChannelRole::group,"BUS B"),
            add(4,ChannelRole::main,"MAIN")
        };
        auto result=roster.edit(0,create);
        assert(result.ok()&&result.createdIds.size()==5);
        voice=result.createdIds[0];music=result.createdIds[1];
        busA=result.createdIds[2];busB=result.createdIds[3];
        main=result.createdIds[4];
        if(!sourceOnly) {
            const std::array<ChannelEdit,4> links{
                out(voice,busA),out(music,busB),
                out(busA,main),out(busB,main)
            };
            result=roster.edit(1,links);assert(result.ok());
        }
        request.physicalInputs=4;request.maxFrames=frames;
        request.sources={{voice,0,1,true},{music,2,3,true}};
        for(int s=0;s<frames;++s){
            input[0][s]=.25f;input[1][s]=.10f;
            input[2][s]=.07f;input[3][s]=.04f;
        }
        for(int i=0;i<4;++i)inputPointers[i]=input[i].data();
        for(int i=0;i<8;++i)outputPointers[i]=output[i].data();
        const auto report=publisher.commit(roster,request,0,epoch,epoch,true);
        assert(report.ok()&&report.revision==1);
    }
    bool render(int activePorts=8) {
        auto fx=[this](ChannelId id,float* l,float* r,int n) noexcept {
            // Native insert once per BLOCK, even with MAIN+EXTERNAL fan-out.
            if(id==busA) {
                ++callsA;
                for(int f=0;f<n;++f){l[f]*=2.f;r[f]*=2.f;}
            }
            if(id==busB) {
                ++callsB;
                for(int f=0;f<n;++f){l[f]*=3.f;r[f]*=3.f;}
            }
        };
        return publisher.render(epoch,inputPointers.data(),4,
            output[0].data(),output[1].data(),frames,false,fx,
            nullptr,nullptr,{},{},{},
            outputPointers.data(),activePorts);
    }
};
static void verifyIndependentBusPcmAndOncePerFx() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busB,4,5,8));
    assert(h.render());
    assert(h.callsA==1&&h.callsB==1);
    assert(near(h.output[2][63],.50f));
    assert(near(h.output[3][63],.20f));
    assert(near(h.output[4][63],.21f));
    assert(near(h.output[5][63],.12f));
    assert(near(h.output[0][63],.71f));
    assert(near(h.output[1][63],.32f));
    assert(near(h.output[6][63],0.f)&&near(h.output[7][63],0.f));
    const auto assigned=h.publisher.externalOutput(h.epoch,1,h.busA);
    assert(assigned.first==2&&assigned.second==3);
    // Invalid changes reject atomically, keeping accepted PCM unchanged.
    assert(!h.publisher.setExternalOutput(h.epoch-1,1,h.busA,6,7,8));
    assert(!h.publisher.setExternalOutput(h.epoch,2,h.busA,6,7,8));
    assert(!h.publisher.setExternalOutput(h.epoch,1,h.busA,7,8,8));
    assert(!h.publisher.setExternalOutput(h.epoch,1,h.busA,4,4,8));
    assert(!h.publisher.setExternalOutput(h.epoch,1,ChannelId{999},6,7,8));
    assert(h.render());
    assert(h.callsA==2&&h.callsB==2);
    assert(near(h.output[2][63],.50f));
}
static void verifySourceOnlyAndSharedSums() {
    Harness h(true); // neither INPUT is connected to MAIN
    assert(h.publisher.setExternalOutput(h.epoch,1,h.voice,2,3,4));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.music,2,3,4));
    assert(h.render(4));
    assert(near(h.output[0][63],0.f)&&near(h.output[1][63],0.f));
    assert(near(h.output[2][63],.32f));
    assert(near(h.output[3][63],.14f));
    // Disconnect retains SOURCE and MAIN state: output stops after ramp.
    assert(h.publisher.setExternalOutput(h.epoch,1,h.voice,-1,-1,4));
    assert(h.render(4));
    assert(near(h.output[2][63],.07f));
    assert(near(h.output[3][63],.04f));
    assert(h.publisher.externalOutput(h.epoch,1,h.voice).first==-1);
}
// Full eight-pin fixture: each destination receives a distinct accepted
// ChannelId post-FX PCM signal. This is NOT a physical-device FAT.
static void verifyEightPinIsolationAndAppliedReadback() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busB,4,5,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.voice,6,7,8));
    // A CONTROL target is not evidence that the callback has installed PCM.
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==-1);
    assert(h.render(8));
    assert(h.callsA==1 && h.callsB==1);
    assert(near(h.output[0][63],.71f));
    assert(near(h.output[1][63],.32f));
    assert(near(h.output[2][63],.50f));
    assert(near(h.output[3][63],.20f));
    assert(near(h.output[4][63],.21f));
    assert(near(h.output[5][63],.12f));
    assert(near(h.output[6][63],.25f));
    assert(near(h.output[7][63],.10f));
    const auto applied=h.publisher.appliedExternalOutput(h.epoch,1,h.busA);
    assert(applied.first==2 && applied.second==3);
    // Only physically supplied callback destinations may be targeted.
    assert(!h.publisher.setExternalOutput(h.epoch,1,h.busB,6,7,4));
    assert(h.render(8));
    assert(near(h.output[4][63],.21f));
}
static void verifyFourPinMainAndOneBus() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,4));
    assert(!h.publisher.setExternalOutput(h.epoch,1,h.busB,4,5,4));
    assert(h.render(4));
    assert(h.callsA==1 && h.callsB==1);
    assert(near(h.output[0][63],.71f));
    assert(near(h.output[2][63],.50f));
    assert(near(h.output[3][63],.20f));
}
// A failed/revoked render must silence ALL ASIO callback pins. Previously
// only MAIN/MONITOR were cleared, leaving potentially stale amplifier PCM.
static void verifyFailClosedAllPhysicalOutputBuffers() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.render(8));
    const auto fx=[](ChannelId,float*,float*,int) noexcept {};
    for(auto& output:h.output)output.fill(.875f);
    assert(!h.publisher.render(h.epoch,h.inputPointers.data(),4,
        h.output[0].data(),h.output[1].data(),Harness::frames,true,fx,
        nullptr,nullptr,{},{},{},h.outputPointers.data(),8));
    for(const auto& output:h.output)
        for(float sample:output)assert(near(sample,0.f));
    // An epoch mismatch also revokes the physical taps, without leaking
    // the last render or invoking any hosted plug-in.
    for(auto& output:h.output)output.fill(.875f);
    assert(!h.publisher.render(h.epoch+1,h.inputPointers.data(),4,
        h.output[0].data(),h.output[1].data(),Harness::frames,false,fx,
        nullptr,nullptr,{},{},{},h.outputPointers.data(),8));
    for(const auto& output:h.output)
        for(float sample:output)assert(near(sample,0.f));
}
// Invalid ASIO callback alias must not borrow a MAIN or MONITOR pin.
// All physical buffers are cleared on the failed render.
static void verifyAliasedCallbackPinFailsClosed() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    h.outputPointers[2]=h.output[0].data(); // same buffer at another ordinal
    for(auto& output:h.output)output.fill(.625f);
    assert(!h.render(8));
    // Verify only the eight buffers actually exported by this callback.
    // output[2] has been deliberately detached and must not be counted.
    for(int c=0;c<8;++c)
        for(int frame=0;frame<Harness::frames;++frame)
            assert(near(h.outputPointers[c][frame],0.f));
}

static void verifyCrossExternalAliasFailsClosedAndRecovers() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busB,4,5,8));
    assert(h.render(8));
    assert(near(h.output[2][63],.50f)&&near(h.output[4][63],.21f));
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==2);
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busB).first==4);
    // These two callback ordinals are distinct physical ASIO pins, but a
    // broken backend supplies one shared float* for their destination.
    // Previously the two BUS PCM paths were SUMMED into the same buffer.
    h.outputPointers[4]=h.output[2].data();
    for(auto& output:h.output)output.fill(.875f);
    assert(!h.render(8));
    // Readback must reflect the actual rejected callback, never keep
    // claiming the last accepted physical OUT A/B was still LIVE.
    assert(h.publisher.externalOutput(h.epoch,1,h.busA).first==2);
    assert(h.publisher.externalOutput(h.epoch,1,h.busB).first==4);
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==-1);
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busB).first==-1);
    // A native failure must silence even MAIN and untouched external pairs,
    // not leave stale samples driving an amplifier.
    for(int c=0;c<8;++c)
        for(float* sample=h.outputPointers[c];sample!=h.outputPointers[c]+Harness::frames;++sample)
            assert(near(*sample,0.f));
    // Transient callback aliasing must not permanently corrupt routing:
    // original ChannelIds, destinations, and FX continue on the next
    // legitimate, uniquely-backed JUCE block without re-ARM or re-WIRE.
    h.outputPointers[4]=h.output[4].data();
    assert(h.render(8));
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==2);
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busB).first==4);
    assert(near(h.output[2][63],.50f)&&near(h.output[3][63],.20f));
    assert(near(h.output[4][63],.21f)&&near(h.output[5][63],.12f));
    assert(near(h.output[0][63],.71f)&&near(h.output[1][63],.32f));
}
static void verifyCurrentEpochRejectedQuantumRevokesPhysicalReadback() {
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.render(8));
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==2);
    const auto fx=[](ChannelId,float*,float*,int) noexcept {};
    // A mismatch in live physical callback count is not an accepted render
    // even though the already-applied ASIO patch is still in CONTROL.
    assert(!h.publisher.render(h.epoch,h.inputPointers.data(),3,
        h.output[0].data(),h.output[1].data(),Harness::frames,false,fx,
        nullptr,nullptr,{},{},{},h.outputPointers.data(),8));
    assert(h.publisher.externalOutput(h.epoch,1,h.busA).first==2);
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==-1);
    for(const auto& output:h.output)
        for(float sample:output)assert(near(sample,0.f));
    assert(h.render(8));
    assert(h.publisher.appliedExternalOutput(h.epoch,1,h.busA).first==2);
}
// Main MUTE is not a device/global STOP. The native callback stays active,
// while 64-sample MAIN attenuation leaves distinct ASIO BUS taps unchanged.
static void verifyMainOnlyMuteAndAllOutputFxTransitions() {
    using Gate=armix::mixer::LiveOutputRouteGate;
    const auto live=Gate::evaluate(true,true,true,true,false,true,false,true);
    assert(live.renderMain && live.hardware);
    // Legacy behavior is unchanged; global ARM/device shutdown still wins.
    assert(!Gate::evaluate(true,true,true,true,false,true,false).renderMain);
    assert(!Gate::evaluate(true,true,true,false,false,true,false,true).renderMain);
    assert(!Gate::evaluate(true,true,true,true,false,false,false,true).renderMain);
    armix::mixer::MainOnlyMuteSlew mute;
    Harness h;
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busB,4,5,8));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.voice,6,7,8));
    assert(h.render(8));
    for(int f=0;f<Harness::frames;++f) {
        const float g=mute.next(true);
        h.output[0][f]*=g;
        h.output[1][f]*=g;
    }
    assert(near(h.output[0][63],0.f)&&near(h.output[1][63],0.f));
    assert(near(h.output[2][63],.50f)&&near(h.output[4][63],.21f));
    assert(near(h.output[6][63],.25f)&&near(h.output[7][63],.10f));
    for(int f=0;f<Harness::frames;++f)
        assert(near(mute.next(false),static_cast<float>(f+1)/64.f));
    mute.reset(true); // global disarm primes 0, preventing restart bleed
    assert(near(mute.next(true),0.f));
    assert(h.render(8));
    float mainPeakL=0.f,mainPeakR=0.f;
    armix::mixer::NativeOutputTransition::apply(h.outputPointers.data(),8,
        Harness::frames,1,false,false,h.output[0].data(),
        h.output[1].data(),mainPeakL,mainPeakR);
    assert(mainPeakL>0.f&&mainPeakR>0.f);
    for(const auto& pair:h.output)
        assert(near(pair[63],0.f)); // ALL physical pins fade out
    assert(h.callsA==2&&h.callsB==2); // effects executed ONCE per render
    assert(h.render(8));
    armix::mixer::NativeOutputTransition::apply(h.outputPointers.data(),8,
        Harness::frames,2,false,false,h.output[0].data(),
        h.output[1].data(),mainPeakL,mainPeakR);
    assert(near(h.output[2][0],0.f)&&near(h.output[2][63],.50f));
    assert(near(h.output[4][63],.21f)&&near(h.output[6][63],.25f));
    assert(h.callsA==3&&h.callsB==3);
    // Global graph handoff declick also reaches every independent physical pin.
    assert(h.render(8));
    armix::mixer::NativeOutputTransition::apply(h.outputPointers.data(),8,
        Harness::frames,0,true,false,h.output[0].data(),
        h.output[1].data(),mainPeakL,mainPeakR);
    for(const auto& pair:h.output)assert(near(pair[63],0.f));
    assert(h.callsA==4&&h.callsB==4);
}
static void verifyLiveFaderMuteAnd2PortBounds() {
    Harness h;
    // A 2-output device has no third/fourth callback ordinal. A driver-level
    // MAIN ownership fence is checked by LiveDryRoutingLab, not this DSP.
    assert(!h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,2));
    assert(h.publisher.setExternalOutput(h.epoch,1,h.busA,2,3,8));
    assert(h.publisher.setChannelMix(h.epoch,1,h.busA,.5f,0.f));
    assert(h.render());
    assert(near(h.output[2][63],.25f));
    assert(near(h.output[3][63],.10f));
    assert(h.publisher.setChannelMuteSolo(h.epoch,1,h.busA,true,false));
    assert(h.render());
    assert(near(h.output[2][63],0.f));
    assert(near(h.output[3][63],0.f));
    // Other bus remains unaffected by the mute or physical tap.
    assert(near(h.output[0][63],.21f));
    assert(near(h.output[1][63],.12f));
}
int main() {
    verifyIndependentBusPcmAndOncePerFx();
    verifyEightPinIsolationAndAppliedReadback();
    verifyFourPinMainAndOneBus();
    verifyFailClosedAllPhysicalOutputBuffers();
    verifyAliasedCallbackPinFailsClosed();
    verifyCrossExternalAliasFailsClosedAndRecovers();
    verifyCurrentEpochRejectedQuantumRevokesPhysicalReadback();
    verifyMainOnlyMuteAndAllOutputFxTransitions();
    verifySourceOnlyAndSharedSums();
    verifyLiveFaderMuteAnd2PortBounds();
    std::puts("O1 physical external OUT: independent PCM, SRC ONLY, gain/mute, shared sum and stale target PASS");
    return 0;
}
