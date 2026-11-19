// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DualAsioCaptureProbePolicy.h"
#include "../engine/device/ClockBridgeSizing.h"
#include "io2/QualifiedInputPlanCases.h"
#include "io2/QualifiedInputGraphBridgeCases.h"
#include <cstdlib>
#include <cstdio>
#include <string_view>

using Policy=armix::device::DualAsioCaptureProbePolicy;
using V=Policy::Verdict;
static void check(bool ok)
{
    // Always active in Release/MSVC CTest; never use NDEBUG-stripped assert.
    if (!ok) {std::fputs("IO0_POLICY_FAIL\\n",stderr); std::abort();}
}

int main()
{
    const Policy::Identity a{L"ASIO Vendor A",L"{11111111-1111-1111-1111-111111111111}"};
    const Policy::Identity b{L"ASIO Vendor B",L"{22222222-2222-2222-2222-222222222222}"};
    Policy::Request wanted{L"Vendor A ASIO",L"Vendor B ASIO",a,b,3,3,1};
    Policy::Readback actual{L"Vendor A ASIO",L"Vendor B ASIO",
        48000.,44100.,256,128,3,3,1,0};
    check(Policy::preflight(wanted)==V::ok);
    check(Policy::verify(wanted,actual)==V::ok);
    check(Policy::preflight({L"Vendor A ASIO",L"Vendor A ASIO",a,a})==
        V::sameDriverClass);
    check(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",a,a})==
        V::sameDriverClass);
    check(Policy::preflight({L"",L"Vendor B ASIO",a,b})==
        V::missingSelection);
    check(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",{},b})==
        V::unverifiedPrimaryDriver);
    check(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",a,{}})==
        V::unverifiedCaptureDriver);
    auto changed=actual;
    changed.captureName=L"USB Audio with same channel names";
    check(Policy::verify(wanted,changed)==V::wrongCaptureEndpoint);
    changed=actual;
    changed.primaryName=L"Unexpected output";
    check(Policy::verify(wanted,changed)==V::wrongPrimaryEndpoint);
    changed=actual;
    changed.primaryOutputMask=1;
    check(Policy::verify(wanted,changed)==V::primaryOutputMissing);
    changed=actual;
    changed.primaryInputMask=1;
    check(Policy::verify(wanted,changed)==V::primaryMaskChanged);
    changed=actual;
    changed.captureInputMask=0;
    check(Policy::verify(wanted,changed)==V::captureInputMissing);
    changed=actual;
    changed.captureOutputMask=3;
    check(Policy::verify(wanted,changed)==V::captureOutputUnexpected);
    changed=actual;
    changed.captureInputMask=2;
    check(Policy::verify(wanted,changed)==V::captureMaskChanged);
    changed=actual;
    changed.captureRate=0;
    check(Policy::verify(wanted,changed)==V::invalidFormat);
    changed=actual;
    changed.primaryPeriod=0;
    check(Policy::verify(wanted,changed)==V::invalidFormat);

    // Reuse production ASRC sizing. Different clocks always need a bounded
    // prepared source bridge in IO-1; IO-0 does not insert one into primary.
    for (const double source : {44100.,48000.,96000.})
    for (const double primary : {44100.,48000.,96000.})
    {
        const auto sizing=armix::device::ClockBridgeSizing::choose(
            source,primary,128,256,48,32768,24.);
        check(sizing.valid);
        check(sizing.targetMs>=24.);
        check(sizing.minFrames>0);
    }
    const auto impossible=armix::device::ClockBridgeSizing::choose(
        48000.,48000.,32768,32768,48,32768,24.);
    check(!impossible.valid);
    armix::io2_test::run();
    armix::io2_graph_test::run(); // same registered IO0 native CTest now compiles IO-2
    std::puts("IO0_POLICY_PASS: identity, zero-output capture, exact masks, 9 clock profiles");
    return 0;
}
