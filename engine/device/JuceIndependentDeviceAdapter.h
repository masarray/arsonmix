// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "IndependentClockPipeline.h"
#include <array>
#include <atomic>
#include <algorithm>

namespace armix::device
{
// Real JUCE two-manager transport feasibility adapter: independently opened
// capture and render devices feed the same preallocated render-master bridge.
// Deliberately NOT activated by the production LiveAudioEngine until control-
// plane setup migration, VST3 ownership and physical rollback are validated.
// No backend/device calls, dynamic allocation, COM, or GUI access in callbacks.
// A single control thread owns prepare/start/stop; callbacks are drained by
// AudioDeviceManager::removeAudioCallback before any resource is reset.
class JuceIndependentDeviceAdapter final
{
public:
    JuceIndependentDeviceAdapter() : captureCallback_(*this), renderCallback_(*this) {}
    ~JuceIndependentDeviceAdapter() { stop(); }
    JuceIndependentDeviceAdapter(const JuceIndependentDeviceAdapter&) = delete;
    JuceIndependentDeviceAdapter& operator=(const JuceIndependentDeviceAdapter&) = delete;

    // The original application must already have stopped its joined device
    // BEFORE entering this method. Never open two exclusive instances of a
    // capture/render device without a controlled migration transaction.
    juce::String prepare (const juce::String& deviceType,
                          const juce::AudioDeviceManager::AudioDeviceSetup& selection,
                          double fifoTargetMs = 25.0)
    {
        stop();
        if (selection.inputDeviceName.isEmpty() || selection.outputDeviceName.isEmpty())
            return "Select both input and output endpoints before split-mode setup";
        auto err = captureManager_.initialise (1, 0, nullptr, false);
        if (err.isNotEmpty()) return fail (err);
        err = renderManager_.initialise (0, 2, nullptr, false);
        if (err.isNotEmpty()) return fail (err);
        captureManager_.setCurrentAudioDeviceType (deviceType, false);
        renderManager_.setCurrentAudioDeviceType (deviceType, false);
        if (captureManager_.getCurrentAudioDeviceType() != deviceType ||
            renderManager_.getCurrentAudioDeviceType() != deviceType)
            return fail ("Requested backend type is unavailable for one-sided I/O");

        auto captureConfig = captureManager_.getAudioDeviceSetup();
        captureConfig.inputDeviceName = selection.inputDeviceName;
        captureConfig.outputDeviceName.clear();
        captureConfig.outputChannels.clear();
        captureConfig.useDefaultOutputChannels = false;
        captureConfig.sampleRate = selection.sampleRate; // may be negotiated
        captureConfig.bufferSize = selection.bufferSize;
        err = captureManager_.setAudioDeviceSetup (captureConfig, false);
        if (err.isNotEmpty()) return fail ("Capture: " + err);

        auto renderConfig = renderManager_.getAudioDeviceSetup();
        renderConfig.inputDeviceName.clear();
        renderConfig.inputChannels.clear();
        renderConfig.useDefaultInputChannels = false;
        renderConfig.outputDeviceName = selection.outputDeviceName;
        renderConfig.sampleRate = selection.sampleRate;
        renderConfig.bufferSize = selection.bufferSize;
        err = renderManager_.setAudioDeviceSetup (renderConfig, false);
        if (err.isNotEmpty()) return fail ("Render: " + err);

        auto* in = captureManager_.getCurrentAudioDevice();
        auto* out = renderManager_.getCurrentAudioDevice();
        if (in == nullptr || out == nullptr ||
            in->getActiveOutputChannels().countNumberOfSetBits() != 0 ||
            out->getActiveInputChannels().countNumberOfSetBits() != 0 ||
            in->getActiveInputChannels().countNumberOfSetBits() < 1 ||
            out->getActiveOutputChannels().countNumberOfSetBits() < 1)
            return fail ("Driver failed to expose independent capture/render endpoint channels");

        if (!pipeline_.prepare (in->getCurrentSampleRate(), out->getCurrentSampleRate(),
                                 2, 8192, fifoTargetMs))
            return fail ("Separate-stream rates or FIFO capacity are unsupported");
        prepared_ = true;
        return {};
    }

