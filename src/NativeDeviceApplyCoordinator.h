// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/device/AudioSetupRecoveryPolicy.h"
#include "../engine/device/DeviceSessionTransitionPolicy.h"
#include "../engine/device/NamedDevicePins.h"
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
// The host still owns physical output permission and engine/session publication.
class NativeDeviceApplyCoordinator final {
public:
    // Every possible JUCE driver mutation uses the same control-thread gate:
    // explicit custom UI Apply, explicit vendor ASIO panel, and bounded recovery.
    enum class Purpose : std::uint8_t { none, explicitApply, setupDialog, asioRecovery, wasapiRecovery, offlineFxCapture, secondaryOutput, busOutputPatch, fxRackFile };

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

    // A blocked correlated command still requires a terminal NACK. This
    // path is CONTROL-only and deliberately cannot inspect or mutate a driver,
    // graph, or accepted revision during a nested vendor modal loop.
    struct BusyReceipt {
        const char* event=nullptr;
        juce::var payload;
    };
    BusyReceipt busyCommandReceipt(const juce::String& action,const juce::var& args) const {
        if(!busy())return {};
        const char* event=nullptr;
        if(action=="routingExternalOutput")event="arMixExternalOutputAck";
        else if(action=="routingMonitorTiming")event="arMixMonitorTimingAck";
        else if(action=="routingLogicalInputEdit")event="arMixLogicalInputAck";
        else if(action=="routingLogicalOutputEdit")event="arMixLogicalOutputAck";
        else if(action=="audioIoDefaults")event="arMixIoDefaultsAck";
        else if(action=="audioNamedPatchEdit")event="arMixNamedPatchAck";
        else if(action=="routingNativeFxRackBypass")event="arMixNativeFxPowerAck";
        else if(action=="routingNativeFxInsertBypass")event="arMixNativeFxPowerAck";
        else if(action=="routingM4Control")event="arMixM4ControlAck";
        else if(action=="routingChannelPatch")event="arMixChannelPatchAck";
        else if(action=="routingSourceSwitch")event="arMixSourceSwitchAck";
        else if(action=="routingFxInsert")event="arMixRoutingFxInsertAck";
        else if(action=="routingFxBypass")event="arMixRoutingFxBypassAck";
        else if(action=="routingFxRackBypass")event="arMixRoutingFxRackAck";
        else if(action=="routingGlobalFxBypass")event="arMixGlobalFxAck";
        if(!event)return {};
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",args.getProperty("requestId",{}));
        ack->setProperty("ok",false);
        ack->setProperty("error","Audio configuration is finishing. Retry when ready.");
        // Echo request identity only, not authoritative installed state.
        if(action=="routingExternalOutput"){
            ack->setProperty("channelId",args.getProperty("channelId",{}).toString());
            ack->setProperty("deviceEpoch",args.getProperty("deviceEpoch",{}));
            ack->setProperty("graphRevision",args.getProperty("revision",{}));
        }else if(action=="routingNativeFxRackBypass"||action=="routingNativeFxInsertBypass"){
            ack->setProperty("channelId",args.getProperty("channelId",{}));
            ack->setProperty("bypassed",args.getProperty("value",{}));
        }else if(action=="routingFxInsert"||action=="routingFxBypass"){
            // The bridge indexes a pending legacy slot-power request by channel:slot.
            ack->setProperty("channel",args.getProperty("channel",-1));
            ack->setProperty("slot",args.getProperty("slot",-1));
        }else if(action=="routingM4Control"||action=="routingChannelPatch"||
                 action=="routingSourceSwitch"||action=="routingFxRackBypass"){
            ack->setProperty("channel",args.getProperty("channel",-1));
        }
        return {event,juce::var(ack.release())};
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
        const auto rawBackend=config.getProperty("backend",{});
        const auto rawCapture=config.getProperty("captureDevice",{});
        const auto rawPlayback=config.getProperty("playbackDevice",{});
        // Device names are exact JUCE-provided strings. A JSON number,
        // boolean or object must never be string-coerced into a physical
        // endpoint. An omitted/empty capture is valid for output-only mode.
        if(!rawBackend.isString() || !rawPlayback.isString() ||
           (!rawCapture.isVoid() && !rawCapture.isString())) {
            out.error="Invalid audio device name types";
            return out;
        }
        r.backend=rawBackend.toString();
        r.capture=rawCapture.isVoid()?juce::String{}:rawCapture.toString();
        r.playback=rawPlayback.toString();
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
            const auto raw=static_cast<juce::int64>(value);
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
        // OUTPUT ONLY must not invent a microphone or require absent IN pins.
        return manager.initialise(setup.inputDeviceName.isEmpty()?0:2,
                                  2,xml.get(),false);
    }

    using BindingMode=DeviceBindingMode;
    using Setup=juce::AudioDeviceManager::AudioDeviceSetup;

    struct ApplyPlan {
        Request requested;
        Setup before,candidate;
        juce::String previousBackend,error;
        juce::StringArray originalInputNames,originalOutputNames;
        AudioSetupRecoveryPolicy::Plan recovery{};
        bool canConfirmRetainedPins=false;
    };

