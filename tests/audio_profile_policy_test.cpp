// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AudioProfilePolicy.h"
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }

int main()
{
    using namespace armix::device;
    EndpointCapabilities shared{"interface-id","driver-v1",StreamApi::asio,
        {44100,48000},{960,64,480,128,480},2,true};
    auto p=AudioProfilePolicy::choose(shared,shared,{});
    check(p.valid && p.direct && !p.needsClockBridge);
    check(p.renderRate==48000 && p.captureRate==48000 && p.renderPeriod==480);

    // Advertised minimum is NOT proof that 64 frames are clean on hardware.
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::lowLatency});
    check(!p.valid && p.reason==ProfileReason::unqualifiedLowLatency);
    shared.validatedLowLatency=true; // test fixture simulates completed per-device FAT
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::lowLatency});
    check(p.valid && p.renderPeriod==64);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,44100,128});
    check(p.valid && p.renderRate==44100 && p.renderPeriod==128);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,96000,128});
    check(!p.valid);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,44100,999});
    check(!p.valid);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,-1,128});
    check(!p.valid);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,44100,-1});
    check(!p.valid);

    // Changed API or capability generation invalidates a previously shared
    // clock; an ASRC fallback cannot silently bypass stale same-ID evidence.
    auto stale=shared;
    stale.capabilityFingerprint="driver-v2";
    p=AudioProfilePolicy::choose(shared,stale,{});
    check(!p.valid && p.reason==ProfileReason::changedIdentity);
    stale=shared;
    stale.api=StreamApi::wasapiShared;
    p=AudioProfilePolicy::choose(shared,stale,{});
    check(!p.valid && p.reason==ProfileReason::changedIdentity);
    stale=shared;
    stale.clockSharedByHardware=false;
    p=AudioProfilePolicy::choose(shared,stale,{});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);
    stale=shared;
    stale.capabilityFingerprint.clear();
    p=AudioProfilePolicy::choose(shared,stale,{});
    check(!p.valid);

    EndpointCapabilities capture{"virtual-cable-id","virtual-driver",StreamApi::wasapiShared,
        {44100},{441},2,false};
    EndpointCapabilities output{"render-device-id","render-driver",StreamApi::wasapiShared,
        {48000},{128,256,480},2,false};
    p=AudioProfilePolicy::choose(capture,output,{});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);
    p=AudioProfilePolicy::choose(capture,output,{AudioIntent::autoStable,0,0,true});
    check(p.valid && !p.direct && p.needsClockBridge);
    check(p.captureRate==44100 && p.renderRate==48000 && p.renderPeriod==480);
    p=AudioProfilePolicy::choose(capture,output,{AudioIntent::lowLatency,0,0,true});
    check(!p.valid && p.reason==ProfileReason::unqualifiedLowLatency);

    // The native independent adapter supports only distinct WASAPI SHARED endpoints.
    auto asioCapture=capture;
    asioCapture.api=StreamApi::asio;
    p=AudioProfilePolicy::choose(asioCapture,output,{AudioIntent::autoStable,0,0,true});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);

    // Exclusive mode must never appear in AUTO_STABLE, even when advertised.
    auto exclusive=shared;
    exclusive.api=StreamApi::wasapiExclusive;
    p=AudioProfilePolicy::choose(exclusive,exclusive,{});
    check(!p.valid && p.reason==ProfileReason::explicitExclusiveApprovalRequired);
    p=AudioProfilePolicy::choose(exclusive,exclusive,
        {AudioIntent::advancedManual,48000,128,false,false});
    check(!p.valid && p.reason==ProfileReason::explicitExclusiveApprovalRequired);
    p=AudioProfilePolicy::choose(exclusive,exclusive,
        {AudioIntent::advancedManual,48000,128,false,true});
    check(p.valid && p.direct && p.renderPeriod==128);

    // An untrusted matching name/ID without positive hardware clock proof
    // cannot become DIRECT by default or via a same-endpoint fake bridge.
    capture=output;
    capture.clockSharedByHardware=false;
    p=AudioProfilePolicy::choose(capture,output,{AudioIntent::autoStable,0,0,true});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);
    capture.clockSharedByHardware=true;
    output.clockSharedByHardware=true;
    p=AudioProfilePolicy::choose(capture,output,{});
    check(p.valid && p.direct);

    // Deterministic fallback independent of driver enumeration order.
    output.periods={256,64,128};
    p=AudioProfilePolicy::choose(capture,output,{});
    check(p.valid && p.renderPeriod==256);
    output.periods={-2,0};
    p=AudioProfilePolicy::choose(capture,output,{});
    check(!p.valid);
    return 0;
}
