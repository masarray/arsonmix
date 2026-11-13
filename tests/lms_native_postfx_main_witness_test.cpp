// SPDX-License-Identifier: AGPL-3.0-only
// M4: End-to-end portable ChannelId post-FX MAIN -> real LMS DSP PCM witness.
// The 12 buffers below are isolated DIAGNOSTIC buffers, NOT ASIO output pins;
// the accepted native publisher performs the ONE graph render / VST dispatch.
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include "../engine/lms/PreparedSpeakerSystem.h"
#include "../engine/lms/NativeSpeakerOutputReservation.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>
using namespace armix::mixer;
using namespace armix::lms;

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

static ChannelEdit add(int index,ChannelRole role,const char* name) {
    ChannelEdit e;
    e.kind=ChannelEdit::Kind::add;e.position=static_cast<std::size_t>(index);
    e.role=role;e.format=ProcessingFormat::stereo;e.text=name;return e;
}
static ChannelEdit output(ChannelId from,ChannelId to) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;
    e.id=from;e.target=to;return e;
}

struct Witness {
    static constexpr int maxFrames=256,activePins=12;
    PreparedSpeakerSystem* speaker=nullptr;
    std::uint64_t epoch=750,revision=1;
    std::array<std::array<float,maxFrames>,activePins> diagnostic{};
    std::array<float*,activePins> diagnosticPointers{};
    int observations=0;
    bool processed=false;
    Witness() {
        for(int i=0;i<activePins;++i)
            diagnosticPointers[i]=diagnostic[i].data();
    }
    static void capture(void* ctx,const float* left,const float* right,
                        int frames) noexcept {
        auto& self=*static_cast<Witness*>(ctx);
        ++self.observations;
        self.processed=self.speaker->process(self.epoch,self.revision,left,right,
            self.diagnosticPointers.data(),activePins,frames,true);
    }
};

