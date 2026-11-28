// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "AdaptiveClockBridge.h"
#include "AudioContinuityPolicy.h"
#include "ClockBridgeSizing.h"
#include "WasapiEndpointIdentityGuard.h"
#include <atomic>
#include <cmath>
#include <thread>

namespace armix::device {

// OUTPUT1: one MIX/FX engine, one existing MAIN PCM producer, one additional
// render-ONLY WASAPI clock consumer feeding an explicit additional selected playback device.
// Clock drift is resampled by the pre-existing bounded ASRC (no DSP duplication).
// No "zero-millisecond" claim across independent clocks.
class LiveSecondaryOutputDevice final {
    struct Render final : juce::AudioIODeviceCallback {
        LiveSecondaryOutputDevice& owner;
        explicit Render(LiveSecondaryOutputDevice& o):owner(o){}
        void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
            if(!d || d->getCurrentSampleRate()!=owner.renderRate_ ||
               !d->getActiveInputChannels().isZero() ||
               d->getActiveOutputChannels().countNumberOfSetBits()!=2)
                owner.markFault();
        }
        void audioDeviceStopped() override {
            // ready is temporarily false during an ASRC reset. That is not
            // an intentional device STOP and cannot mask an unexpected loss.
            if(owner.expectedRunning_.load(std::memory_order_acquire))
                owner.markFault();
        }
        void audioDeviceError(const juce::String&) override {
            owner.markFault();
        }
        void audioDeviceIOCallbackWithContext(const float* const*,int,
                float* const* output,int channels,int frames,
                const juce::AudioIODeviceCallbackContext&) override {
            if(frames<=0)return;
            const auto ticket=owner.captureProducerTicket();
            for(int i=0;i<channels;++i)
                if(output && output[i])
                    juce::FloatVectorOperations::clear(output[i],frames);
            if(!output || channels!=2 || !output[0] || !output[1]){
                owner.markFault();
                return;
            }
            if(!ticket)return;
            // SC reservation precedes final generation validation. The
            // CONTROL reset sees every reader that could still use the ring.
            owner.consumers_.fetch_add(1);
            if(owner.ticketValid(ticket)){
                owner.bridge_.pull(output,2,frames);
                // A STOP/fault racing the pull cannot leave old audio in
                // this callback's destination after the gate was revoked.
                if(!owner.ticketValid(ticket)){
                    juce::FloatVectorOperations::clear(output[0],frames);
                    juce::FloatVectorOperations::clear(output[1],frames);
                }
            }
            owner.consumers_.fetch_sub(1);
        }
    };
public:
    LiveSecondaryOutputDevice():render_(*this){}
    ~LiveSecondaryOutputDevice(){stop();}
    LiveSecondaryOutputDevice(const LiveSecondaryOutputDevice&)=delete;
    LiveSecondaryOutputDevice& operator=(const LiveSecondaryOutputDevice&)=delete;

    juce::String open(const juce::String& target,
                      const juce::String& directOutput,
                      double sourceRate,int sourcePeriod,
                      const std::wstring& expectedEndpointId={}) {
        if(target.trim().isEmpty() || target==directOutput)
            return "Select a separate, explicit user-selected playback device";
        if(!std::isfinite(sourceRate) || sourceRate<8000 || sourceRate>384000 ||
           sourcePeriod<=0 || sourcePeriod>8192)
            return "Invalid source device sample rate or buffer period";
        const auto selected=WasapiEndpointIdentityGuard::resolveRender(target);
        if(!selected.ok())return selected.error;
        if(!expectedEndpointId.empty()&&selected.selection.id!=expectedEndpointId)
            return "Additional output device identity changed; choose the device again";
        // Create ONLY the selected shared-mode render endpoint. A second
        // DeviceManager initialise/setType may transiently open default hardware.
        // Keep all vendor enumeration/open off the primary callback.
        stop();
        type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
        if(!type_)return "Windows Audio output-only endpoint backend unavailable";
        type_->scanForDevices();
        if(!type_->getDeviceNames(false).contains(target)){
            stop();return "Selected Windows playback endpoint unavailable";
        }
        device_.reset(type_->createDevice(target,{}));
        if(!device_){stop();return "Cannot create selected Windows playback endpoint";}
        auto* device=device_.get();
        const auto rates=device->getAvailableSampleRates();
        if(rates.isEmpty()){stop();return "Playback endpoint reports no sample rates";}
        const double requestedRate=rates.contains(sourceRate)?sourceRate:rates[0];
        juce::BigInteger in,out;out.setBit(0);out.setBit(1);
        auto error=device->open(in,out,requestedRate,device->getDefaultBufferSize());
        if(error.isNotEmpty()){stop();return error;}
        if(!device->isOpen() || !device->getActiveInputChannels().isZero() ||
           device->getActiveOutputChannels()!=out){
            stop();return "WASAPI secondary output changed channel mask";
        }
        error=WasapiEndpointIdentityGuard::verifyRender(selected.selection,device->getName());
        if(error.isNotEmpty()){stop();return error;}
        const auto rate=device->getCurrentSampleRate();
        const int block=device->getCurrentBufferSizeSamples();
        const auto sizing=ClockBridgeSizing::choose(sourceRate,rate,sourcePeriod,
                             block,AdaptiveClockBridge::taps,32768,24.);
        if(!sizing.valid ||
           !bridge_.prepare(sourceRate,rate,2,32768,sizing.targetMs)){
            stop();return "Secondary output clock buffering exceeds safe ASRC capacity";
        }
        name_=target;
        endpointId_=selected.selection.id;
        endpointHash_=WasapiEndpointIdentityGuard::fingerprint(endpointId_);
        renderRate_=rate;targetMs_=sizing.targetMs;
        fault_.store(false,std::memory_order_release);
        ready_.store(true,std::memory_order_release);
        expectedRunning_.store(true,std::memory_order_release);
        device->start(&render_);
        if(!device->isPlaying()||faulted()){
            stop();return "Selected playback endpoint could not start";
        }
        return {};
    }

