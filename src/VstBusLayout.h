// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Non-RT only. The host processes a 2-channel scratch block. Keep the
// plugin's already-compatible main stereo buses unchanged, rather than
// unconditionally forcing a new layout (which can fail for VST3s with
// optional sidechain/auxiliary buses).
namespace armix::vst
{
inline juce::String describeBuses (const juce::AudioProcessor::BusesLayout& layout)
{
    juce::String text;
    const auto describe = [] (const juce::Array<juce::AudioChannelSet>& buses)
    {
        juce::StringArray names;
        for (const auto& bus : buses)
            names.add (juce::String (bus.size()) + "ch");
        return names.joinIntoString (",");
    };
    return "inputs [" + describe (layout.inputBuses) + "], outputs [" +
        describe (layout.outputBuses) + "]";
}

inline juce::String prepareStereoEffectLayout (juce::AudioProcessor& processor)
{
    if (processor.getBusCount (true) < 1 || processor.getBusCount (false) < 1)
        return "VST3 has no main input or output bus";

    auto original = processor.getBusesLayout();
    if (original.inputBuses.isEmpty() || original.outputBuses.isEmpty())
        return "VST3 reports missing main bus layout";
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();
    const auto compatible = [&stereo, &disabled] (const juce::AudioProcessor::BusesLayout& l)
    {
        if (l.inputBuses.isEmpty() || l.outputBuses.isEmpty() ||
            l.inputBuses[0] != stereo || l.outputBuses[0] != stereo) return false;
        for (int i = 1; i < l.inputBuses.size(); ++i)
            if (l.inputBuses[i] != disabled) return false;
        for (int i = 1; i < l.outputBuses.size(); ++i)
            if (l.outputBuses[i] != disabled) return false;
        return true;
    };

    // Working defaults (common in FabFilter and other commercial VST3s)
    // must not be reset just because the host wants stereo.
    if (compatible (original)) return {};

    auto requested = original;
    requested.inputBuses.set (0, stereo);
    requested.outputBuses.set (0, stereo);
    for (int i = 1; i < requested.inputBuses.size(); ++i)
        requested.inputBuses.set (i, disabled);
    for (int i = 1; i < requested.outputBuses.size(); ++i)
        requested.outputBuses.set (i, disabled);

    if (processor.setBusesLayout (requested) &&
        compatible (processor.getBusesLayout())) return {};

    return "Stereo main bus could not be negotiated (existing " +
        describeBuses (original) + "). This host requires stereo in/out with optional sidechains disabled.";
}
} // namespace armix::vst
