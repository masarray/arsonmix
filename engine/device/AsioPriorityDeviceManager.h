// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AsioStartupPolicy.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <string_view>

namespace armix::device
{
// Control-thread factory override, not a new backend or audio worker.
// JUCE enumerates Windows Audio BEFORE ASIO on an unsaved fresh install.
// Reorder only when the ASIO backend exposes one unambiguous full-duplex
// device name; when multiple ASIO interfaces exist leave the native JUCE
// default untouched and require an explicit user's Audio Setup selection.
// Saved DEVICESETUP XML is always authoritative; ordering does not override it.
class AsioPriorityDeviceManager final : public juce::AudioDeviceManager
{
public:
    bool singleAsioPreferred() const noexcept { return singleAsioPreferred_; }

    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        juce::AudioDeviceManager::createAudioDeviceTypes (types);
        singleAsioPreferred_ = false;
#if defined(_WIN32) && JUCE_ASIO
        for (int i = 0; i < types.size(); ++i)
        {
            auto* driver = types.getUnchecked (i);
            if (driver->getTypeName() != "ASIO") continue;
            driver->scanForDevices(); // once at startup, never in RT
            const auto inputNames = driver->getDeviceNames (true);
            const auto outputNames = driver->getDeviceNames (false);
            if (inputNames.size() != 1 || outputNames.size() != 1)
                break;
            const std::array<std::string_view, 1> captures
                {std::string_view (inputNames[0].toRawUTF8())};
            const std::array<std::string_view, 1> renders
                {std::string_view (outputNames[0].toRawUTF8())};
            if (!AsioStartupPolicy::preferAsio (captures, renders)) break;
            // JUCE's initialiseDefault picks the FIRST backend with devices.
            // Move one verified candidate to front BEFORE initialise(), so
            // we don't call setCurrentAudioDeviceType() on a running WASAPI
            // stream (JUCE itself does a 1500ms control-thread sleep there).
            types.move (i, 0);
            singleAsioPreferred_ = true;
            break;
        }
#endif
    }
private:
    bool singleAsioPreferred_ = false;
};
} // namespace armix::device