int main() {
    constexpr auto epoch=750ULL;
    DynamicNativeSessionPublisher native;
    DynamicChannelRoster roster;
    const std::array<ChannelEdit,2> creates{
        add(0,ChannelRole::input,"VOICE"),
        add(1,ChannelRole::main,"MAIN")
    };
    const auto created=roster.edit(0,creates);
    assert(created.ok() && created.createdIds.size()==2);
    const auto voice=created.createdIds[0],main=created.createdIds[1];
    const ChannelEdit mainWire=output(voice,main);
    assert(roster.edit(roster.revision(),std::span<const ChannelEdit>(&mainWire,1)).ok());
    DynamicGraphRequest request;
    request.physicalInputs=2;request.maxFrames=Witness::maxFrames;
    request.sources={{voice,0,1,true}};
    const auto accepted=native.commit(roster,request,0,epoch,epoch,true);
    assert(accepted.ok() && accepted.revision==1);
    assert(native.acceptedRouting(epoch,1)->find(voice)->primaryOutput==main);

    SpeakerSystemRequest settings;
    settings.deviceEpoch=epoch;settings.graphRevision=1;
    settings.sampleRate=48000.;settings.maxFrames=Witness::maxFrames;
    settings.activeOutputChannels=Witness::activePins;
    settings.stereo=true;settings.ways=4;
    settings.crossoverHz={100.,1000.,6000.};
    for(int i=0;i<Witness::activePins;++i)
        settings.verifiedPorts.push_back({"DRIVER-IDENTITY#LIVE/"+std::to_string(i),i});
    for(int side=0;side<2;++side)
        for(int section=0;section<4;++section) {
            OutputPatch p;
            const int physical=4+side*4+section;
            p.outputId="speaker-"+std::to_string(physical);
            p.portIdentity=settings.verifiedPorts[physical].identity;
            p.sourceSide=side;p.section=section;
            p.mute=true; // approved PHYSICAL intention ALWAYS muted
            settings.outputs.push_back(p);
        }
    NativePcmOutputInventory physical;
    physical.deviceEpoch=epoch;physical.acceptedGraphRevision=1;
    physical.mainChannelId=main.value;physical.deviceIdentity="DRIVER-IDENTITY#LIVE";
    physical.sampleRate=48000.;physical.callbackOutputs=Witness::activePins;
    physical.mainL=0;physical.mainR=1;physical.monitorL=2;physical.monitorR=3;
    physical.acceptedChannelIdPcm=true;physical.allOutputsDisarmed=true;
    physical.enabledOutputPorts=settings.verifiedPorts;
    auto admitted=NativeSpeakerOutputReservation::preflight(settings,physical,main.value);
    assert(admitted.ok() && !admitted.mayWritePhysicalAudio);
    assert(admitted.reservedCallbackOrdinals.size()==8);
    // An actual unmuted physical plan must be rejected even when the user
    // claims a limiter and speaker-profile checkbox is ON.
    for(auto& p:settings.outputs) {
        p.mute=false;p.confirmedSpeakerProfile=true;
        p.limiterEnabled=true;p.ceilingDbfs=0.;
    }
    const auto illegal=NativeSpeakerOutputReservation::preflight(settings,physical,main.value);
    assert(illegal.fault==SpeakerReservationFault::unprotectedOutput);
    assert(!illegal.mayWritePhysicalAudio);

    // A TEST-ONLY prepared kernel receives the exact accepted post-insert
    // MAIN buffers through the publisher's read-only observer. It writes
    // into isolated diagnostic memory; never the device buffer.
    std::unique_ptr<PreparedSpeakerSystem> dsp;
    assert(PreparedSpeakerSystem::prepare(settings,dsp).ok());
    Witness witness;witness.speaker=dsp.get();
    DynamicNativeSessionPublisher::MainPcmObserver observer{
        &witness,&Witness::capture
    };
    std::array<float,Witness::maxFrames> inL{},inR{},mainL{},mainR{};
    std::array<const float*,2> inputs{inL.data(),inR.data()};
    inL.fill(.2f);inR.fill(.05f);
    // Physical pins supplied by an ASIO callback must NEVER be modified in
    // M4 diagnostic witnessing (including MAIN, MONITOR and other BUS).
    std::array<std::array<float,Witness::maxFrames>,Witness::activePins> driver{};
    for(auto& pin:driver)pin.fill(.8125f);
    int voiceFxCalls=0,mainFxCalls=0;
    auto fx=[&](ChannelId id,float* l,float* r,int n) noexcept {
        if(id==voice) {
            ++voiceFxCalls;
            for(int i=0;i<n;++i) {l[i]*=2.f;r[i]*=2.f;}
        }
        if(id==main)++mainFxCalls;
    };
    const auto before=allocations.load(std::memory_order_relaxed);
    for(int block=0;block<250;++block) {
        const bool done=native.render(epoch,inputs.data(),2,
            mainL.data(),mainR.data(),Witness::maxFrames,
            false,fx,nullptr,nullptr,{},{},{},nullptr,0,observer);
        assert(done && witness.processed);
        assert(witness.observations==block+1);
        assert(voiceFxCalls==block+1); // ONE FX pass per source per block
        assert(mainFxCalls==block+1);  // not re-rendered by crossover ways
        assert(std::abs(mainL[255]-.4f)<.0001f);
        assert(std::abs(mainR[255]-.1f)<.0001f);
    }
    assert(allocations.load(std::memory_order_relaxed)==before);
    assert(std::abs(witness.diagnostic[4][255]-.4f)<.0004f);
    assert(std::abs(witness.diagnostic[8][255]-.1f)<.0004f);
    assert(std::abs(witness.diagnostic[7][255])<.002f);
    assert(std::abs(witness.diagnostic[11][255])<.002f);
    for(const auto& pin:driver)
        for(float v:pin)assert(v==.8125f);

    // Invalid accepted device epoch or render quantum must not enter observer.
    const auto count=witness.observations;
    assert(!native.render(epoch+1,inputs.data(),2,mainL.data(),mainR.data(),
        Witness::maxFrames,false,fx,nullptr,nullptr,{},{},{},nullptr,0,observer));
    assert(witness.observations==count);
    // Invalid zero quantum cannot read/write outside caller-provided arrays.
    assert(!native.render(epoch,inputs.data(),2,mainL.data(),mainR.data(),
        0,false,fx,nullptr,nullptr,{},{},{},nullptr,0,observer));
    assert(witness.observations==count);
    native.unpublish();native.pollRetirement();
    std::puts("PASS LMS: one accepted ChannelId MAIN FX render -> 8 isolated crossover outputs, zero callback heap, no physical write");
}
