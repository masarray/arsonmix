// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace armix::device
{
// Pure capability-based profile planning. Runs on control thread only.
// No device-name heuristics, probing, COM or audio-thread work.
enum class AudioIntent { autoStable, lowLatency, advancedManual };
enum class StreamApi { asio, wasapiShared, wasapiLowLatency, wasapiExclusive };
enum class ProfileReason {
    unavailable, directSameClock, bridgedIndependentClocks, unsupportedClockAdapter
};
struct EndpointCapabilities
{
    std::string stableId;           // verified ID, not a friendly name
    std::string capabilityFingerprint;
    StreamApi api = StreamApi::wasapiShared;
    std::vector<int> rates;
    std::vector<int> periods;       // physical supported output periods in frames
    int activeChannels = 0;
    bool clockSharedByHardware = false; // evidence must be confirmed by adapter
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
    int requestedRate = 0;          // optional; nonzero manual exact request
    int requestedFrames = 0;
    bool hasIndependentAdapter = false;
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
            input.rates.empty() || output.rates.empty() ||
            output.periods.empty()) return p;

        // Only positive backend evidence can prove the two stream clocks are one.
        const bool sharedClock = input.stableId == output.stableId
                              && input.clockSharedByHardware
                              && output.clockSharedByHardware;
        p.direct = sharedClock;
        p.needsClockBridge = !sharedClock;
        if (!sharedClock && !request.hasIndependentAdapter)
        {
            p.reason = ProfileReason::unsupportedClockAdapter;
            return p;
        }

        const auto available = [](const std::vector<int>& v, int rate)
        { return std::find (v.begin(), v.end(), rate) != v.end(); };
        const auto chooseRate = [&](const std::vector<int>& rates, bool requireCommon) -> int
        {
            if (request.requestedRate != 0)
                return available (rates, request.requestedRate) &&
                       (!requireCommon || available (input.rates, request.requestedRate))
                        ? request.requestedRate : 0;
            // Windows streaming defaults to 48k ONLY if endpoints actually support it.
            for (const int rate : {48000, 44100, 96000, 88200})
                if (available (rates, rate) &&
                    (!requireCommon || available (input.rates, rate))) return rate;
            for (auto rate : rates)
                if (rate > 0 && (!requireCommon || available (input.rates, rate))) return rate;
            return 0;
        };

        p.renderRate = chooseRate (output.rates, sharedClock);
        if (p.renderRate == 0) return p;
        p.captureRate = sharedClock ? p.renderRate : (
            available (input.rates, p.renderRate) ? p.renderRate :
            choosePreferredCaptureRate (input.rates));
        if (p.captureRate == 0) return p;
        if (request.requestedFrames != 0)
        {
            if (std::find (output.periods.begin(), output.periods.end(),
                           request.requestedFrames) == output.periods.end()) return p;
            p.renderPeriod = request.requestedFrames;
        }
        else
        {
            // AUTO_STABLE: conservative supported candidate >= 10 ms if present.
            // This 10 ms threshold is a provisional laboratory policy,
            // not a physical guarantee of clean sound.
            std::vector<int> valid;
            for (auto frames : output.periods)
                if (frames > 0) valid.push_back (frames);
            if (valid.empty()) return p;
            std::sort (valid.begin(), valid.end());
            if (request.intent == AudioIntent::lowLatency)
                p.renderPeriod = valid.front();
            else
            {
                const int target = (p.renderRate + 99) / 100;
                const auto it = std::lower_bound (valid.begin(), valid.end(), target);
                p.renderPeriod = it == valid.end() ? valid.back() : *it;
            }
        }
        p.inputChannels = input.activeChannels;
        p.outputChannels = output.activeChannels;
        p.reason = sharedClock ? ProfileReason::directSameClock
                               : ProfileReason::bridgedIndependentClocks;
        p.valid = true;
        return p;
    }
private:
    static int choosePreferredCaptureRate (const std::vector<int>& available)
    {
        for (auto r : {48000, 44100, 96000, 88200})
            if (std::find (available.begin(), available.end(), r) != available.end())
                return r;
        for (auto r : available) if (r > 0) return r;
        return 0;
    }
};
} // namespace armix::device
