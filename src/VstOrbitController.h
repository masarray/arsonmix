// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/core/OrbitPhase.h"
#include "../engine/core/OrbitControlPolicy.h"
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
        lastPublished_ = phase;
        parameterSteps_ = next->getNumSteps();
        dispatch_ = {};
        previousTickMs_ = startMs_;
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
    const armix::core::OrbitDispatchStats& stats() const noexcept { return dispatch_; }

private:
    void timerCallback() override
    {
        if (parameter_ == nullptr) return;
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        dispatch_.observe (nowMs - previousTickMs_);
        previousTickMs_ = nowMs;
        ++dispatch_.ticks;
        // Compute from monotonic absolute time: late ticks skip obsolete phases.
        const auto value = armix::core::orbitRamp (
            phaseStart_, (nowMs - startMs_) / 1000.0, period_, reverse_);
        if (!armix::core::shouldPublishOrbit (value, lastPublished_, parameterSteps_))
        {
            ++dispatch_.coalesced;
            return;
        }
        parameter_->setValueNotifyingHost (value);
        lastPublished_ = value;
        ++dispatch_.writes;
    }

    juce::AudioPluginInstance* processor_ = nullptr; // borrowed, only while slot stable
    juce::AudioProcessorParameter* parameter_ = nullptr; // borrowed, only while slot stable
    int slot_ = -1, parameterIndex_ = -1;
    double period_ = 20.0, startMs_ = 0.0;
    float phaseStart_ = 0.0f;
    float lastPublished_ = 0.0f;
    int parameterSteps_ = 0;
    double previousTickMs_ = 0.0;
    armix::core::OrbitDispatchStats dispatch_;
    bool reverse_ = false;
};
