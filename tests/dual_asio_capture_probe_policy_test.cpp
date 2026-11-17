// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DualAsioCaptureProbePolicy.h"
#include "../engine/device/ClockBridgeSizing.h"
#include <cassert>
#include <cstdio>
#include <string_view>

using Policy=armix::device::DualAsioCaptureProbePolicy;
using V=Policy::Verdict;

int main()
{
    const Policy::Identity a{L"ASIO Vendor A",L"{11111111-1111-1111-1111-111111111111}"};
    const Policy::Identity b{L"ASIO Vendor B",L"{22222222-2222-2222-2222-222222222222}"};
    Policy::Request wanted{L"Vendor A ASIO",L"Vendor B ASIO",a,b,3,3,1};
    Policy::Readback actual{L"Vendor A ASIO",L"Vendor B ASIO",
        48000.,44100.,256,128,3,3,1,0};
    assert(Policy::preflight(wanted)==V::ok);
    assert(Policy::verify(wanted,actual)==V::ok);
    assert(Policy::preflight({L"Vendor A ASIO",L"Vendor A ASIO",a,a})==
        V::sameDriverClass);
    assert(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",a,a})==
        V::sameDriverClass);
    assert(Policy::preflight({L"",L"Vendor B ASIO",a,b})==
        V::missingSelection);
    assert(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",{},b})==
        V::unverifiedPrimaryDriver);
    assert(Policy::preflight({L"Vendor A ASIO",L"Vendor B ASIO",a,{}})==
        V::unverifiedCaptureDriver);
    auto changed=actual;
    changed.captureName=L"USB Audio with same channel names";
    assert(Policy::verify(wanted,changed)==V::wrongCaptureEndpoint);
    changed=actual;
    changed.primaryName=L"Unexpected output";
    assert(Policy::verify(wanted,changed)==V::wrongPrimaryEndpoint);
    changed=actual;
    changed.primaryOutputMask=1;
    assert(Policy::verify(wanted,changed)==V::primaryOutputMissing);
    changed=actual;
    changed.primaryInputMask=1;
    assert(Policy::verify(wanted,changed)==V::primaryMaskChanged);
    changed=actual;
    changed.captureInputMask=0;
    assert(Policy::verify(wanted,changed)==V::captureInputMissing);
    changed=actual;
    changed.captureOutputMask=3;
    assert(Policy::verify(wanted,changed)==V::captureOutputUnexpected);
    changed=actual;
    changed.captureInputMask=2;
    assert(Policy::verify(wanted,changed)==V::captureMaskChanged);
    changed=actual;
    changed.captureRate=0;
    assert(Policy::verify(wanted,changed)==V::invalidFormat);
    changed=actual;
    changed.primaryPeriod=0;
    assert(Policy::verify(wanted,changed)==V::invalidFormat);

    // Reuse production ASRC sizing. Different clocks always need a bounded
    // prepared source bridge in IO-1; IO-0 does not insert one into primary.
    for (const double source : {44100.,48000.,96000.})
    for (const double primary : {44100.,48000.,96000.})
    {
        const auto sizing=armix::device::ClockBridgeSizing::choose(
            source,primary,128,256,48,32768,24.);
        assert(sizing.valid);
        assert(sizing.targetMs>=24.);
        assert(sizing.minFrames>0);
    }
    const auto impossible=armix::device::ClockBridgeSizing::choose(
        48000.,48000.,32768,32768,48,32768,24.);
    assert(!impossible.valid);
    std::puts("IO0_POLICY_PASS: identity, zero-output capture, exact masks, 9 clock profiles");
    return 0;
}
