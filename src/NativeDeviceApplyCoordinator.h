// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace armix::device {
// JUCE MESSAGE/CONTROL thread only. One serialized device transaction owner.
// This class owns request/pin/readback validation and the narrow driver-open
// boundary, NOT a second AudioDeviceManager, mixer document or PCM callback.
// The host still owns physical output permission and session rebind/rollback.
class NativeDeviceApplyCoordinator final {
public:
    // Every possible JUCE driver mutation uses the same control-thread gate:
    // explicit UI Apply, native modal selector, and bounded ASIO recovery.
    enum class Purpose : std::uint8_t { none, explicitApply, setupDialog, asioRecovery };

    class Lease final {
    public:
        Lease() noexcept = default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_,nullptr)) {}
        Lease& operator=(Lease&&)=delete;
        ~Lease() {if(owner_)owner_->purpose_=Purpose::none;}
        explicit operator bool() const noexcept {return owner_!=nullptr;}
    private:
        friend class NativeDeviceApplyCoordinator;
        explicit Lease(NativeDeviceApplyCoordinator* owner) noexcept : owner_(owner) {}
        NativeDeviceApplyCoordinator* owner_=nullptr;
    };

    bool busy() const noexcept {return purpose_!=Purpose::none;}
    Purpose activePurpose() const noexcept {return purpose_;}
    [[nodiscard]] Lease tryBegin(Purpose purpose=Purpose::explicitApply) noexcept {
        if(purpose==Purpose::none || busy())return {};
        purpose_=purpose;
        return Lease(this);
    }

    struct Request {
        juce::String backend,capture,playback;
        double rate=0.,block=0.;
        bool assignMain=false,confirmRetainedPinMapping=false;
    };
    struct Parsed {
        Request value{};
        juce::String error;
        explicit operator bool() const noexcept {return error.isEmpty();}
    };
    static Parsed parse(const juce::var& config) {
        Parsed out;
        if(!config.isObject()) {
            out.error="Missing device configuration";
            return out;
        }
        auto& r=out.value;
        r.backend=config.getProperty("backend",{}).toString();
        r.capture=config.getProperty("captureDevice",{}).toString();
        r.playback=config.getProperty("playbackDevice",{}).toString();
        if(r.backend.isEmpty()||r.playback.isEmpty()||
           r.backend.length()>160||r.capture.length()>250||r.playback.length()>250) {
            out.error="Choose an actual playback device";
            return out;
        }
        const auto sample=config.getProperty("sampleRate",{}),
                   period=config.getProperty("bufferSize",{});
        if(!(sample.isInt()||sample.isInt64()||sample.isDouble())||
           !(period.isInt()||period.isInt64()||period.isDouble())) {
            out.error="Rate and buffer must be numeric or AUTO (0)";
            return out;
        }
        r.rate=static_cast<double>(sample);
        r.block=static_cast<double>(period);
        if(!std::isfinite(r.rate)||!std::isfinite(r.block)||
           (r.rate!=0.&&(r.rate<8000.||r.rate>384000.))||
           (r.block!=0.&&(r.block<16.||r.block>8192.||
                          std::floor(r.block)!=r.block))) {
            out.error="Invalid device sample rate or buffer size";
            return out;
        }
        const auto assign=config.getProperty("assignSelectedPlaybackPinsToMain",false);
        if(!assign.isBool()) {
            out.error="Invalid MAIN playback pin selection";
            return out;
        }
        const auto confirm=config.getProperty("confirmRetainedPinMapping",false);
        if(!confirm.isBool()) {
            out.error="Invalid retained mixer mapping confirmation";
            return out;
        }
        r.assignMain=static_cast<bool>(assign);
        r.confirmRetainedPinMapping=static_cast<bool>(confirm);
        return out;
    }

    static juce::String validateNames(const Request& r,
             const juce::StringArray& inputs,const juce::StringArray& outputs,
             bool separate) {
        const bool outputOnly=separate&&r.capture.isEmpty();
        if((!outputOnly&&!inputs.contains(r.capture))||
           !outputs.contains(r.playback)||
           (!separate&&r.capture!=r.playback))
            return "Device names or duplex pairing are not reported by JUCE";
        return {};
    }

    // Do not narrow untrusted 64-bit UI values to int before testing bounds:
    // a forged 2^32+N pin could otherwise alias an existing physical jack.
    static bool parsePins(const juce::var& values,int limit,juce::BigInteger& mask) {
        const auto* list=values.getArray();
        if(!list||list->size()>64||limit<0||limit>64)return false;
        juce::BigInteger candidate;
        for(const auto& value:*list) {
            if(!(value.isInt()||value.isInt64()))return false;
            const auto raw=static_cast<std::int64_t>(value);
            if(raw<0||raw>=limit||candidate[static_cast<int>(raw)])return false;
            candidate.setBit(static_cast<int>(raw));
        }
        mask=candidate;
        return true;
    }

    static bool parsePinMasks(const juce::var& inputs,const juce::var& outputs,
            int inputCount,int outputCount,bool outputOnly,
            juce::BigInteger& inputMask,juce::BigInteger& outputMask) {
        juce::BigInteger in,out;
        if(!parsePins(inputs,juce::jmin(64,inputCount),in)||
           !parsePins(outputs,juce::jmin(64,outputCount),out)||
           (outputOnly&&!in.isZero())||out.countNumberOfSetBits()<2)return false;
        inputMask=in;outputMask=out;
        return true;
    }

    struct AppliedReadback {
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        juce::String backend, deviceName, deviceType;
        bool open=false;
        juce::BigInteger activeInputs,activeOutputs;
        double deviceRate=0.;
        int deviceBuffer=0;
    };
    // Read *both* manager configuration and the installed JUCE device object.
    // The latter is the hardware evidence; it cannot be synthesized from the
    // requested/stored setup. CONTROL thread only, after openSelected returns.
    static AppliedReadback inspectApplied(juce::AudioDeviceManager& manager) {
        AppliedReadback evidence;
        manager.getAudioDeviceSetup(evidence.actual);
        evidence.backend=manager.getCurrentAudioDeviceType();
        if(auto* device=manager.getCurrentAudioDevice();device && device->isOpen()) {
            evidence.open=true;
            evidence.deviceName=device->getName();
            evidence.deviceType=device->getTypeName();
            evidence.activeInputs=device->getActiveInputChannels();
            evidence.activeOutputs=device->getActiveOutputChannels();
            evidence.deviceRate=device->getCurrentSampleRate();
            evidence.deviceBuffer=device->getCurrentBufferSizeSamples();
        }
        return evidence;
    }
    // A driver may return an OPEN status but no usable clock/buffer evidence.
    // Unknown/zero/nonfinite live formats cannot establish playable PCM.
    static bool validLiveFormat(double sampleRate,int frames) noexcept {
        return std::isfinite(sampleRate) && sampleRate>=8000. &&
               sampleRate<=384000. && frames>=16 && frames<=8192;
    }
    // The acknowledgement is not permission to output sound: it proves only
    // that JUCE accepted the selected backend, names, masks, rate and period.
    static bool matchesApplied(const Request& requested,
            const juce::AudioDeviceManager::AudioDeviceSetup& candidate,
            const AppliedReadback& live) noexcept {
        const auto& a=live.actual;
        return live.open&&validLiveFormat(live.deviceRate,live.deviceBuffer)&&
            live.backend==requested.backend&&
            // Strict physical instance identity for ASIO: manager setup alone
            // may name the intended driver while another callback is active.
            (requested.backend!="ASIO" ||
             (live.deviceType=="ASIO" && live.deviceName==requested.playback))&&
            a.inputDeviceName==requested.capture&&
            a.outputDeviceName==requested.playback&&
            (requested.rate<=0.||std::abs(a.sampleRate-requested.rate)<=1.)&&
            (requested.block<=0.||a.bufferSize==static_cast<int>(requested.block))&&
            (candidate.useDefaultInputChannels||a.inputChannels==candidate.inputChannels)&&
            (candidate.useDefaultOutputChannels||a.outputChannels==candidate.outputChannels)&&
            live.activeInputs==a.inputChannels&&live.activeOutputs==a.outputChannels&&
            live.activeOutputs.countNumberOfSetBits()>=2&&
            std::abs(live.deviceRate-a.sampleRate)<=1.&&
            live.deviceBuffer==a.bufferSize;
    }

    struct RollbackReadback {
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        juce::String backend, deviceName, deviceType;
        bool open=false;
        juce::StringArray inputNames,outputNames;
        juce::BigInteger activeInputs,activeOutputs;
        double deviceRate=0.;
        int deviceBuffer=0;
    };
    static RollbackReadback inspectRollback(juce::AudioDeviceManager& manager) {
        RollbackReadback evidence;
        manager.getAudioDeviceSetup(evidence.actual);
        evidence.backend=manager.getCurrentAudioDeviceType();
        if(auto* device=manager.getCurrentAudioDevice();device && device->isOpen()) {
            evidence.open=true;
            evidence.deviceName=device->getName();
            evidence.deviceType=device->getTypeName();
            evidence.inputNames=device->getInputChannelNames();
            evidence.outputNames=device->getOutputChannelNames();
            evidence.activeInputs=device->getActiveInputChannels();
            evidence.activeOutputs=device->getActiveOutputChannels();
            evidence.deviceRate=device->getCurrentSampleRate();
            evidence.deviceBuffer=device->getCurrentBufferSizeSamples();
        }
        return evidence;
    }
    // Previously CLOSED driver is never a valid rollback target. For ASIO,
    // even a numerically equal mask is unsafe after pin names are reordered.
    static bool matchesRollback(
            const juce::AudioDeviceManager::AudioDeviceSetup& before,
            const juce::String& previousBackend,
            const juce::StringArray& previousInputs,
            const juce::StringArray& previousOutputs,
            const RollbackReadback& live) noexcept {
        const auto& a=live.actual;
        return live.open&&validLiveFormat(live.deviceRate,live.deviceBuffer)&&
            live.backend==previousBackend&&
            (previousBackend!="ASIO" ||
              (live.deviceType=="ASIO" && live.deviceName==before.outputDeviceName))&&
            a.inputDeviceName==before.inputDeviceName&&
            a.outputDeviceName==before.outputDeviceName&&
            a.inputChannels==before.inputChannels&&
            a.outputChannels==before.outputChannels&&
            a.sampleRate==before.sampleRate&&a.bufferSize==before.bufferSize&&
            // A manager can remember an old setup even if an ASIO instance
            // restarted with fewer ports or a different callback quantum.
            live.activeInputs==a.inputChannels&&live.activeOutputs==a.outputChannels&&
            live.activeOutputs.countNumberOfSetBits()>=2&&
            std::abs(live.deviceRate-a.sampleRate)<=1.&&
            live.deviceBuffer==a.bufferSize&&
            (previousBackend!="ASIO"||
             (live.inputNames==previousInputs&&live.outputNames==previousOutputs));
    }


    // Device rebind owns the hardware graph, NEVER the accepted logical
    // ChannelId/.smix document. Pure control-thread verdict: the caller is
    // responsible for performing the exact existing native restore action.
    enum class MixerRebindAction : std::uint8_t {
        reject, retainInstalled, restoreRetained, acceptFreshBound
    };
    struct MixerRebindEvidence {
        bool physicalAuthorized=false;
        bool installedChannelPcm=false;
        bool storedCheckpoint=false;
        bool pendingStartupRestore=false;
        bool pristineStartupMixer=false;
        bool retainedNativeSession=false;
        bool freshBound=false;
    };
    [[nodiscard]] static constexpr MixerRebindAction decideMixerRebind(
            MixerRebindEvidence e) noexcept {
        if(!e.physicalAuthorized)return MixerRebindAction::reject;
        const bool mustRestorePristine=e.storedCheckpoint &&
            e.pendingStartupRestore && e.pristineStartupMixer;
        if(e.installedChannelPcm && !mustRestorePristine)
            return MixerRebindAction::retainInstalled;
        if(e.retainedNativeSession || e.storedCheckpoint)
            return MixerRebindAction::restoreRetained;
        return e.freshBound?MixerRebindAction::acceptFreshBound:
                            MixerRebindAction::reject;
    }

    using RecordPhase=std::function<void(const juce::String&)>;
    static juce::String openSelected(juce::AudioDeviceManager& manager,
          const juce::String& backend,
          const juce::AudioDeviceManager::AudioDeviceSetup& setup,
          bool resetDefaults,const RecordPhase& record) {
        if(manager.getCurrentAudioDeviceType()==backend&&!resetDefaults) {
            if(auto* device=manager.getCurrentAudioDevice();device&&!device->isOpen())
                manager.closeAudioDevice();
            return manager.setAudioDeviceSetup(setup,true);
        }
        // setCurrentAudioDeviceType can open an unrelated remembered endpoint;
        // initialise directly with the explicit backend and physical names.
        auto xml=manager.createStateXml();
        if(!xml)xml=std::make_unique<juce::XmlElement>("DEVICESETUP");
        xml->setAttribute("deviceType",backend);
        xml->removeAttribute("audioDeviceName");
        xml->setAttribute("audioInputDeviceName",setup.inputDeviceName);
        xml->setAttribute("audioOutputDeviceName",setup.outputDeviceName);
        xml->setAttribute("audioDeviceRate",setup.sampleRate);
        xml->setAttribute("audioDeviceBufferSize",setup.bufferSize);
        const auto setMask=[&](const char* key,bool defaults,const juce::BigInteger& mask) {
            if(defaults)xml->removeAttribute(key);
            else xml->setAttribute(key,mask.toString(2));
        };
        setMask("audioDeviceInChans",setup.useDefaultInputChannels,setup.inputChannels);
        setMask("audioDeviceOutChans",setup.useDefaultOutputChannels,setup.outputChannels);
        if(record)record("CLOSE_PREVIOUS | "+manager.getCurrentAudioDeviceType());
        manager.closeAudioDevice();
        if(record)record("OPEN_SELECTED | "+backend);
        return manager.initialise(2,2,xml.get(),false);
    }

private:
    Purpose purpose_=Purpose::none; // JUCE message thread; one driver transaction
};
} // namespace armix::device
