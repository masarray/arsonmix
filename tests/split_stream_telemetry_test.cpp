// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/SplitStreamTelemetry.h"
#include <cstdlib>
static void check(bool ok){if(!ok)std::abort();}
int main()
{
    armix::device::SplitStreamTelemetry d;
    d.sourceStarted();
    d.renderStarted();
    d.captured(256,250);
    d.captured(256,256);
    d.rendered(128,4);
    d.rendered(128,0);
    d.rendered(-1,0);
    d.captured(10,11);
    const auto s=d.snapshot();
    check(s.sourceGeneration==1 && s.renderGeneration==1);
    check(s.captureCallbacks==3 && s.renderCallbacks==3);
    check(s.captureFrames==506 && s.captureDropFrames==6);
    check(s.renderFrames==256 && s.renderMissingFrames==4);
    check(s.invalidBufferCallbacks==2);
    return 0;
}
