// SPDX-License-Identifier: AGPL-3.0-only
// Explicit isolated qualification tool, never installed or invoked by mixer.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File module(juce::String::fromUTF8(argv[1]));
    const juce::File receipt(juce::String::fromUTF8(argv[2]));
    if (!module.isDirectory() || !module.hasFileExtension("vst3") || receipt.exists()) return 2;
    juce::AudioPluginFormatManager formats;
    auto format = std::make_unique<juce::VST3PluginFormat>();
    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile(types, module.getFullPathName());
    formats.addFormat(std::move(format));
    if (types.size() != 1 || types[0]->isInstrument || types[0]->uniqueId == 0) return 3;
    auto& d = *types[0];
    juce::String error;
    auto first = formats.createPluginInstance(d, 48000., 128, error);
    auto second = formats.createPluginInstance(d, 48000., 128, error);
    if (!first || !second) return 4;
    auto prepare = [](juce::AudioPluginInstance& p) {
        auto layout = p.getBusesLayout();
        if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) return false;
        layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
        layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
        if (!p.setBusesLayout(layout)) return false;
        p.setRateAndBufferSizeDetails(48000., 128);
        p.prepareToPlay(48000., 128);
        return true;
    };
    if (!prepare(*first) || !prepare(*second)) return 5;
    juce::MemoryBlock state;
    first->getStateInformation(state);
    if (state.getSize() == 0 || state.getSize() > 64u * 1024u * 1024u) return 6;
    second->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto render = [](juce::AudioPluginInstance& p) {
        juce::AudioBuffer<float> pcm(juce::jmax(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), 128);
        juce::MidiBuffer midi;
        double energy = 0.;
        for (int block = 0; block < 192; ++block) {
            pcm.clear(); midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    pcm.setSample(ch, i, 0.1f * std::sin(float(block * 128 + i) * 0.057595865f));
            p.processBlock(pcm, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i) {
                    const auto v = pcm.getSample(ch, i);
                    if (!std::isfinite(v)) return false;
                    energy += double(v) * v;
                }
        }
        return energy > 1.e-12;
    };
    if (!render(*first) || !render(*second)) return 7;
    const bool hasEditor = first->hasEditor();
    if (hasEditor) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(first->createEditorIfNeeded());
        if (!editor) return 8;
    }
    const auto latency = first->getLatencySamples();
    first->releaseResources(); second->releaseResources();
    first.reset(); second.reset();
    auto row = std::make_unique<juce::DynamicObject>();
    row->setProperty("hostUniqueId", d.uniqueId);
    row->setProperty("name", d.name);
    row->setProperty("version", d.version);
    row->setProperty("latencySamples", latency);
    row->setProperty("stateBytes", static_cast<int>(state.getSize()));
    row->setProperty("editorCreated", hasEditor);
    row->setProperty("scope", "stereo-two-instances-finite-pcm-state-transfer-editor-smoke");
    return receipt.replaceWithText(juce::JSON::toString(juce::var(row.release()))) ? 0 : 9;
}