    // Snapshot and validate before suspending the installed engine. No
    // driver operation is permitted during this stage.
    static ApplyPlan prepare(juce::AudioDeviceManager& manager,const juce::var& config) {
        ApplyPlan plan;
        const auto parsed=parse(config);
        if(!parsed) {
            plan.error=parsed.error;
            return plan;
        }
        plan.requested=parsed.value;
        const auto& r=plan.requested;
        juce::AudioIODeviceType* selected=nullptr;
        for(auto* type:manager.getAvailableDeviceTypes())
            if(type && type->getTypeName()==r.backend)selected=type;
        if(!selected) {
            plan.error="Audio backend is not installed";
            return plan;
        }

        const bool separate=selected->hasSeparateInputsAndOutputs();
        plan.error=validateNames(r,selected->getDeviceNames(true),
            selected->getDeviceNames(false),separate);
        if(plan.error.isNotEmpty())return plan;

        plan.previousBackend=manager.getCurrentAudioDeviceType();
        manager.getAudioDeviceSetup(plan.before);
        auto* device=manager.getCurrentAudioDevice();
        const bool priorOpen=device && device->isOpen();
        const bool sameEndpoint=plan.previousBackend==r.backend &&
            plan.before.inputDeviceName==r.capture && plan.before.outputDeviceName==r.playback;
        if(r.confirmRetainedPinMapping) {
            plan.canConfirmRetainedPins=!r.assignMain && sameEndpoint && priorOpen &&
                r.rate==plan.before.sampleRate && r.block==plan.before.bufferSize;
            return plan;
        }

        if(priorOpen) {
            plan.originalInputNames=device->getInputChannelNames();
            plan.originalOutputNames=device->getOutputChannelNames();
        }

        auto& candidate=plan.candidate;
        candidate=plan.before;
        candidate.inputDeviceName=r.capture;
        candidate.outputDeviceName=r.playback;
        candidate.sampleRate=r.rate;
        candidate.bufferSize=static_cast<int>(r.block);
        const auto input=config.getProperty("enabledInputs",{});
        const auto output=config.getProperty("enabledOutputs",{});
        plan.recovery=AudioSetupRecoveryPolicy::select(sameEndpoint,priorOpen,
            r.backend=="ASIO",input.isArray()&&output.isArray());
        if(r.assignMain && (!plan.recovery.reuseActivePins || output.getArray()->size()!=2)) {
            plan.error="Select exactly two inspected playback pins for MAIN.";
            return plan;
        }

        const bool outputOnly=separate&&r.capture.isEmpty();
        if(plan.recovery.reuseActivePins) {
            if(!parsePinMasks(input,output,plan.originalInputNames.size(),
                             plan.originalOutputNames.size(),outputOnly,
                             candidate.inputChannels,candidate.outputChannels)) {
                plan.error="Select real input pins (or none) and >=2 verified playback pins";
                return plan;
            }
            candidate.useDefaultInputChannels=false;
            candidate.useDefaultOutputChannels=false;
        } else {
            // Do not enable a default microphone for a 0-IN request.
            candidate.useDefaultInputChannels=!outputOnly;
            if(outputOnly)candidate.inputChannels.clear();
            candidate.useDefaultOutputChannels=true;
        }

        if(sameEndpoint && priorOpen) {
            // A currently negotiated format is evidence even if a vendor
            // omits it from its advertised alternatives. Pin-only edits must
            // retain that exact format rather than silently requesting AUTO.
            if(r.rate>0&&r.rate!=device->getCurrentSampleRate()&&
               !device->getAvailableSampleRates().contains(r.rate))
                plan.error="Requested sample rate not reported by selected device";
            else if(r.block>0&&candidate.bufferSize!=device->getCurrentBufferSizeSamples()&&
                    !device->getAvailableBufferSizes().contains(candidate.bufferSize))
                plan.error="Requested buffer size not reported by selected device";
        }
        return plan;
    }

    enum class Outcome { rejected, accepted, needsRemap, topologyChanged, restored, recoveryRequired };
    struct ApplyResult {
        Outcome state;
        juce::String error;
        bool canReusePins=false;
    };
    struct BindResult {
        juce::String error;
        bool needsRemap=false;
        bool canReusePins=false;
    };
    struct ApplyHooks {
        std::function<bool()> prepareReplacement;
        std::function<void()> suspend;
        std::function<BindResult(BindingMode)> bind;
        std::function<void(const juce::String&,const juce::String&)> record;
    };

    // Only accepted driver readback reaches mixer binding. A remap or
    // changed pin topology retains the muted device instead of replaying
    // stale indices; other failures attempt one verified rollback.
    template<class Bind,class Rollback>
    static ApplyResult complete(juce::String error,bool topologyChanged,
                                Bind&& bind,Rollback&& rollback) {
        if(topologyChanged)return {Outcome::topologyChanged,std::move(error)};
        if(error.isEmpty()) {
            const auto bound=bind();
            if(bound.needsRemap)return {Outcome::needsRemap,bound.error,bound.canReusePins};
            if(bound.error.isEmpty())return {Outcome::accepted,{}};
            error=bound.error;
        }
        return {rollback()?Outcome::restored:Outcome::recoveryRequired,std::move(error)};
    }

