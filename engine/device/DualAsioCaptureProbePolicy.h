// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AsioRegistrationPolicy.h"
#include <cmath>
#include <cstdint>
#include <string_view>

namespace armix::device
{
// IO-0, CONTROL-only: conservative compatibility probe policy, NOT a
// production source binding or a claim that a registered class can open.
// Distinct ASIO driver classes are necessary but NOT sufficient evidence of
// two independently usable physical interfaces.
class DualAsioCaptureProbePolicy final
{
public:
    using Identity = AsioRegistrationPolicy::Identity;
    enum class Verdict : std::uint8_t
    {
        ok, missingSelection, unverifiedPrimaryDriver, unverifiedCaptureDriver,
        sameDriverClass, wrongPrimaryEndpoint, wrongCaptureEndpoint,
        invalidFormat, primaryOutputMissing, captureInputMissing,
        captureOutputUnexpected, primaryMaskChanged, captureMaskChanged
    };

    struct Request
    {
        std::wstring_view primaryName, captureName;
        Identity primary, capture;
        std::uint64_t primaryInputMask = 0x3;
        std::uint64_t primaryOutputMask = 0x3;
        std::uint64_t captureInputMask = 0x1;
    };
    struct Readback
    {
        std::wstring_view primaryName, captureName;
        double primaryRate = 0., captureRate = 0.;
        int primaryPeriod = 0, capturePeriod = 0;
        std::uint64_t primaryInputMask = 0, primaryOutputMask = 0;
        std::uint64_t captureInputMask = 0, captureOutputMask = 0;
    };

    // Two post-open observation windows are mandatory. Historical >0
    // counters alone incorrectly passed when A stopped as B was opened.
    struct CallbackProgress final
    {
        std::uint64_t primaryAtCaptureStart=0,captureAtCaptureStart=0;
        std::uint64_t primaryMid=0,captureMid=0;
        std::uint64_t primaryEnd=0,captureEnd=0;
    };

    static bool continuousAfterCaptureStart(const CallbackProgress& p) noexcept
    {
        return p.primaryMid>p.primaryAtCaptureStart &&
               p.captureMid>p.captureAtCaptureStart &&
               p.primaryEnd>p.primaryMid &&
               p.captureEnd>p.captureMid;
    }

    static Verdict preflight(const Request& r) noexcept
    {
        if (r.primaryName.empty() || r.captureName.empty())
            return Verdict::missingSelection;
        if (!r.primary.valid()) return Verdict::unverifiedPrimaryDriver;
        if (!r.capture.valid()) return Verdict::unverifiedCaptureDriver;
        if (r.primary.sameDriverClass(r.capture) ||
            r.primaryName == r.captureName)
            return Verdict::sameDriverClass;
        if (r.primaryOutputMask == 0 || r.captureInputMask == 0)
            return Verdict::invalidFormat;
        return Verdict::ok;
    }

    static Verdict verify(const Request& r, const Readback& a) noexcept
    {
        const auto ready=preflight(r);
        if (ready!=Verdict::ok) return ready;
        if (a.primaryName!=r.primaryName) return Verdict::wrongPrimaryEndpoint;
        if (a.captureName!=r.captureName) return Verdict::wrongCaptureEndpoint;
        if (!std::isfinite(a.primaryRate)||!std::isfinite(a.captureRate)||
            a.primaryRate<8000.||a.captureRate<8000.||
            a.primaryRate>384000.||a.captureRate>384000.||
            a.primaryPeriod<=0||a.capturePeriod<=0)
            return Verdict::invalidFormat;
        if ((a.primaryOutputMask & 0x3u)!=0x3u)
            return Verdict::primaryOutputMissing;
        if (a.captureInputMask==0) return Verdict::captureInputMissing;
        if (a.captureOutputMask!=0) return Verdict::captureOutputUnexpected;
        if (a.primaryInputMask!=r.primaryInputMask||
            a.primaryOutputMask!=r.primaryOutputMask)
            return Verdict::primaryMaskChanged;
        if (a.captureInputMask!=r.captureInputMask)
            return Verdict::captureMaskChanged;
        return Verdict::ok;
    }

    static const char* reason(Verdict v) noexcept
    {
        switch(v)
        {
            case Verdict::ok: return "IO0_EXACT_ASIO_READBACK";
            case Verdict::missingSelection: return "IO0_MISSING_EXPLICIT_DRIVER";
            case Verdict::unverifiedPrimaryDriver: return "IO0_PRIMARY_CLASS_NOT_ATTESTED";
            case Verdict::unverifiedCaptureDriver: return "IO0_CAPTURE_CLASS_NOT_ATTESTED";
            case Verdict::sameDriverClass: return "IO0_REQUIRES_DISTINCT_ASIO_CLASSES";
            case Verdict::wrongPrimaryEndpoint: return "IO0_PRIMARY_NAME_SUBSTITUTED";
            case Verdict::wrongCaptureEndpoint: return "IO0_CAPTURE_NAME_SUBSTITUTED";
            case Verdict::invalidFormat: return "IO0_NEGOTIATED_FORMAT_INVALID";
            case Verdict::primaryOutputMissing: return "IO0_PRIMARY_OUTPUT_STEREO_MISSING";
            case Verdict::captureInputMissing: return "IO0_CAPTURE_INPUT_MISSING";
            case Verdict::captureOutputUnexpected: return "IO0_SECONDARY_CAPTURE_MUST_HAVE_ZERO_OUTPUTS";
            case Verdict::primaryMaskChanged: return "IO0_PRIMARY_PIN_MASK_CHANGED";
            case Verdict::captureMaskChanged: return "IO0_CAPTURE_PIN_MASK_CHANGED";
        }
        return "IO0_UNKNOWN_FAILURE";
    }
};
} // namespace armix::device