    juce::String start (juce::AudioIODeviceCallback& processor)
    {
        if (!prepared_ || running_) return "Split adapter is not prepared or already running";
        processor_ = &processor;
        // Attach capture producer first, then render consumer, ensuring source
        // clock starts filling FIFO before output becomes audible.
        captureManager_.addAudioCallback (&captureCallback_);
        captureAttached_ = true;
        renderManager_.addAudioCallback (&renderCallback_);
        renderAttached_ = true;
        running_ = true;
        return {};
    }
    void stop()
    {
        // Removal synchronously drains callbacks. Render stops before producer.
        if (renderAttached_)
        {
            renderManager_.removeAudioCallback (&renderCallback_);
            renderAttached_ = false;
        }
        if (captureAttached_)
        {
            captureManager_.removeAudioCallback (&captureCallback_);
            captureAttached_ = false;
        }
        processor_ = nullptr;
        running_ = false;
        renderManager_.closeAudioDevice();
        captureManager_.closeAudioDevice();
        prepared_ = false;
    }
    bool running() const noexcept { return running_; } // control-thread only
    juce::AudioIODevice* outputDevice() const noexcept
    { return renderManager_.getCurrentAudioDevice(); }
    juce::AudioIODevice* inputDevice() const noexcept
    { return captureManager_.getCurrentAudioDevice(); }
    IndependentClockPipeline& pipeline() noexcept { return pipeline_; }
    bool callbackErrorObserved() const noexcept
    { return driverError_.load(std::memory_order_relaxed); }

private:
    class Capture final : public juce::AudioIODeviceCallback
    {
    public:
        explicit Capture(JuceIndependentDeviceAdapter& owner):owner_(owner){}
        void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
        void audioDeviceError(const juce::String&) override
        { owner_.driverError_.store(true,std::memory_order_relaxed); }
        void audioDeviceIOCallbackWithContext(const float* const* input,int numInput,
            float* const* output,int numOutput,int frames,
            const juce::AudioIODeviceCallbackContext&) override
        {
            // A capture-only device should not have output channels; zero any
            // incorrectly exposed buffer to fail closed on buggy backends.
            for(int c=0;c<numOutput;++c)
                if(output[c]) std::fill_n(output[c],frames,0.f);
            owner_.pipeline_.capture(input,numInput,frames);
        }
    private: JuceIndependentDeviceAdapter& owner_;
    };
    class Render final : public juce::AudioIODeviceCallback
    {
    public:
        explicit Render(JuceIndependentDeviceAdapter& owner):owner_(owner){}
        void audioDeviceAboutToStart(juce::AudioIODevice* device) override
        {
            if(owner_.processor_) owner_.processor_->audioDeviceAboutToStart(device);
        }
        void audioDeviceStopped() override
        {
            if(owner_.processor_) owner_.processor_->audioDeviceStopped();
        }
        void audioDeviceError(const juce::String&) override
        { owner_.driverError_.store(true,std::memory_order_relaxed); }
        void audioDeviceIOCallbackWithContext(const float* const*,int,
            float* const* outputs,int numOutputs,int numSamples,
            const juce::AudioIODeviceCallbackContext& context) override
        {
            // Process arbitrary backend block lengths in fixed bounded chunks.
            // No overflow of preallocated PCM buffers or stack pointer table.
            constexpr int maxFrames=8192, maxOutputPointers=32;
            for(int offset=0;offset<numSamples;offset+=maxFrames)
            {
                const int frames=std::min(maxFrames,numSamples-offset);
                float* captureData[2] = {owner_.scratch_[0].data(),owner_.scratch_[1].data()};
                owner_.pipeline_.render(captureData,2,frames);
                float* deviceOutput[maxOutputPointers] {};
                const int count=std::min(numOutputs,maxOutputPointers);
                for(int c=0;c<count;++c)
                    deviceOutput[c]=outputs[c]?outputs[c]+offset:nullptr;
                if(owner_.processor_)
                {
                    const float* input[2]={captureData[0],captureData[1]};
                    owner_.processor_->audioDeviceIOCallbackWithContext(
                        input,2,deviceOutput,count,frames,context);
                }
                else for(int c=0;c<count;++c)
                    if(deviceOutput[c]) std::fill_n(deviceOutput[c],frames,0.f);
                for(int c=count;c<numOutputs;++c)
                    if(outputs[c]) std::fill_n(outputs[c]+offset,frames,0.f);
            }
        }
    private: JuceIndependentDeviceAdapter& owner_;
    };
    juce::String fail(const juce::String& why) { stop(); return why; }

    juce::AudioDeviceManager captureManager_, renderManager_;
    IndependentClockPipeline pipeline_;
    Capture captureCallback_;
    Render renderCallback_;
    std::array<std::array<float,8192>,2> scratch_ {};
    juce::AudioIODeviceCallback* processor_ = nullptr;
    std::atomic<bool> driverError_ {false};
    bool prepared_ = false, running_ = false;
    bool captureAttached_ = false, renderAttached_ = false;
};
} // namespace armix::device
