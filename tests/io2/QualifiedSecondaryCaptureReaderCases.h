// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../engine/device/QualifiedSecondaryCaptureReader.h"
#include "../../engine/device/QualifiedCaptureHostMode.h"
#include "../../engine/device/QualifiedCaptureAdmission.h"
#include "../../engine/device/SecondaryCaptureReadbackState.h"
#include "../../engine/device/QualifiedInputGraphBridge.h"
#include "../../engine/core/DynamicNativeSessionPublisher.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace armix::io2_clocked_test {
namespace d=armix::device;
namespace m=armix::mixer;
inline void check(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"IO2_CLOCKED_FAIL %s\n",why);std::abort();}
}
inline bool near(float a,float b,float tolerance=0.03f) {
    return std::abs(a-b)<tolerance;
}
inline void run() {
    using Health=d::SecondaryCaptureReadbackState;
    const auto health=d::classifySecondaryCapture;
    check(health(false,false,false,false,false,0,0)==Health::notConfigured,
          "No secondary is not a phantom hardware device");
    check(health(true,true,true,false,true,4,4)==Health::needsRemap,
          "Actual A epoch change invalidates B even with old callback evidence");
    check(health(true,false,true,false,false,4,4)==Health::degraded,
          "A epoch mismatch never advertises B capture LIVE");
    check(health(true,false,false,true,true,0,0)==Health::degraded,
          "B fault isolates capture without optimistic READY");
    check(health(true,false,true,false,true,0,0)==Health::waitingForPcm,
          "Driver-open is not capture PCM evidence");
    check(health(true,false,true,false,true,4,0)==Health::waitingForPcm,
          "B-only producer blocks do not prove primary consumption");
    check(health(true,false,true,false,true,4,2)==Health::pcmFlowObserved,
          "Both independent capture and master PCM consumption observed");
    check(d::secondaryCaptureStateName(Health::pcmFlowObserved)==
          std::string_view("PCM_FLOW_OBSERVED"),
          "Readback state is stable and machine-readable");
    // A successful FIFO pull is not proof of sound: ASRC may output only
    // priming (or later starvation) frames. Epoch/lease and time freshness
    // are required for any positive B PCM flow observation.
    check(d::secondaryUsableAsrcFrames(128,128,0)==0 &&
          d::secondaryUsableAsrcFrames(128,0,128)==0 &&
          d::secondaryUsableAsrcFrames(256,128,64)==64,
          "Only ASRC FIR-valid frames count; priming and underflow excluded");
    d::SecondaryCaptureFlowEvidence evidence;
    check(evidence.recentUsableFrames(22,0,0,0,0,100.,true)==0,
          "Initial B lease observation is only a baseline");
    check(evidence.recentUsableFrames(22,1,128,128,0,110.,true)==0,
          "Priming-only authenticated pulls must not advertise B PCM");
    check(evidence.recentUsableFrames(22,2,256,128,0,120.,true)==128,
          "Both producer and actually usable consumer progress within 500ms");
    check(evidence.recentUsableFrames(22,2,384,128,0,700.,true)==0,
          "Stopped B producer cannot remain FLOW_OBSERVED indefinitely");
    check(evidence.recentUsableFrames(23,100,500,0,0,710.,true)==0,
          "Reconnect with new B lease cannot reuse accumulated PCM history");
    check(evidence.recentUsableFrames(23,101,628,0,0,720.,true)>0,
          "Fresh B callback and ASRC after new lease restores flow evidence");
    check(evidence.recentUsableFrames(23,102,756,128,0,725.,false)==0,
          "Faulted capture immediately invalidates timed flow evidence");
    check(evidence.recentUsableFrames(23,102,756,128,0,730.,true)==0,
          "Recovery cannot reuse data accumulated before fault");

    // The host may select direct A or qualified A+B, never a partial bridge.
    check(d::qualifiedCaptureHostMode(false,false)==
          d::QualifiedCaptureHostMode::directPrimary,
          "A-only direct fast path remains default");
    check(d::qualifiedCaptureHostMode(true,true)==
          d::QualifiedCaptureHostMode::qualifiedDual,
          "both qualified resources required for B capture");
    check(d::qualifiedCaptureHostMode(true,false)==
          d::QualifiedCaptureHostMode::rejectPartial &&
          d::qualifiedCaptureHostMode(false,true)==
          d::QualifiedCaptureHostMode::rejectPartial,
          "partial clock/reader admission must fail closed");
    constexpr std::uint64_t epoch=71,captureGeneration=94;
    constexpr int frames=128;
    m::DynamicChannelRoster roster;
    std::array<m::ChannelEdit,3> create{};
    for(int i=0;i<3;++i){
        create[i].kind=m::ChannelEdit::Kind::add;
        create[i].position=static_cast<std::size_t>(i);
        create[i].role=i==2?m::ChannelRole::main:m::ChannelRole::input;
        create[i].format=i==0?m::ProcessingFormat::mono:m::ProcessingFormat::stereo;
    }
    const auto added=roster.edit(0,create);
    check(added.ok()&&added.createdIds.size()==3,"qualified ChannelId roster");
    const auto a=added.createdIds[0],b=added.createdIds[1],
               main=added.createdIds[2];
    std::array<m::ChannelEdit,2> wiring{};
    for(int i=0;i<2;++i){
        wiring[i].kind=m::ChannelEdit::Kind::setPrimaryOutput;
        wiring[i].id=i==0?a:b;wiring[i].target=main;
    }
    check(roster.edit(roster.revision(),wiring).ok(),"both inputs WIRE MAIN");
    const std::array<int,1> aPins{0};
    const std::array<int,2> bPins{0,7};
    const std::array<d::CaptureEndpointView,2> devices{{
        {"verified-A",epoch,true,aPins,1001},
        {"verified-B",captureGeneration,false,bPins,2002}
    }};
    const std::array<d::QualifiedChannelSource,2> bindings{{
        {a,{"verified-A",0},{},false},
        {b,{"verified-B",0},{"verified-B",7},true}
    }};
    d::MultiDeviceInputPlan plan;
    d::MultiDeviceInputFault err;
    check(d::MultiDeviceInputPlan::prepare(epoch,devices,bindings,
        roster.channels(),plan,err),"two physical namespaces qualified");
    m::DynamicGraphRequest old;old.physicalInputs=1;old.maxFrames=frames;
    old.sources={{a,0,-1,false}};
    m::DynamicGraphRequest request;
    d::QualifiedInputGraphBridge::Fault bridgeError{};
    check(d::QualifiedInputGraphBridge::stage(plan,old,request,bridgeError),
        "control maps both verified sources into same graph");
    m::DynamicNativeSessionPublisher native;
    check(native.commit(roster,request,0,epoch,epoch,true,
        {},false,false,&plan).ok(),"one PCM and device map publication");
    check(native.hasQualifiedInputMap(epoch,1),
          "v1 .smix cannot safely save virtual A+B input ordinals");
    check(!native.hasQualifiedInputMap(epoch+1,1)&&
          !native.hasQualifiedInputMap(epoch,0),
          "stale graph generation cannot authorize persistence");

    d::SecondaryCaptureClockStream clock;
    const d::SecondaryCaptureClockStream::Format format{
        44100.,48000.,frames,frames,2,captureGeneration,epoch,2002};
    // REAL clock/ASRC regression (not just mocked numbers): a successful
    // read of one B block can be entirely priming silence.
    {
        d::SecondaryCaptureClockStream probeClock;
        check(probeClock.prepare(format)&&probeClock.activate(),
              "real ASRC priming probe prepared on control thread");
        std::array<float,frames> sourceL{},sourceR{},destL{},destR{};
        sourceL.fill(.4f);sourceR.fill(.6f);
        const float* probeIn[]{sourceL.data(),sourceR.data()};
        float* probeOut[]{destL.data(),destR.data()};
        check(probeClock.push(probeIn,2,frames),
              "one independent B producer block accepted");
        check(probeClock.pull(probeOut,2,frames,
              captureGeneration,epoch,2002),
              "B primary pull is authorized even while priming");
        const auto probe=probeClock.status();
        check(probe.asrc.renderedFrames==frames &&
              probe.asrc.primingFrames==frames &&
              d::secondaryUsableAsrcFrames(
                  probe.asrc.renderedFrames,probe.asrc.primingFrames,
                  probe.asrc.underflowFrames)==0,
              "actual 44.1k->48k ASRC authorized pull has NO usable PCM");
        probeClock.stop();
    }

    check(clock.prepare(format)&&clock.activate(),"independent 44.1k B into 48k A ASRC ready");
    using Admission=d::QualifiedCaptureAdmission;
    check(Admission::verify(plan,clock,epoch,1,frames)==Admission::Fault::none,
          "exact two-device qualified CONTROL admission");
    check(Admission::verify(plan,clock,epoch+1,1,frames)==
          Admission::Fault::stalePrimary,
          "stale A output epoch cannot publish qualified PCM");
    check(Admission::verify(plan,clock,epoch,2,frames)==
          Admission::Fault::wrongPrimaryPins,
          "virtual A+B input count must not replace real A callback count");
    check(Admission::verify(plan,clock,epoch,1,0)==
          Admission::Fault::unsafeFrames,
          "no zero or unsafe native callback scratch capacity");
    auto wrongDevice=devices;
    wrongDevice[1].deviceLease=3003;
    d::MultiDeviceInputPlan wrongLeasePlan;
    check(d::MultiDeviceInputPlan::prepare(epoch,wrongDevice,bindings,
          roster.channels(),wrongLeasePlan,err),
          "negative independent B lease candidate prepared");
    check(Admission::verify(wrongLeasePlan,clock,epoch,1,frames)==
          Admission::Fault::wrongCaptureLease,
          "driver B ASRC lease cannot silently follow device-map rebind");

    std::array<float,4096> bLeft{},bRight{};
    bLeft.fill(.4f);bRight.fill(.6f);
    const float* bInput[]{bLeft.data(),bRight.data()};
    check(clock.push(bInput,2,4096),"producer FIFO primed before A render");
    d::QualifiedSecondaryCaptureReader reader;
    check(reader.prepare(plan,frames),"reader allocates only on CONTROL");
    std::array<float,frames> aRaw{},left{},right{};
    aRaw.fill(.2f);const float* aInput[]{aRaw.data()};
    int aFx=0,bFx=0,mainFx=0;
    const auto fx=[&](m::ChannelId id,float*,float*,int) noexcept {
        if(id==a)++aFx;
        if(id==b)++bFx;
        if(id==main)++mainFx;
    };
    const auto render=[&](bool nested=false) {
        return reader.withBlocks(epoch,aInput,1,frames,clock,
            [&](std::span<const d::MultiDeviceInputPlan::CaptureBlock> blocks) noexcept {
                if(nested)
                    check(!reader.withBlocks(epoch,aInput,1,frames,clock,
                        [](std::span<const d::MultiDeviceInputPlan::CaptureBlock>) noexcept {
                            return true;
                        }),"overlap must fail silent without scratching B");
                return native.render(epoch,nullptr,0,left.data(),right.data(),
                    frames,false,fx,nullptr,nullptr,{},{},{},nullptr,0,blocks);
            });
    };
    check(render(true),"A+B native render consumes B ASRC exactly once");
    check(aFx==1&&bFx==1&&mainFx==1,"one ChannelId FX pass per block");
    constexpr float monoCenter=.7071067811865475f;
    check(near(left[frames-1],.2f*monoCenter+.4f)&&
          near(right[frames-1],.2f*monoCenter+.6f),
          "B's real clock bridge PCM reaches MAIN with A");
    check(reader.status().secondaryAccepted==1&&reader.status().overlaps==1,
          "one successful capture consumption, overlap rejected");
    // Structural WIRE/FX edits cannot silently drop the verified B map.
    // An identical authenticated topology is allowed even with MAIN armed
    // when the native caller proved this is internal WIRE-only.
    const auto wired=native.commit(roster,request,1,epoch,epoch,false,
                                   {},true,false);
    check(wired.ok()&&wired.revision==2,
          "WIRE-only graph revision inherits qualified input lease");
    check(native.hasQualifiedInputMap(epoch,2),
          "WIRE-only revision must not restore unsafe v1 .smix saving");
    check(render(),"B still audible after internal graph recompile");
    check(near(left[frames-1],.2f*monoCenter+.4f),
          "committed BUS/WIRE revision retains capture B");
    auto wrongPatch=request;
    wrongPatch.sources.back().inputL=0; // erroneously aliases A, no longer B map
    const auto wrongCommit=native.commit(roster,wrongPatch,2,epoch,epoch,false,
                                   {},true,false);
    check(wrongCommit.fault==m::DynamicNativeSessionPublisher::Fault::invalidCandidate
          &&native.acceptedRevision()==2,
          "cannot silently remap B by reusing native WIRE-only authorization");
    const auto whileArmed=native.commit(roster,request,2,epoch,epoch,false,
                                       {},true,false,&plan);
    check(whileArmed.fault==m::DynamicNativeSessionPublisher::Fault::outputArmed,
          "explicit device map replacement must disarm MAIN first");

    // B lost/failed is SILENCE in B channels, never relabeled A Input1.
    clock.fault();
    check(render(),"faulted B cannot block verified primary A");
    check(near(left[frames-1],.2f*monoCenter,0.005f)&&
          near(right[frames-1],.2f*monoCenter,0.005f),
          "B dropout leaves A audible and correctly isolated");
    check(reader.status().secondarySilent==1,
          "native B fault recorded without false ready");
    check(!reader.withBlocks(epoch+1,aInput,1,frames,clock,
        [](std::span<const d::MultiDeviceInputPlan::CaptureBlock>) noexcept {
            return true;
        }),"primary epoch invalidates entire callback");
    check(reader.status().rejected==1,"stale primary epoch counted");

    // Same generation but changed physical device lease is not sufficient
    // to republish audio or borrow another ASIO driver's PCM.
    reader.stop();
    auto changed=devices;
    changed[1].deviceLease=9999;
    d::MultiDeviceInputPlan wrong;
    check(d::MultiDeviceInputPlan::prepare(epoch,changed,bindings,
        roster.channels(),wrong,err),"detached revised B lease prepared");
    check(reader.prepare(wrong,frames),"controlled new reader prepared");
    check(render(),"new B lease absent in ASRC must not break A");
    check(near(left[frames-1],.2f*monoCenter,0.005f),
          "stale ASRC cannot be relabeled into newly leased device");
    reader.stop();
    clock.stop();
    // IO-2D persistence recovery: a saved primary-only .smix can replace
    // A+B only by an explicit, muted native session transaction. WIRE/FX
    // omission of a map still inherits B and must never silently alias pins.
    using MapChange=m::DynamicNativeSessionPublisher::QualifiedMapChange;
    check(native.hasQualifiedInputMap(epoch,2),
          "prior A+B PCM map installed before restore");
    const auto unsafeRestore=native.commit(
        roster,old,2,epoch,epoch,false,{},false,false,nullptr,
        MapChange::replaceWithPrimaryOnly);
    check(unsafeRestore.fault==
            m::DynamicNativeSessionPublisher::Fault::outputArmed &&
          native.hasQualifiedInputMap(epoch,2),
          "unmuted primary-only .smix replacement cannot remove B map");
    const auto restored=native.commit(
        roster,old,2,epoch,epoch,true,{},false,false,nullptr,
        MapChange::replaceWithPrimaryOnly);
    check(restored.ok()&&restored.revision==3 &&
          !native.hasQualifiedInputMap(epoch,3),
          "muted explicit A-only .smix publishes one new PCM without B map");
    check(native.render(epoch,aInput,1,left.data(),right.data(),
           frames,false,fx,nullptr,nullptr,{},{},{},nullptr,0),
          "restored A-only PCM accepts raw primary callback again");
    check(near(left[frames-1],.2f*monoCenter,0.005f)&&
          near(right[frames-1],.2f*monoCenter,0.005f),
          "restored primary-only .smix never aliases or sums B PCM");
    const auto doubleRemove=native.commit(
        roster,old,3,epoch,epoch,true,{},false,false,nullptr,
        MapChange::replaceWithPrimaryOnly);
    check(doubleRemove.fault==
          m::DynamicNativeSessionPublisher::Fault::invalidCandidate,
          "a nonqualified A-only session cannot spoof a B map retirement");
    std::puts("IO2_CLOCKED_NATIVE_PASS: 44.1k B -> ASRC -> 48k A qualified ChannelId PCM MAIN; loss isolated; exact A-only restore");
}
} // namespace armix::io2_clocked_test