    ApplyResult execute(juce::AudioDeviceManager& manager,const ApplyPlan& plan,
                        const ApplyHooks& hooks) {
        if(activePurpose()!=Purpose::explicitApply)
            return {Outcome::rejected,"Audio device Apply requires the active transaction"};
        if(plan.error.isNotEmpty() || plan.requested.confirmRetainedPinMapping)
            return {Outcome::rejected,plan.error.isNotEmpty()?plan.error:
                juce::String("Retained pin confirmation requires the current device")};
        if(!hooks.prepareReplacement())
            return {Outcome::rejected,"Failed to prepare an explicit replacement for the missing device"};

        hooks.suspend();
        const auto& r=plan.requested;
        auto candidate=plan.candidate;
        const auto phase=[&](const juce::String& message) {
            hooks.record("CUSTOM_AUDIO_PHASE",message);
        };
        if(plan.recovery.recreateDevice && manager.getCurrentAudioDeviceType()==r.backend) {
            hooks.record("CUSTOM_AUDIO_RECREATE",
                r.backend+" | "+(plan.recovery.priorKnownOpen?"REFRESH_ASIO_PINS":"CLOSED_DEVICE_RECOVERY"));
            manager.closeAudioDevice();
        }
        phase("APPLY | "+r.backend);
        auto error=openSelected(manager,r.backend,candidate,false,phase);
        phase("VERIFY_READBACK | "+r.backend);

        auto* opened=manager.getCurrentAudioDevice();
        bool changedPins=error.isEmpty() && plan.recovery.verifyAsioPinIdentity &&
            opened && opened->isOpen() &&
            (opened->getInputChannelNames()!=plan.originalInputNames ||
             opened->getOutputChannelNames()!=plan.originalOutputNames);
        const bool refreshAsio4All=r.backend=="ASIO" && r.playback.startsWithIgnoreCase("ASIO4ALL") &&
            plan.recovery.reuseActivePins && (changedPins || error.isNotEmpty());
        bool explicitPairStillIdentified=r.assignMain;
        if(refreshAsio4All) {
            // Translate physical names before the one bounded fresh open.
            const auto freshInputs=changedPins?opened->getInputChannelNames():juce::StringArray{};
            const auto freshOutputs=changedPins?opened->getOutputChannelNames():juce::StringArray{};
            const auto capturePins=changedPins?translateNamedPins(
                candidate.inputChannels,plan.originalInputNames,freshInputs):std::nullopt;
            const auto playbackPins=changedPins?translateNamedPins(
                candidate.outputChannels,plan.originalOutputNames,freshOutputs):std::nullopt;
            candidate.useDefaultInputChannels=!capturePins && !candidate.inputChannels.isZero();
            candidate.useDefaultOutputChannels=!playbackPins;
            if(capturePins)candidate.inputChannels=*capturePins;
            if(playbackPins)candidate.outputChannels=*playbackPins;
            explicitPairStillIdentified=r.assignMain && playbackPins.has_value();
            phase("REFRESH_ASIO4ALL_INVENTORY");
            error=openSelected(manager,r.backend,candidate,true,phase);
            opened=manager.getCurrentAudioDevice();
            changedPins=error.isEmpty() && opened && opened->isOpen() &&
                ((capturePins && opened->getInputChannelNames()!=freshInputs) ||
                 (playbackPins && opened->getOutputChannelNames()!=freshOutputs));
        }
        if(changedPins)
            error="ASIO pin layout changed. Output remains muted; review the new playback pins.";
        if(error.isEmpty() && !matchesApplied(r,candidate,inspectApplied(manager)))
            error="Driver readback mismatch; device setting was not accepted";

        return complete(std::move(error),changedPins,[&] {
            phase("BIND_MIXER | "+r.backend);
            return hooks.bind(explicitPairStillIdentified?BindingMode::verifiedMainPinApply:
                                                         BindingMode::verifiedDeviceApply);
        },[&] {
            hooks.suspend();
            phase("ROLLBACK | "+plan.previousBackend);
            if(!plan.recovery.priorKnownOpen || plan.previousBackend.isEmpty() ||
               openSelected(manager,plan.previousBackend,plan.before,false,phase).isNotEmpty() ||
               manager.getCurrentAudioDeviceType()!=plan.previousBackend)
                return false;
            return matchesRollback(plan.before,plan.previousBackend,plan.originalInputNames,
                                    plan.originalOutputNames,inspectRollback(manager)) &&
                   hooks.bind(BindingMode::exact).error.isEmpty();
        });
    }

private:
    Purpose purpose_=Purpose::none; // JUCE message thread; one driver transaction
};
} // namespace armix::device