    void stop() {
        admission_.revoke();
        expectedRunning_.store(false,std::memory_order_release);
        ready_.store(false,std::memory_order_release);
        // Wait on CONTROL, never on the audio callback. Reservations and
        // revocation share an SC order, including late old-generation work.
        while(publishers_.load()!=0)
            std::this_thread::yield();
        if(device_){device_->stop();device_->close();device_.reset();}
        type_.reset();
        name_.clear();endpointId_.clear();endpointHash_.clear();targetMs_=renderRate_=0;
        fault_.store(false,std::memory_order_release);
    }
    bool arm(bool requested) {
        if(!requested) {disarm();return true;}
        if(!ready() || faulted())return false;
        // Explicit re-arm is a new PCM generation, not merely a bool. Old
        // primary DSP blocks and consumer callbacks cannot enter the reset ring.
        const auto ticket=admission_.beginRearm();
        ready_.store(false,std::memory_order_release);
        while(publishers_.load()!=0 || consumers_.load()!=0)
            std::this_thread::yield(); // CONTROL only
        if(faulted())return false;
        if(!admission_.current(ticket)){
            ready_.store(true,std::memory_order_release);
            return false; // an explicit STOP superseded this re-arm
        }
        bridge_.resetStopped(); // ONLY when both callbacks are quiescent
        substitutedFrames_.store(0,std::memory_order_relaxed);
        if(faulted())return false;
        ready_.store(true,std::memory_order_release);
        return admission_.authorize(ticket)&&!faulted();
    }
    void disarm() noexcept {admission_.revoke();}
    // Any direct clock restart invalidates the prepared source-rate ASRC.
    // Reconnect explicitly; never resume using stale sample-rate assumptions.
    void invalidateSource() noexcept {
        markFault();
        ready_.store(false,std::memory_order_release);
    }
    bool ready() const noexcept {return ready_.load(std::memory_order_acquire);}
    bool faulted() const noexcept {return fault_.load(std::memory_order_acquire);}
    using Ticket=SecondaryOutputGeneration::Ticket;
    Ticket captureProducerTicket() const noexcept {
        return ready()&&!faulted()?admission_.capture():0;
    }
    bool armed() const noexcept {return captureProducerTicket()!=0;}
    void publish(Ticket ticket,const float* left,const float* right,int frames) noexcept {
        if(!ticket||frames<=0)return;
        publishers_.fetch_add(1);
        if(ticketValid(ticket)) {
            if(left&&right) {
                const float* pair[2]={left,right};
                bridge_.push(pair,2,frames);
            } else {
                // Never send a partial or stale pair. Keep the authorized
                // sink clock fed for this real primary callback interval.
                // Count substitutions separately from actual FIFO loss.
                bridge_.pushSilence(frames);
                substitutedFrames_.fetch_add(static_cast<std::uint64_t>(frames),
                                             std::memory_order_relaxed);
            }
        }
        publishers_.fetch_sub(1);
    }
    juce::String name() const {return name_;}
    juce::String endpointHash() const {return endpointHash_;}
    std::wstring endpointId() const {return endpointId_;} // CONTROL only, never exported to GUI
    double rate() const noexcept{return renderRate_;}
    double targetMs() const noexcept{return targetMs_;}
    std::uint64_t substitutedFrames() const noexcept {
        return ready()?substitutedFrames_.load(std::memory_order_relaxed):0;
    }
    AdaptiveClockBridge::Status status() const noexcept {
        return ready()?bridge_.status():AdaptiveClockBridge::Status{};
    }
private:
    void markFault() noexcept {
        fault_.store(true,std::memory_order_release);
        admission_.revoke();
    }
    bool ticketValid(Ticket ticket) const noexcept {
        return ready()&&!faulted()&&admission_.valid(ticket);
    }
    // Device must be destroyed before its owning backend type.
    std::unique_ptr<juce::AudioIODeviceType> type_;
    std::unique_ptr<juce::AudioIODevice> device_;
    AdaptiveClockBridge bridge_;
    Render render_;
     juce::String name_,endpointHash_;
    std::wstring endpointId_;
    double renderRate_=0,targetMs_=0;
    std::atomic<bool> ready_{false},fault_{false},expectedRunning_{false};
    SecondaryOutputGeneration admission_;
    std::atomic<unsigned> publishers_{0},consumers_{0};
    std::atomic<std::uint64_t> substitutedFrames_{0};
};
} // namespace armix::device
