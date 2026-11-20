// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SecondaryCaptureClockStream.h"
#include "AsioPriorityDeviceManager.h"
#include "AsioRegistryIdentityProbe.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace armix::device
{
// IO-1 production-capable, explicit SECONDARY ASIO input-only device.
// The JUCE callback is a producer ONLY; it never invokes a mixer, VST3,
// WebView, file I/O or user-selected output. The primary ASIO render
// callback is the ONLY consumer, through pullForPrimary() after IO-2 source
// admission. A driver that requires an output channel is REJECTED, never
// silently turned into a speaker. No second AudioDeviceManager initialise.
class LiveSecondaryCaptureDevice final
{
    struct Capture final : juce::AudioIODeviceCallback
    {
        LiveSecondaryCaptureDevice& owner;
        explicit Capture(LiveSecondaryCaptureDevice& o):owner(o){}
        void audioDeviceAboutToStart(juce::AudioIODevice* d) override
        {
            // JUCE lifecycle, not the realtime sample callback.
            if(!d||d->getName()!=owner.name_||
               d->getActiveInputChannels()!=owner.inputMask_||
               !d->getActiveOutputChannels().isZero()||
               std::abs(d->getCurrentSampleRate()-owner.captureRate_)>0.001||
               d->getCurrentBufferSizeSamples()!=owner.capturePeriod_)
                owner.stream_.fault();
        }
        void audioDeviceStopped() override
        {
            if(owner.opened_.load(std::memory_order_acquire))
                owner.stream_.fault();
        }
        void audioDeviceError(const juce::String&) override {owner.stream_.fault();}
        void audioDeviceIOCallbackWithContext(
            const float* const* inputs,int inputsCount,
            float* const* outputs,int outputsCount,int frames,
            const juce::AudioIODeviceCallbackContext&) override
        {
            // Defensive silence if a broken driver produces output pointers.
            for(int c=0;c<outputsCount;++c)
                if(outputs&&outputs[c]&&frames>0)
                    juce::FloatVectorOperations::clear(outputs[c],frames);
            if(outputsCount!=0)
            {
                owner.stream_.fault();
                return;
            }
            // This is the ONLY capture writer; no plugin, render, allocation,
            // lock, type scan, COM, registry, UI or disk work on this thread.
            owner.stream_.push(inputs,inputsCount,frames);
        }
    };
public:
    struct Readback
    {
        juce::String driverName; // CONTROL-only, not an endpoint hardware GUID
        juce::BigInteger activeInputPins; // physical indices, not callback order
        double captureRate=0.,primaryRate=0.,bridgeTargetMs=0.;
        int capturePeriod=0,primaryPeriod=0,callbackInputs=0;
        std::uint64_t captureGeneration=0,primaryEpoch=0,deviceLease=0;
        std::uint64_t driverClassFingerprint=0; // diagnostic, not authorization
        bool ready=false,faulted=false;
        SecondaryCaptureClockStream::Status clock{};
    };

    LiveSecondaryCaptureDevice():capture_(*this){}
    ~LiveSecondaryCaptureDevice(){close();}
    LiveSecondaryCaptureDevice(const LiveSecondaryCaptureDevice&)=delete;
    LiveSecondaryCaptureDevice& operator=(const LiveSecondaryCaptureDevice&)=delete;

    // Called ONLY after primary driver is stable, on serialized CONTROL.
    // No requested software fallback, no 0-output -> duplex retry, no
    // automatic speaker ARM. generation and primaryEpoch are native leases.
    juce::String open(const juce::String& secondaryName,
                      const juce::String& primaryAsioName,
                      const juce::BigInteger& selectedInputPins,
                      double requestedCaptureHz,int requestedCapturePeriod,
                      double primaryHz,int primaryPeriod,
                      std::uint64_t captureGeneration,
                      std::uint64_t primaryEpoch,
                      std::uint64_t deviceLease)
    {
        close();
#if !defined(_WIN32) || !JUCE_ASIO
        (void)secondaryName;(void)primaryAsioName;(void)selectedInputPins;
        (void)requestedCaptureHz;(void)requestedCapturePeriod;
        (void)primaryHz;(void)primaryPeriod;
        (void)captureGeneration;(void)primaryEpoch;(void)deviceLease;
        return "Independent ASIO capture is Windows JUCE ASIO only";
#else
        if(primaryAsioName.isEmpty()||secondaryName.isEmpty()||
           secondaryName==primaryAsioName||selectedInputPins.isZero()||
           selectedInputPins.countNumberOfSetBits()>16||
           selectedInputPins.getHighestBit()>=64||
           captureGeneration==0||primaryEpoch==0||deviceLease==0||
           !std::isfinite(requestedCaptureHz)||
           requestedCaptureHz<8000.||requestedCaptureHz>192000.||
           requestedCapturePeriod<32||
           requestedCapturePeriod>SecondaryCaptureClockStream::maxCallbackFrames)
            return "Invalid explicit secondary ASIO capture selection";
        const std::wstring p(primaryAsioName.toWideCharPointer());
        const std::wstring b(secondaryName.toWideCharPointer());
        const auto primary=AsioRegistryIdentityProbe::resolve(p).registration;
        const auto secondary=AsioRegistryIdentityProbe::resolve(b).registration;
        if(!primary.ok()||!secondary.ok()||
           primary.identity.sameDriverClass(secondary.identity))
            return "Secondary ASIO must be an independently attested driver class";

        enumerator_.createAudioDeviceTypes(factories_);
        juce::AudioIODeviceType* asio=nullptr;
        for(auto* type:factories_)
            if(type->getTypeName()=="ASIO"){asio=type;break;}
        if(!asio){close();return "JUCE ASIO backend is unavailable";}
        asio->scanForDevices(); // CONTROL; never enumerate while callbacks run
        if(!asio->getDeviceNames(true).contains(secondaryName))
        {close();return "Selected secondary ASIO input driver is missing";}

        // JUCE signature is createDevice(OUTPUT name, INPUT name).
        device_.reset(asio->createDevice({},secondaryName));
        if(!device_){close();return "ASIO rejected capture-only construction";}
        const juce::BigInteger noOutputs;
        auto error=device_->open(selectedInputPins,noOutputs,
                                 requestedCaptureHz,requestedCapturePeriod);
        if(error.isNotEmpty()){close();return error;}
        if(!device_->isOpen()||device_->getName()!=secondaryName||
           device_->getActiveInputChannels()!=selectedInputPins||
           !device_->getActiveOutputChannels().isZero())
        {close();return "ASIO changed input pins, output isolation, or device identity";}
        const auto actualHz=device_->getCurrentSampleRate();
        const auto actualPeriod=device_->getCurrentBufferSizeSamples();
        if(std::abs(actualHz-requestedCaptureHz)>0.001||
           actualPeriod!=requestedCapturePeriod)
        {close();return "ASIO changed the explicitly requested capture rate/period";}
        const auto pAfter=AsioRegistryIdentityProbe::resolve(p).registration;
        const auto bAfter=AsioRegistryIdentityProbe::resolve(b).registration;
        if(!pAfter.ok()||!bAfter.ok()||
           !primary.identity.sameDriverClass(pAfter.identity)||
           !secondary.identity.sameDriverClass(bAfter.identity))
        {close();return "ASIO registry driver class changed during capture open";}
        if(!stream_.prepare({actualHz,primaryHz,actualPeriod,primaryPeriod,
                selectedInputPins.countNumberOfSetBits(),
                captureGeneration,primaryEpoch,deviceLease}))
        {close();return "Existing bounded clock bridge cannot admit negotiated periods";}
        name_=secondaryName;
        inputMask_=selectedInputPins;
        captureRate_=actualHz;capturePeriod_=actualPeriod;
        primaryRate_=primaryHz;primaryPeriod_=primaryPeriod;
        generation_=captureGeneration;primaryEpoch_=primaryEpoch;
        deviceLease_=deviceLease;
        classFingerprint_=AsioRegistryIdentityProbe::diagnosticFingerprint(
            secondary.identity);
        if(!stream_.activate()){close();return "Capture bridge activation refused";}
        opened_.store(true,std::memory_order_release);
        device_->start(&capture_);
        if(stream_.faulted()||!device_->isPlaying())
        {close();return "ASIO capture callback refused or changed its format";}
        return {};
#endif
    }

    // CONTROL stop, idempotent. Detached device producer and primary reader
    // are quiescent before bridge/FIR memory is reset or device is destroyed.
    void close()
    {
        opened_.store(false,std::memory_order_release);
        stream_.fault();
        if(device_){device_->stop();device_->close();}
        stream_.stop();
        device_.reset(); // must be destroyed before its ASIO factory
        factories_.clear();
        name_.clear();inputMask_.clear();
        captureRate_=primaryRate_=0.;
        capturePeriod_=primaryPeriod_=0;
        generation_=primaryEpoch_=deviceLease_=classFingerprint_=0;
    }

    // RT primary render-clock READ ONLY. Return silence on stale primary
    // device epoch, stale capture generation, stalled/faulted secondary, or
    // non-admitted channel format. No DSP or secondary device access.
    bool pullForPrimary(float* const* dest,int channels,int frames,
                        std::uint64_t captureGeneration,
                        std::uint64_t primaryEpoch,
                        std::uint64_t deviceLease) noexcept
    {
        return stream_.pull(dest,channels,frames,
                            captureGeneration,primaryEpoch,deviceLease);
    }
    void invalidatePrimaryClock() noexcept {stream_.fault();}
    bool ready() const noexcept{return opened_.load(std::memory_order_acquire)&&
                                       stream_.ready();}
    bool faulted() const noexcept{return stream_.faulted();}
    // Borrowed ONLY from the one native primary callback; lifetime owned by
    // this LiveSecondaryCaptureDevice and serialized CONTROL stop/close.
    SecondaryCaptureClockStream& clockStream() noexcept{return stream_;}
    Readback readback() const // CONTROL only: called after driver lifecycle
    {
        Readback b;
        b.driverName=name_;
        b.activeInputPins=inputMask_;
        b.captureRate=captureRate_;b.primaryRate=primaryRate_;
        b.capturePeriod=capturePeriod_;b.primaryPeriod=primaryPeriod_;
        b.callbackInputs=inputMask_.countNumberOfSetBits();
        b.bridgeTargetMs=stream_.targetMs();
        b.captureGeneration=generation_;
        b.primaryEpoch=primaryEpoch_;b.deviceLease=deviceLease_;
        b.driverClassFingerprint=classFingerprint_;
        b.ready=ready();b.faulted=faulted();
        b.clock=stream_.status();
        return b;
    }

private:
    AsioPriorityDeviceManager enumerator_; // never initialise a default device
    juce::OwnedArray<juce::AudioIODeviceType> factories_; // outlive device_
    std::unique_ptr<juce::AudioIODevice> device_;
    SecondaryCaptureClockStream stream_;
    Capture capture_;
    juce::String name_;
    juce::BigInteger inputMask_;
    double captureRate_=0.,primaryRate_=0.;
    int capturePeriod_=0,primaryPeriod_=0;
    std::uint64_t generation_=0,primaryEpoch_=0,deviceLease_=0,
                  classFingerprint_=0;
    std::atomic<bool> opened_{false};
};
} // namespace armix::device
