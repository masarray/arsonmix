// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace armix::device
{
// Control-plane-only capability planner. It neither opens a device nor changes
// the current route. Rates/periods must be reported by the selected backend;
// stable IDs and shared-clock claims require positive platform evidence.
enum class AudioIntent { autoStable, lowLatency, advancedManual };
enum class StreamApi { asio, wasapiShared, wasapiLowLatency, wasapiExclusive };
enum class ProfileReason {
    unavailable, directSameClock, bridgedIndependentClocks,
    unsupportedClockAdapter, changedIdentity, unqualifiedLowLatency,
    explicitExclusiveApprovalRequired
};
struct EndpointCapabilities
{
    std::string stableId;           // verified endpoint ID, not a friendly name
    std::string capabilityFingerprint; // driver+capabilities generation
    StreamApi api = StreamApi::wasapiShared;
    std::vector<int> rates;
    std::vector<int> periods;       // driver-reported output periods in frames
    int activeChannels = 0;
    bool clockSharedByHardware = false; // positive evidence from the adapter
    bool validatedLowLatency = false;   // device/profile-specific physical FAT evidence
};
struct AudioProfile
{
    int captureRate = 0, renderRate = 0, renderPeriod = 0;
    int inputChannels = 0, outputChannels = 0;
    bool direct = false, needsClockBridge = false, valid = false;
    ProfileReason reason = ProfileReason::unavailable;
};
struct ProfileRequest
{
    AudioIntent intent = AudioIntent::autoStable;
    int requestedRate = 0;          // optional, exact when nonzero
    int requestedFrames = 0;        // optional, exact when nonzero
    bool hasIndependentAdapter = false;
    bool exclusiveModeApproved = false; // explicit user action, NEVER inferred
};

class AudioProfilePolicy final
{
public:
    static AudioProfile choose (const EndpointCapabilities& input,
                                const EndpointCapabilities& output,
                                ProfileRequest request)
    {
        AudioProfile p;
        if (input.activeChannels < 1 || output.activeChannels < 1 ||
            input.stableId.empty() || output.stableId.empty() ||
            input.capabilityFingerprint.empty() || output.capabilityFingerprint.empty() ||
            input.rates.empty() || output.rates.empty() ||
            output.periods.empty() || request.requestedRate < 0 ||
            request.requestedFrames < 0) return p;

        // Never silently enable exclusive mode: it may evict other audio apps.
        if (input.api == StreamApi::wasapiExclusive ||
            output.api == StreamApi::wasapiExclusive)
        {
            if (request.intent != AudioIntent::advancedManual || !request.exclusiveModeApproved)
            {
                p.reason = ProfileReason::explicitExclusiveApprovalRequired;
                return p;
            }
        }

        // Identical endpoint IDs but a different driver/API fingerprint mean
        // stale capabilities, not proof of one clock (and not an ASRC escape).
        const bool sameId = input.stableId == output.stableId;
        if (sameId && (input.api != output.api ||
                       input.capabilityFingerprint != output.capabilityFingerprint))
        {
            p.reason = ProfileReason::changedIdentity;
            return p;
        }
        const bool direct = sameId && input.clockSharedByHardware
                         && output.clockSharedByHardware;
        if (!direct)
        {
            // The only currently implemented independent JUCE adapter is
            // WASAPI shared + WASAPI shared, with DISTINCT validated endpoints.
            // Do not pretend an ASIO or exclusive cross-device bridge exists.
            if (!request.hasIndependentAdapter || sameId ||
                input.api != StreamApi::wasapiShared ||
                output.api != StreamApi::wasapiShared)
            {
                p.reason = ProfileReason::unsupportedClockAdapter;
                return p;
            }
        }
        if (request.intent == AudioIntent::lowLatency &&
            (!input.validatedLowLatency || !output.validatedLowLatency))
        {
            p.reason = ProfileReason::unqualifiedLowLatency;
            return p;
        }

        const auto containsRate = [](const std::vector<int>& rates, int rate)
        {
            return rate >= 8000 && rate <= 384000 &&
                std::find (rates.begin(), rates.end(), rate) != rates.end();
        };
        const auto rateFor = [&](const std::vector<int>& rates, bool requireCommon)
        {
            const auto supported = [&](int r) {
                return containsRate(rates, r) &&
                    (!requireCommon || containsRate(input.rates, r));
            };
            if (request.requestedRate != 0)
                return supported(request.requestedRate) ? request.requestedRate : 0;
            for (int r : {48000,44100,96000,88200})
                if (supported(r)) return r;
            // Do not assume the backend reports rates in ascending order.
            int lowest = 0;
            for (int r : rates)
                if (supported(r) && (lowest == 0 || r < lowest)) lowest = r;
            return lowest;
        };
        const int renderRate = rateFor(output.rates, direct);
        const int captureRate = direct ? renderRate :
            (containsRate(input.rates, renderRate) ? renderRate : rateFor(input.rates, false));
        if (renderRate == 0 || captureRate == 0) return p;

        // One scan, no heap copy or sort: AUTO_STABLE chooses the smallest
        // advertised period >= provisional 10ms; else the largest advertised.
        // LOW_LATENCY may pick the minimum only after physical qualification.
        int minimum = 0, largest = 0, conservative = 0;
        const int target = (renderRate + 99) / 100;
        for (int frames : output.periods)
        {
            if (frames <= 0 || frames > 65536) continue;
            if (minimum == 0 || frames < minimum) minimum = frames;
            largest = std::max(largest, frames);
            if (frames >= target &&
                (conservative == 0 || frames < conservative)) conservative = frames;
        }
        if (minimum == 0) return p;
        const int selected = request.requestedFrames != 0 ? request.requestedFrames :
            request.intent == AudioIntent::lowLatency ? minimum :
            conservative != 0 ? conservative : largest;
        if (selected < 1 || selected > 65536 ||
            std::find(output.periods.begin(), output.periods.end(), selected) ==
                output.periods.end()) return p;

        // Publish a complete decision only after all preconditions passed.
        p.captureRate = captureRate;
        p.renderRate = renderRate;
        p.renderPeriod = selected;
        p.inputChannels = input.activeChannels;
        p.outputChannels = output.activeChannels;
        p.direct = direct;
        p.needsClockBridge = !direct;
        p.reason = direct ? ProfileReason::directSameClock
                          : ProfileReason::bridgedIndependentClocks;
        p.valid = true;
        return p;
    }
};
} // namespace armix::device
