// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/core/OrbitPhase.h"
#include <cmath>
#include <utility>

// A single, explicit VST3 parameter modulator. Only called/owned by the JUCE
// message thread. Never calls plugin parameter APIs from audio callbacks,
// WebView events or background HighResolutionTimer threads.
class VstOrbitController final : private juce::Timer
{
public:
    ~VstOrbitController() override { stop(); }

    juce::String start (juce::AudioPluginInstance* plugin, int slot, int parameterIndex,
                        double revolutionSeconds, bool reverse)
    {
        if (plugin == nullptr) return "VST3 processor is no longer available";
        if (!std::isfinite (revolutionSeconds) ||
            revolutionSeconds < 2.0 || revolutionSeconds > 180.0)
            return "One revolution must take between 2 and 180 seconds";
        const auto& params = plugin->getParameters();
        if (parameterIndex < 0 || parameterIndex >= params.size())
            return "Invalid VST3 parameter index";
        auto* next = params[parameterIndex];
        if (next == nullptr || !next->isAutomatable() || next->getNumSteps() < 72)
            return "Choose an automatable VST3 parameter with at least 72 resolution steps";

        const float phase = next->getValue();
        if (!std::isfinite (phase) || phase < 0.0f || phase > 1.0f)
            return "Selected VST3 parameter returned an invalid normalized value";

        stop(); // ends gesture on the old processor before replacing it
        processor_ = plugin;
        parameter_ = next;
        slot_ = slot;
        parameterIndex_ = parameterIndex;
        period_ = revolutionSeconds;
        reverse_ = reverse;
        phaseStart_ = phase; // no abrupt jump on START
        startMs_ = juce::Time::getMillisecondCounterHiRes();
        parameter_->beginChangeGesture();
        startTimerHz (60); // control updates only; callback remains untouched
        return {};
    }

    void stop()
    {
        stopTimer();
        auto* oldParameter = std::exchange (parameter_, nullptr);
        if (oldParameter != nullptr)
            oldParameter->endChangeGesture();
        processor_ = nullptr;
        slot_ = -1;
        parameterIndex_ = -1;
    }

    bool active() const noexcept { return parameter_ != nullptr; }
    int slot() const noexcept { return slot_; }
    int parameterIndex() const noexcept { return parameterIndex_; }
    double secondsPerRevolution() const noexcept { return period_; }
    bool reverse() const noexcept { return reverse_; }

private:
    void timerCallback() override
    {
        if (parameter_ == nullptr) return;
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startMs_) / 1000.0;
        const auto normalized = armix::core::orbitRamp (phaseStart_, elapsed, period_, reverse_);
        if (std::isfinite (normalized) && normalized >= 0.0f && normalized <= 1.0f)
            parameter_->setValueNotifyingHost (normalized);
    }

    juce::AudioPluginInstance* processor_ = nullptr; // borrowed, only while slot stable
    juce::AudioProcessorParameter* parameter_ = nullptr; // borrowed, only while slot stable
    int slot_ = -1, parameterIndex_ = -1;
    double period_ = 20.0, startMs_ = 0.0;
    float phaseStart_ = 0.0f;
    bool reverse_ = false;
};
