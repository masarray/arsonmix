// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AudioProfilePolicy.h"
#include <cstdlib>
static void check(bool ok){if(!ok)std::abort();}
int main()
{
    using namespace armix::device;
    EndpointCapabilities shared {"interface-id","driver-v1",StreamApi::asio,
        {44100,48000},{64,128,480,960},2,true};
    auto p=AudioProfilePolicy::choose(shared,shared,{});
    check(p.valid && p.direct && !p.needsClockBridge);
    check(p.renderRate==48000 && p.captureRate==48000 && p.renderPeriod==480);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::lowLatency});
    check(p.valid && p.renderPeriod==64);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,44100,128});
    check(p.valid && p.renderRate==44100 && p.renderPeriod==128);
    p=AudioProfilePolicy::choose(shared,shared,{AudioIntent::advancedManual,96000,128});
    check(!p.valid);
    EndpointCapabilities capture {"virtual-cable-id","virtual-driver",StreamApi::wasapiShared,
        {44100},{441},2,false};
    EndpointCapabilities output {"render-device-id","render-driver",StreamApi::wasapiShared,
        {48000},{128,256,480},2,false};
    p=AudioProfilePolicy::choose(capture,output,{});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);
    p=AudioProfilePolicy::choose(capture,output,{AudioIntent::autoStable,0,0,true});
    check(p.valid && !p.direct && p.needsClockBridge);
    check(p.captureRate==44100 && p.renderRate==48000 && p.renderPeriod==480);
    // A matching friendly name/identifier is NOT proof of one hardware clock.
    capture=output;
    capture.clockSharedByHardware=false;
    p=AudioProfilePolicy::choose(capture,output,{});
    check(!p.valid && p.reason==ProfileReason::unsupportedClockAdapter);
    capture.clockSharedByHardware=true;
    output.clockSharedByHardware=true;
    p=AudioProfilePolicy::choose(capture,output,{});
    check(p.valid && p.direct);
    // Unsupported manual rates/frames do not quietly become an invented profile.
    p=AudioProfilePolicy::choose(capture,output,{AudioIntent::advancedManual,48000,999});
    check(!p.valid);
    return 0;
}
