// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>

// First-run selection is a deterministic, read-only CONTROL-plane policy.
// The Windows MMDevice/JUCE adapter must supply VERIFIED classification,
// stable IDs, a unique name-to-native-ID mapping and stereo capability.
// Nothing here opens devices, arms MAIN, writes Windows defaults or runs on
// the realtime callback. No profile => never invent an input source.
namespace armix::device
{
class FirstRunEndpointPolicy final
{
public:
    enum class Direction { capture, render };
    enum class Kind { standardVbCable, physical, otherVirtual, unknown };
    enum class Outcome {
        preserveSavedProfile, cableToSpeakers, playbackOnly,
        chooseSpeakers, chooseCapture, noVerifiedPlayback, invalidInventory
    };
    struct Endpoint
    {
        std::wstring id, name;
        Direction direction = Direction::capture;
        Kind kind = Kind::unknown;
        bool active = false;
        bool verifiedIdentity = false;
        bool uniqueNativeName = false;
        bool stereo = false;
        bool currentDefaultRender = false;
        bool preInstallDefaultRender = false;
    };
    struct Decision
    {
        Decision() = default;
        // Non-aggregate constructor avoids C++20 -Wmissing-field-initializers
        // while value-initializing all identity fields to empty.
        Decision (Outcome status) noexcept : outcome (status) {}
        Outcome outcome = Outcome::noVerifiedPlayback;
        std::wstring captureId, renderId;
        std::wstring captureName, renderName;
        bool hasCapture() const noexcept { return !captureId.empty(); }
        bool hasPlayback() const noexcept { return !renderId.empty(); }
        bool mayAttemptApply() const noexcept
        {
            return (outcome == Outcome::cableToSpeakers ||
                    outcome == Outcome::playbackOnly) && hasPlayback();
        }
    };

    static Decision choose (std::span<const Endpoint> endpoints,
                            bool savedExplicitProfile)
    {
        // The first-run chooser is NOT a device recovery policy; only a
        // distinct, explicit UI transaction may change a remembered ASIO path.
        if (savedExplicitProfile) return { Outcome::preserveSavedProfile };

        // Reject duplicate or missing stable IDs and names in *active*
        // inventory. Calling JUCE by a duplicate name can open the wrong card.
        for (std::size_t i = 0; i < endpoints.size(); ++i)
        {
            const auto& ep = endpoints[i];
            if (!ep.active) continue;
            if (ep.id.empty() || ep.name.empty())
                return { Outcome::invalidInventory };
            for (std::size_t j = i + 1; j < endpoints.size(); ++j)
            {
                const auto& other = endpoints[j];
                if (other.active && (ep.id == other.id ||
                    (ep.direction == other.direction && ep.name == other.name)))
                    return { Outcome::invalidInventory };
            }
        }

        std::vector<const Endpoint*> speakers;
        std::vector<const Endpoint*> cables;
        for (const auto& ep : endpoints)
        {
            if (!ep.active || !ep.verifiedIdentity || !ep.uniqueNativeName)
                continue; // never fall back to friendly-name guessing
            if (ep.direction == Direction::render && ep.stereo &&
                ep.kind == Kind::physical)
                speakers.push_back (&ep);
            else if (ep.direction == Direction::capture && ep.stereo &&
                     ep.kind == Kind::standardVbCable)
                cables.push_back (&ep);
        }
        if (speakers.empty()) return { Outcome::noVerifiedPlayback };

        // Ambiguous default metadata must never silently downgrade to a
        // different default or whichever device was listed first. A single
        // pre-install speaker is more trustworthy after VB-CABLE installation,
        // because Windows may replace the current default with CABLE Input.
        const Endpoint* historical = nullptr;
        const Endpoint* current = nullptr;
        std::size_t historicalCount = 0, currentCount = 0;
        for (const auto* candidate : speakers)
        {
            if (candidate->preInstallDefaultRender)
            {
                historical = candidate;
                ++historicalCount;
            }
            if (candidate->currentDefaultRender)
            {
                current = candidate;
                ++currentCount;
            }
        }
        if (historicalCount > 1 || (historicalCount == 0 && currentCount > 1))
            return { Outcome::chooseSpeakers };
        const Endpoint* render = historicalCount == 1 ? historical :
                                 currentCount == 1 ? current :
                                 speakers.size() == 1 ? speakers.front() : nullptr;
        if (!render) return { Outcome::chooseSpeakers };

        Decision result;
        result.renderId = render->id;
        result.renderName = render->name;
        if (cables.empty())
        {
            // An output-only session may play a real Player track; it must
            // never report live capture or claim the missing cable is active.
            result.outcome = Outcome::playbackOnly;
            return result;
        }
        if (cables.size() != 1)
        {
            result.outcome = Outcome::chooseCapture;
            return result; // preserve known speaker, no arbitrary capture
        }
        const auto* capture = cables.front();
        if (capture->id == render->id || capture->name == render->name)
        {
            result.outcome = Outcome::invalidInventory;
            result.renderId.clear();
            result.renderName.clear();
            return result;
        }
        result.captureId = capture->id;
        result.captureName = capture->name;
        result.outcome = Outcome::cableToSpeakers;
        return result;
    }
};
} // namespace armix::device
