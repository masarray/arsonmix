// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../engine/device/QualifiedSecondaryCaptureReader.h"
#include "../../engine/device/QualifiedCaptureHostMode.h"
#include "../../engine/device/QualifiedCaptureAdmission.h"
#include "../../engine/device/QualifiedInputGraphBridge.h"
#include "../../engine/device/QualifiedPrimaryRestartRecoveryPlan.h"
#include "../../engine/core/DynamicNativeSessionPublisher.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

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
    // SOURCE NONE and Delete Track must remove only the named ChannelId
    // binding while preserving A/B device slots, generation and lease.
    d::MultiDeviceInputPlan reduced;
    check(plan.withoutSource(b,reduced) && reduced.sourceCount()==1 &&
          plan.sourceCount()==2 && reduced.virtualInputCount()==plan.virtualInputCount() &&
          reduced.device(1).deviceLease==plan.device(1).deviceLease &&
          reduced.source(0).channel==a,
          "detached qualified source removal never drops B's device lease");
    check(!plan.withoutSource(main,reduced),
          "deleting an unbound MAIN cannot mutate the qualified input map");
    m::DynamicGraphRequest old;old.physicalInputs=1;old.maxFrames=frames;
    old.sources={{a,0,-1,false}};
    m::DynamicGraphRequest request;
    d::QualifiedInputGraphBridge::Fault bridgeError{};
    check(d::QualifiedInputGraphBridge::stage(plan,old,request,bridgeError),
        "control maps both verified sources into same graph");
    m::DynamicGraphRequest primaryFallback;
    check(d::QualifiedPrimaryRestartRecoveryPlan::stage(
              plan,request,1,primaryFallback) &&
          primaryFallback.physicalInputs==1 &&
          primaryFallback.sources.size()==1 &&
          primaryFallback.sources.front().id==a &&
          primaryFallback.sources.front().inputL==0 &&
          request.physicalInputs==3 && request.sources.size()==2,
          "ASIO restart leaves B ChannelId/FX/WIRE roster and silences only B source");
    check(!d::QualifiedPrimaryRestartRecoveryPlan::stage(
              plan,request,2,primaryFallback),
          "different A input mask is never accepted by stale B virtual slot map");
    auto swapped=request;
    swapped.sources.back().inputL=0;
    check(!d::QualifiedPrimaryRestartRecoveryPlan::stage(
              plan,swapped,1,primaryFallback),
          "wrong-device B ordinal cannot relabel existing A source during restart");
    m::DynamicNativeSessionPublisher recoveredPcm;
    check(recoveredPcm.commit(roster,primaryFallback,0,epoch+1,epoch+1,true).ok() &&
          !recoveredPcm.hasQualifiedInputMap(epoch+1,1),
          "a new primary epoch publishes only verified A PCM but keeps original roster");
    check(recoveredPcm.commit(roster,primaryFallback,1,epoch+2,epoch+2,true).ok() &&
          !recoveredPcm.hasQualifiedInputMap(epoch+2,2),
          "second A restart rebinds the recovered A-only graph without stale B map");
    // IO-2F caller-path regression: the REAL retained owner has an A+B
    // input map on the previous epoch. Merely staging the safe A-only
    // document is insufficient; ASIO restart must explicitly pass the map
    // retirement token all the way through restoreNativeMixerSession().
    // The preceding A-only fixture never had B installed and missed P1.
    m::DynamicNativeSessionPublisher restarted;
    using Change=m::DynamicNativeSessionPublisher::QualifiedMapChange;
    using Fault=m::DynamicNativeSessionPublisher::Fault;
    check(restarted.commit(roster,request,0,epoch,epoch,true,
          {},false,false,&plan).ok() &&
          restarted.hasQualifiedInputMap(epoch,1),
          "actual A+B PCM installed before driver restart");
    check(restarted.commit(roster,primaryFallback,1,epoch+1,epoch+1,true).fault==
          Fault::invalidCandidate &&
          restarted.acceptedRevision()==1 &&
          restarted.hasQualifiedInputMap(epoch,1),
          "P1 regression reproduction: restart without explicit retirement fails closed");
    check(restarted.commit(roster,primaryFallback,1,epoch+1,epoch+2,true,
          {},false,false,nullptr,Change::replaceWithPrimaryOnly).fault==
          Fault::staleDevice &&
          restarted.hasQualifiedInputMap(epoch,1),
          "mismatched live A epoch must preserve old B lease on rejected recovery");
    check(restarted.commit(roster,primaryFallback,1,epoch+1,epoch+1,false,
          {},false,false,nullptr,Change::replaceWithPrimaryOnly).fault==
          Fault::outputArmed &&
          restarted.hasQualifiedInputMap(epoch,1),
          "ARM cannot pass the explicit map retirement boundary");
    check(restarted.commit(roster,primaryFallback,1,epoch+1,epoch+1,true,
          {},false,false,nullptr,Change::replaceWithPrimaryOnly).ok() &&
          restarted.acceptedRevision()==2 &&
          !restarted.hasQualifiedInputMap(epoch+1,2) &&
          !restarted.hasQualifiedInputMap(epoch,1),
          "verified A-only restart atomically retires old B map");
    const auto* retainedRouting=restarted.acceptedRouting(epoch+1,2);
    check(retainedRouting && retainedRouting->find(a) &&
          retainedRouting->find(b) && retainedRouting->find(main),
          "A, silent B, MAIN ChannelId roster survives ASIO restart");
    check(restarted.commit(roster,primaryFallback,2,epoch+2,epoch+2,true,
          {},false,false,nullptr,Change::replaceWithPrimaryOnly).fault==
          Fault::invalidCandidate,
          "second restart must not retire nonexistent B mapping again");
    check(restarted.commit(roster,primaryFallback,2,epoch+2,epoch+2,true).ok() &&
          !restarted.hasQualifiedInputMap(epoch+2,3) &&
          restarted.acceptedRouting(epoch+2,3)->find(b),
          "second ASIO restart keeps original B track silent but session usable");
    m::DynamicNativeSessionPublisher native;
    check(native.commit(roster,request,0,epoch,epoch,true,
        {},false,false,&plan).ok(),"one PCM and device map publication");
    check(native.hasQualifiedInputMap(epoch,1),
          "v1 .smix cannot safely save virtual A+B input ordinals");
    check(!native.hasQualifiedInputMap(epoch+1,1)&&
          !native.hasQualifiedInputMap(epoch,0),
          "stale graph generation cannot authorize persistence");
    // A plain SOURCE NONE/Delete Track request is rejected if the old map
    // remains unchanged. An explicit DETACHED subset is accepted atomically
    // and stays qualified for the remaining A source.
    auto subsetRequest=request;
    std::erase_if(subsetRequest.sources,[b](const auto& row){return row.id==b;});
    m::DynamicNativeSessionPublisher subsetPcm;
    check(subsetPcm.commit(roster,request,0,epoch,epoch,true,
          {},false,false,&plan).ok(),"subset fixture starts with A+B map");
    check(subsetPcm.commit(roster,subsetRequest,1,epoch,epoch,true).fault==
              m::DynamicNativeSessionPublisher::Fault::invalidCandidate &&
          subsetPcm.acceptedRevision()==1,
          "implicit SOURCE NONE must not silently desynchronize old B map");
    check(subsetPcm.commit(roster,subsetRequest,1,epoch,epoch,true,
          {},false,false,&reduced).ok() &&
          subsetPcm.hasQualifiedInputMap(epoch,2),
          "explicit source subset publishes and retains exact device leases");


    d::SecondaryCaptureClockStream clock;
    const d::SecondaryCaptureClockStream::Format format{
        44100.,48000.,frames,frames,2,captureGeneration,epoch,2002};
    check(clock.prepare(format)&&clock.activate(),"independent 44.1k B into 48k A ASRC ready");
    using Admission=d::QualifiedCaptureAdmission;
    check(Admission::verify(plan,clock,epoch,1,frames)==Admission::Fault::none,
          "exact two-device qualified CONTROL admission");
    check(Admission::verify(plan,clock,epoch,1,frames/2)==Admission::Fault::none,
          "hardware ASIO period may exceed retained renderer chunk quantum");
    check(Admission::verify(plan,clock,epoch,1,8193)==Admission::Fault::unsafeFrames,
          "reader scratch cannot exceed absolute callback hard frame limit");
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
    // IO-2G P1: CONTROL must close callback admission BEFORE reading
    // count/freeing scratch. In-flight render keeps the same exact borrowed
    // B pointer pinned; a concurrent late primary callback fails immediately.
    check(reader.prepare(plan,frames),"race fixture uses verified A+B map");
    std::atomic<bool> entered{false},release{false},stopped{false};
    std::thread admitted([&] {
        const bool ok=reader.withBlocks(epoch,aInput,1,frames,clock,
            [&](std::span<const d::MultiDeviceInputPlan::CaptureBlock>) noexcept {
                entered.store(true,std::memory_order_release);
                while(!release.load(std::memory_order_acquire))
                    std::this_thread::yield(); // test-only; never production RT
                return true;
            });
        check(ok,"previously admitted callback completed with scratch intact");
    });
    const auto deadline=std::chrono::steady_clock::now()+
        std::chrono::seconds(5);
    while(!entered.load(std::memory_order_acquire) &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    check(entered.load(std::memory_order_acquire),
          "must enter withBlocks before CONTROL stop");
    std::thread control([&] {
        reader.stop(); // must wait for the admitted borrowed pointer
        stopped.store(true,std::memory_order_release);
    });
    while(reader.status().ready &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::yield();
    check(!reader.status().ready &&
          !stopped.load(std::memory_order_acquire),
          "writer bit closes new admissions before scratch reclamation");
    check(!reader.withBlocks(epoch,aInput,1,frames,clock,
        [](std::span<const d::MultiDeviceInputPlan::CaptureBlock>) noexcept {
            return true;
        }),"late primary callback rejects without borrowing freed scratch");
    release.store(true,std::memory_order_release);
    admitted.join();control.join();
    check(stopped.load(std::memory_order_acquire) && !reader.status().ready,
          "CONTROL retirement finishes only after old callback exits");

    // ASan/TSan-friendly concurrent entry vs STOP/reprepare stress.
    // The capture clock is stable; only reader scratch is being retired.
    check(reader.prepare(plan,frames),"reader can safely reprepare after STOP");
    std::atomic<bool> running{true};
    std::thread primary([&] {
        while(running.load(std::memory_order_acquire))
            (void)reader.withBlocks(epoch,aInput,1,frames,clock,
                [](std::span<const d::MultiDeviceInputPlan::CaptureBlock>)
                    noexcept {return true;});
    });
    for(int i=0;i<1200;++i) {
        reader.stop();
        check(reader.prepare(plan,frames),"CONTROL reprepare gate stress");
    }
    running.store(false,std::memory_order_release);
    primary.join();
    reader.stop();
    clock.stop();

    // IO-2H P1: B capture closes/resets its ASRC while the primary render
    // callback is still consuming B. Exactly the unsafe sequence on an
    // intentional disconnect. No global A reader shutdown is allowed:
    // qualified A+B stays installed; B must become SILENT and A stays live.
    check(clock.prepare(format)&&clock.activate(),
          "fresh B clock is prepared for independent disconnect race");
    check(reader.prepare(plan,frames),"same native A+B reader stays installed");
    std::atomic<bool> keepRendering{true};
    std::atomic<unsigned> validPrimary{0},aOnly{0};
    std::thread aCallback([&] {
        while(keepRendering.load(std::memory_order_acquire)) {
            if(reader.withBlocks(epoch,aInput,1,frames,clock,
               [&](std::span<const d::MultiDeviceInputPlan::CaptureBlock> blocks)
                    noexcept {
                   const bool ok=native.render(epoch,nullptr,0,
                       left.data(),right.data(),frames,false,fx,
                       nullptr,nullptr,{},{},{},nullptr,0,blocks);
                   if(ok) {
                       validPrimary.fetch_add(1,std::memory_order_relaxed);
                       if(near(left[frames-1],.2f*monoCenter,0.005f))
                           aOnly.fetch_add(1,std::memory_order_relaxed);
                   }
                   return ok;
               })){} // test only: single primary callback
        }
    });
    for(int i=0;i<160;++i) {
        clock.stop(); // CONTROL must wait for both admitted clock callbacks
        if(i!=159)
            check(clock.prepare(format)&&clock.activate(),
                  "B ASRC reprepare with competing A pulls never races reset");
    }
    keepRendering.store(false,std::memory_order_release);
    aCallback.join();
    check(validPrimary.load(std::memory_order_relaxed)>0 &&
          aOnly.load(std::memory_order_relaxed)>0,
          "primary ChannelId MAIN and A-only PCM survive competing B disconnect");
    // After B has fully disconnected, the same qualified reader must still
    // invoke the ONE A graph and silence only the missing B source.
    check(reader.withBlocks(epoch,aInput,1,frames,clock,
       [&](std::span<const d::MultiDeviceInputPlan::CaptureBlock> blocks)
           noexcept {
           return native.render(epoch,nullptr,0,left.data(),right.data(),
               frames,false,fx,nullptr,nullptr,{},{},{},nullptr,0,blocks);
       }) && near(left[frames-1],.2f*monoCenter,0.005f) &&
           near(right[frames-1],.2f*monoCenter,0.005f),
          "B absent never mutes or aliases direct primary A PCM");
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
