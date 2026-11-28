// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GraphicEq31Core.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
#include <BinaryData.h>
#endif
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <limits>
#include <vector>
#include <utility>

namespace armix::builtin_fx {

class GraphicEq31Processor final : public juce::AudioProcessor,
                                   public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.graphic-eq31.v1";
    static constexpr int stateMagic=0x47455131; // GEQ1, independent of PEQ
    struct Completion {
        std::uint64_t requestId=0;
        bool applied=false;
    };
    GraphicEq31Processor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input",juce::AudioChannelSet::stereo(),true)
              .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
          params_(*this,nullptr,"GEQ31",makeParameters()) {
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            bandValues_[i]=params_.getRawParameterValue(paramId(i));
        levelValue_=params_.getRawParameterValue("levelDb");
    }

    const juce::String getName() const override {return "Graphic EQ 31";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Manual";}
    void changeProgramName(int,const juce::String&) override {}
    // Product GUI now renders ONLY in the approved /preview/ WebView2.
    // Keep the old JUCE Editor class compiled for source-state regression,
    // but prevent any native HWND route from opening a second renderer.
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate,int /*maxFrames*/) override {
        ready_=core_.prepare(sampleRate);
        if(!ready_)return;
        for(std::size_t i=0;i<bandValues_.size();++i)
            core_.setGain(i,static_cast<double>(bandValues_[i]->load(std::memory_order_relaxed)));
        levelGain_.reset(sampleRate,0.015); // 15 ms, no clicks or lookahead
        lastLevelDb_=levelValue_->load(std::memory_order_acquire);
        rtLevelDb_.store(lastLevelDb_,std::memory_order_release);
        levelGain_.setCurrentAndTargetValue(
            core_.linearGainForDb(lastLevelDb_));
        peak_.store(0.f,std::memory_order_relaxed);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    void consumeRealtimeEdits() noexcept override {
        // Regular single-band JUCE edits remain lock-free. A 31-band user
        // preset publishes one bounded even/odd generation so the audio
        // callback cannot observe a partially recalled EQ curve.
        if(!ready_)return;
        const auto before=presetWriteSeq_.load(std::memory_order_acquire);
        if((before&1u)!=0u)return;
        std::array<float,GraphicEq31Core::bandCount> values{};
        for(std::size_t i=0;i<values.size();++i)
            values[i]=bandValues_[i]->load(std::memory_order_relaxed);
        const float levelDb=levelValue_->load(std::memory_order_relaxed);
        const auto after=presetWriteSeq_.load(std::memory_order_acquire);
        if(before!=after||(after&1u)!=0u)return;
        for(std::size_t i=0;i<values.size();++i)
            (void)core_.setGain(i,values[i]);
        // Same accepted generation as all 31 gains; never publish a
        // partially recalled Output Level ahead of the fader curve.
        rtLevelDb_.store(levelDb,std::memory_order_release);
        // The completed callback, NOT a WebView click, ACKs a native edit.
        // All parameters were published by CONTROL before the request token;
        // audio has now consumed the same atomic snapshot without allocating.
        std::uint64_t requestId=0;
        if(completionGeq_.canPush() && queuedGeq_.pop(requestId))
            (void)completionGeq_.push({requestId,true});
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return ready_&&controlEditsSettled()&&
               (presetWriteSeq_.load(std::memory_order_acquire)&1u)==0u &&
            queuedGeq_.empty() && completionGeq_.empty();
    }
    bool supportsExactIdle() const noexcept override {return true;}
    bool tryExactIdle(const float* left,const float* right,int frames) noexcept override {
        // Cheap rejection first: non-flat EQ, an active output ramp or
        // pending ACK always uses the ordinary DSP path, including tails.
        if(!ready_||frames<=0||!core_.flatSettled()||levelGain_.isSmoothing()||
           levelGain_.getCurrentValue()!=1.f||levelGain_.getTargetValue()!=1.f||
           lastLevelDb_!=0.f||!queuedGeq_.empty()||!completionGeq_.empty())return false;
        const auto before=presetWriteSeq_.load(std::memory_order_acquire);
        if(before&1u)return false;
        for(auto* value:bandValues_)
            if(value->load(std::memory_order_relaxed)!=0.f)return false;
        if(levelValue_->load(std::memory_order_relaxed)!=0.f||
           presetWriteSeq_.load(std::memory_order_acquire)!=before||
           !queuedGeq_.empty())return false;
        float peak=0.f;
        if(!core_.tryUnityPeak(left,right,static_cast<std::size_t>(frames),peak))return false;
        // Equivalent to settled unity multiplication, with the same meter.
        // No edit ACK is consumed here; first changed block uses processBlock.
        peak_.store(peak,std::memory_order_relaxed);
        return true;
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        const auto count=pcm.getNumSamples();
        auto* l=pcm.getWritePointer(0);
        auto* r=pcm.getWritePointer(1);
        // One explicit OUTPUT LEVEL after the 31-band EQ, in the existing
        // render graph. 15ms gain smoothing is sample-accurate and RT-safe.
        core_.processStereo(l,r,static_cast<std::size_t>(count));
        const float acceptedLevel=rtLevelDb_.load(std::memory_order_acquire);
        if(acceptedLevel!=lastLevelDb_) {
            levelGain_.setTargetValue(core_.linearGainForDb(acceptedLevel));
            lastLevelDb_=acceptedLevel;
        }
        float peak=0.f;
        for(int n=0;n<count;++n) {
            const float gain=levelGain_.getNextValue();
            const float outL=l[n]*gain, outR=r[n]*gain;
            // Extreme finite upstream PCM can overflow float only AFTER
            // the operator's explicit Output gain. Contain the resulting
            // non-finite sample before the rack/mixer/DAC; do not clamp
            // legitimate finite >0 dBFS peaks or hide headroom errors.
            l[n]=std::isfinite(outL)?outL:0.f;
            r[n]=std::isfinite(outR)?outR:0.f;
            peak=std::max(peak,std::max(std::abs(l[n]),std::abs(r[n])));
        }
        peak_.store(peak,std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    bool supportsLiveStateSnapshot() const noexcept override {return true;}
    void getStateInformation(juce::MemoryBlock& destination) override {
        // CONTROL thread. Only one fixed-size, versioned state per insert.
        juce::MemoryOutputStream out(destination,false);
        // GEQ1 v4: 31 bands + one EXPLICIT Output Level (136 bytes).
        // v1/v2 obsolete hidden trims remain discarded; v3 restores unity.
        out.writeInt(stateMagic);out.writeInt(4);
        for(auto* p:bandValues_)out.writeFloat(p->load(std::memory_order_acquire));
        out.writeFloat(acceptedLevelDb());
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        constexpr int bandBytes=static_cast<int>(GraphicEq31Core::bandCount)*4;
        constexpr int v1Bytes=8+bandBytes+8; // obsolete input + output
        constexpr int v2Bytes=8+bandBytes+4; // obsolete input
        constexpr int v3Bytes=8+bandBytes;   // band-only
        constexpr int v4Bytes=8+bandBytes+4; // explicit Output Level
        if(published_||!bytes||
           (length!=v1Bytes&&length!=v2Bytes&&length!=v3Bytes&&
            length!=v4Bytes))return false;
        juce::MemoryInputStream input(bytes,static_cast<std::size_t>(length),false);
        if(input.readInt()!=stateMagic)return false;
        const int version=input.readInt();
        if((version==1&&length!=v1Bytes)||
           (version==2&&length!=v2Bytes)||
           (version==3&&length!=v3Bytes)||
           (version==4&&length!=v4Bytes)||
           (version<1||version>4))return false;
        std::array<float,GraphicEq31Core::bandCount> bands{};
        for(auto& value:bands) {
            value=input.readFloat();
            if(!std::isfinite(value)||value< -12.f||value>12.f)return false;
        }
        float explicitLevel=0.f;
        if(version==4) {
            explicitLevel=input.readFloat();
            if(!std::isfinite(explicitLevel)||
               explicitLevel< -24.f||explicitLevel>6.f)return false;
        }else {
            // Old input/output trims are validated only to reject corrupted
            // sessions, then DISCARDED, never reactivated as Output Level.
            for(int i=0;i<(version==1?2:version==2?1:0);++i) {
                const float obsoleteTrim=input.readFloat();
                if(!std::isfinite(obsoleteTrim)||
                   obsoleteTrim< -12.f||obsoleteTrim>12.f)return false;
            }
        }
        if(!input.isExhausted())return false;
        // Nothing mutates until the entire binary state is validated.
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            setParameter(paramId(i),bands[i]);
        setParameter("levelDb",explicitLevel);
        if(ready_)prepareToPlay(core_.sampleRate(),0);
        return true;
    }

    void setStateInformation(const void* bytes,int count) override {
        (void)restoreUnpublishedState(bytes,count);
    }
    // CONTROL/UI snapshot of the accepted 31 APVTS targets. Never reads
    // a transient slider pixel position and never touches realtime history.
    std::array<float,GraphicEq31Core::bandCount> acceptedBands() const noexcept {
        std::array<float,GraphicEq31Core::bandCount> bands{};
        for(std::size_t i=0;i<bands.size();++i)
            bands[i]=bandValues_[i]->load(std::memory_order_acquire);
        return bands;
    }
    bool setBandsFromPreset(
            const std::array<float,GraphicEq31Core::bandCount>& values,
            float levelDb=0.f) {
        for(const auto db:values)
            if(!std::isfinite(db)||db < -12.f||db > 12.f)return false;
        if(!std::isfinite(levelDb)||levelDb< -24.f||levelDb>6.f)return false;
        // CONTROL thread only. Two atomic increments fence one committed
        // 31-band target array; realtime skips incomplete batches.
        presetWriteSeq_.fetch_add(1u,std::memory_order_acq_rel);
        for(std::size_t i=0;i<values.size();++i)
            setBandFromUi(i,values[i]);
        setLevelFromUi(levelDb);
        presetWriteSeq_.fetch_add(1u,std::memory_order_release);
        return true;
    }
    // One message-thread writer and ONE audio callback reader. This narrow
    // token queue does not carry sound data or execute JUCE parameter setters
    // on the realtime thread. Band/Output edits use the same APVTS target
    // cache as native/editor factory recall, preserving legacy .smix states.
    bool queueBandEdit(std::size_t index,float db,std::uint64_t requestId) {
        if(!ready_||requestId==0||index>=GraphicEq31Core::bandCount||
           !std::isfinite(db)||db<-12.f||db>12.f||!queuedGeq_.canPush())
            return false;
        setBandFromUi(index,db);
        return trackControlEdit(queuedGeq_.push(requestId));
    }
    bool queueOutputEdit(float db,std::uint64_t requestId) {
        if(!ready_||requestId==0||!std::isfinite(db)||
           db<-24.f||db>6.f||!queuedGeq_.canPush())return false;
        setLevelFromUi(db);
        return trackControlEdit(queuedGeq_.push(requestId));
    }
    bool queueCurveEdit(
            const std::array<float,GraphicEq31Core::bandCount>& bands,
            float outputDb,std::uint64_t requestId) {
        if(!ready_||requestId==0||!queuedGeq_.canPush()||
           !setBandsFromPreset(bands,outputDb))return false;
        return trackControlEdit(queuedGeq_.push(requestId));
    }
    bool pollCompletion(Completion& result) noexcept {
        return settleControlEdit(completionGeq_.pop(result));
    }
    float acceptedLevelDb() const noexcept {
        return levelValue_->load(std::memory_order_acquire);
    }
    void setLevelFromUi(float db) {
        if(std::isfinite(db)&&db>=-24.f&&db<=6.f)
            setParameter("levelDb",db);
    }
    juce::AudioProcessorValueTreeState& parameters() noexcept {return params_;}
    float observedPeak() const noexcept {return peak_.load(std::memory_order_relaxed);}
    void setBandFromUi(std::size_t index,float db) {
        if(index<GraphicEq31Core::bandCount&&std::isfinite(db)&&db>=-12.f&&db<=12.f)
            setParameter(paramId(index),db);
    }
    void setFlatFromUi() {
        // Flat preserves the currently accepted Output Level, while all 31
        // bands pass through the same bounded preset generation fence.
        const std::array<float,GraphicEq31Core::bandCount> flat{};
        (void)setBandsFromPreset(flat,acceptedLevelDb());
    }
private:
    static juce::String paramId(std::size_t i) {
        return "band"+juce::String(static_cast<int>(i));
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            result.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{paramId(i),1},
                juce::String(graphicEq31Labels[i])+" Hz",
                juce::NormalisableRange<float>(-12.f,12.f,.1f),0.f));
        result.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"levelDb",1},"Output Level",
            juce::NormalisableRange<float>(-24.f,6.f,.1f),0.f));
        return result;
    }
    void setParameter(const juce::String& id,float db) {
        if(auto* p=dynamic_cast<juce::RangedAudioParameter*>(params_.getParameter(id))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(db));
            p->endChangeGesture();
        }
    }
    juce::AudioProcessorValueTreeState params_;
    GraphicEq31Core core_;
    std::array<std::atomic<float>*,GraphicEq31Core::bandCount> bandValues_{};
    std::atomic<float>* levelValue_=nullptr;
    std::atomic<float> rtLevelDb_{0.f};
    juce::SmoothedValue<float,juce::ValueSmoothingTypes::Linear> levelGain_;
    float lastLevelDb_=0.f;
    std::atomic<float> peak_{0.f};
    std::atomic<std::uint32_t> presetWriteSeq_{0u};
    FxSpsc<std::uint64_t,16> queuedGeq_{};
    FxSpsc<Completion,16> completionGeq_{};
    bool published_=false,ready_=false;
};

// Shared native zero-detent control: 31 faders plus Output Level.
// JUCE slider wheel already owns its host automation gesture and calls
// snapValue(value, notDragging); never create parallel editor param state.
class GraphicEq31DetentSlider final : public juce::Slider {
public:
    void setZeroDetent(double captureDb,double releaseDb) noexcept {
        jassert(captureDb>0.0 && releaseDb>captureDb);
        captureDb_=captureDb;
        releaseDb_=releaseDb;
    }
    void mouseDown(const juce::MouseEvent& event) override {
        detentEngaged_=std::abs(getValue())<0.001;
        wheelEscapingZero_=false;
        juce::Slider::mouseDown(event);
    }
    double snapValue(double attemptedValue,DragMode dragMode) override {
        if(dragMode==notDragging) {
            // The JUCE wheel path owns the automation gesture. A step into
            // or ACROSS 0 dB captures exactly once. Outbound steps from 0
            // must pass through the whole capture band without oscillation.
            const double current=getValue();
            if(std::abs(current)<=0.001) {
                wheelEscapingZero_=true;
                wheelEscapeDirection_=attemptedValue>0.0?1:
                                      attemptedValue<0.0?-1:0;
                return juce::Slider::snapValue(attemptedValue,dragMode);
            }
            if(wheelEscapingZero_ &&
               current*wheelEscapeDirection_>0.0 &&
               attemptedValue*wheelEscapeDirection_>0.0 &&
               std::abs(attemptedValue)>std::abs(current)) {
                if(std::abs(attemptedValue)>captureDb_)
                    wheelEscapingZero_=false;
                return juce::Slider::snapValue(attemptedValue,dragMode);
            }
            wheelEscapingZero_=false;
            if(std::abs(attemptedValue)<=captureDb_ ||
               (current<0.0&&attemptedValue>0.0) ||
               (current>0.0&&attemptedValue<0.0))
                return 0.0;
            return juce::Slider::snapValue(attemptedValue,dragMode);
        }
        wheelEscapingZero_=false;
        // Drag hysteresis is UI-only; accepted APVTS retains 0.1dB steps.
        if(std::abs(attemptedValue)<=(detentEngaged_?releaseDb_:captureDb_)) {
            detentEngaged_=true;
            return 0.0;
        }
        detentEngaged_=false;
        return juce::Slider::snapValue(attemptedValue,dragMode);
    }
private:
    double captureDb_=1.1,releaseDb_=1.8;
    bool detentEngaged_=false;
    bool wheelEscapingZero_=false;
    int wheelEscapeDirection_=0;
};

class GraphicEq31Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    // Readback from the existing mixer-authoritative global/rack/insert gates.
    struct HostBypassState {
        bool valid=false;
        bool slotBypassed=false;
        bool rackBypassed=false;
        bool globalBypassed=false;
        bool effectiveBypassed() const noexcept {
            return valid && (slotBypassed || rackBypassed || globalBypassed);
        }
    };
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<HostBypassState()> readback) {
        requestBypass_=std::move(request);
        readHostPower_=std::move(readback);
        refreshHostPower();
    }
    void synchronizeHostPower() {refreshHostPower();}

    struct UserPresetEntry {juce::String id,name;};
    // Host owns disk IO, exact graph lease and typed .armixfx validation.
    // The editor is only a view; no parallel preset file store.
    void bindUserPresetService(
        std::function<std::vector<UserPresetEntry>()> enumerate,
        std::function<juce::String(const juce::String&)> saveNew,
        std::function<bool(const juce::String&)> recall) {
        listUserPresets_=std::move(enumerate);
        saveUserPreset_=std::move(saveNew);
        recallUserPreset_=std::move(recall);
        save_.setEnabled(static_cast<bool>(saveUserPreset_));
        refreshPresetList();
    }

public:
    explicit GraphicEq31Editor(GraphicEq31Processor& processor)
        : juce::AudioProcessorEditor(processor), processor_(processor) {
        setLookAndFeel(&skin_);
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
        // Decode supplied genuine brushed-aluminium texture ONCE on UI init.
        const auto originalMetal=juce::ImageCache::getFromMemory(
            BinaryData::geq31_brushed_metal_jpg,
            BinaryData::geq31_brushed_metal_jpgSize);
        // One-time photo-derived satin titanium; wood-like macro streaks
        // attenuated while subtle fine horizontal brushing is preserved.
        brushedMetal_=makeSatinBrushedMetal(originalMetal);
#endif
        // The existing ArSonMix rack/ChannelId owns this JUCE editor window.
        // The owner's original grouped SVG provides EVERY track and thumb.
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
            auto& slider=sliders_[i];
            slider.setComponentID("geq31-band-"+juce::String(static_cast<int>(i)));
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setDoubleClickReturnValue(true,0.);
            slider.setScrollWheelEnabled(true);
            slider.setZeroDetent(1.1,1.8); // 31-band 0dB detent
            slider.setTooltip(juce::String(graphicEq31Labels[i])+
                " Hz  |  -12 ... +12 dB  |  Double-click: 0 dB");
            // Preset identity is reconciled from all 31 accepted parameter
            // values on the message-thread refresh, not from one async
            // SliderAttachment notification that might arrive mid-preset.
            bandSurface_.addAndMakeVisible(slider);
            attachments_.emplace_back(std::make_unique<Attachment>(
                processor_.parameters(),"band"+juce::String(static_cast<int>(i)),slider));
        }

        // New owner-selected black PNG is static. Pointer/outer arc are coded
        // in JUCE LookAndFeel, not embedded in this asset.
        level_.setComponentID("geq31.output-level");
        level_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        level_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        level_.setDoubleClickReturnValue(true,0.);
        level_.setScrollWheelEnabled(true);
        level_.setZeroDetent(0.65,1.2); // rotary unity detent
        level_.setTooltip("Output Level -24 ... +6 dB (double-click: 0)");
        level_.onValueChange=[this]{
            repaint(getWidth()-rightRailWidth-9,86,rightRailWidth+9,166);
        };
        levelAttachment_=std::make_unique<Attachment>(
            processor_.parameters(),"levelDb",level_);
        addAndMakeVisible(level_);
        viewport_.setViewedComponent(&bandSurface_,false);
        viewport_.setScrollBarsShown(false,true);
        viewport_.setScrollBarThickness(7);
        addAndMakeVisible(viewport_);

        // Factory + real host-owned .armixfx user presets share one selector.
        // Save/read/recall are disabled until a valid host lease is bound.
        preset_.setComponentID("geq31.preset");
        preset_.setJustificationType(juce::Justification::centredLeft);
        preset_.setTooltip("Factory and saved Graphic EQ 31 presets");
        preset_.onChange=[this]{selectPreset(preset_.getSelectedId());};
        refreshPresetList();
        addAndMakeVisible(preset_);

        save_.setComponentID("geq31.save");
        save_.setButtonText("Save");
        save_.setTooltip("Save current 31-band EQ as a new user preset");
        save_.setEnabled(false);
        save_.onClick=[this]{promptSavePreset();};
        addAndMakeVisible(save_);

        power_.setComponentID("geq31.host-bypass");
        power_.setButtonText(juce::String::fromUTF8("⏻"));
        power_.setClickingTogglesState(false);
        power_.setEnabled(false); // no mixer lease: no fake power authority
        power_.setTooltip("FX state unavailable");
        power_.onClick=[this]{
            if(!readHostPower_||!requestBypass_)return;
            const auto before=readHostPower_();
            if(!before.valid||before.globalBypassed||before.rackBypassed) {
                refreshHostPower();
                return;
            }
            (void)requestBypass_(!before.slotBypassed);
            refreshHostPower(); // reflect actual accepted host state only
        };
        addAndMakeVisible(power_);

        close_.setComponentID("geq31.close");
        close_.setButtonText({}); // shared built-in FX vector close icon
        close_.setTooltip("Close Graphic EQ 31");
        close_.onClick=[this]{
            if(auto* window=dynamic_cast<juce::DocumentWindow*>(
                   getTopLevelComponent()))
                window->setVisible(false); // retain mixer FX & preset state
        };
        addAndMakeVisible(close_);

        flat_.setComponentID("geq31.flat");
        flat_.setButtonText("Flat");
        flat_.setTooltip("Reset all 31 EQ bands to 0 dB");
        flat_.onClick=[this]{
            selectedUserId_.clear();
            processor_.setFlatFromUi(); // Preserve the output level
            preset_.setSelectedId(matchingFactoryPreset(),
                                  juce::dontSendNotification);
        };
        addAndMakeVisible(flat_);
        setResizable(true,true);
        // Hardware stays 1:1 as authored SVG; only the 31-band viewport
        // renders at 80%. Header/control sizes are deliberately unchanged.
        // Descendant listener is passive on actual controls and active only
        // on owner-blank background; mixer WebView2 CSS stays untouched.
        addMouseListener(this,true);
        setResizeLimits(860,305,1760,305);
        setSize(1430,305);
        startTimerHz(6); // GUI-only external host readback; no audio work
    }

    ~GraphicEq31Editor() override {
        removeMouseListener(this);
        dragWindow_=nullptr;
        stopTimer();
        setLookAndFeel(nullptr);
    }
    // Move only the existing host-owned floating DocumentWindow. Clicking
    // sliders/knob/buttons/preset never arms window dragging.
    void mouseDown(const juce::MouseEvent& event) override {
        if(!event.mods.isLeftButtonDown()||!isDragSurface(event))return;
        if(auto* window=dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
            dragWindow_=window;
            windowDragger_.startDraggingComponent(window,event);
        }
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if(!dragWindow_ || !event.mods.isLeftButtonDown()
            || !isDragSurface(event))return;
        windowDragger_.dragComponent(dragWindow_.getComponent(),event,nullptr);
    }
    void mouseUp(const juce::MouseEvent&) override {dragWindow_=nullptr;}


    void paint(juce::Graphics& g) override {
        const int width=getWidth(),height=getHeight();
        // Actual operator-supplied horizontal brushed-aluminium texture.
        // One decode in the editor constructor; one cached Image draw here.
        if(brushedMetal_.isValid())
            g.drawImageWithin(brushedMetal_,0,0,width,height,
                juce::RectanglePlacement::stretchToFit);
        else
            g.fillAll(juce::Colour::fromRGB(160,161,161));
        g.setGradientFill(juce::ColourGradient(
            juce::Colours::white.withAlpha(.045f),0.f,0.f,
            juce::Colours::black.withAlpha(.06f),0.f,static_cast<float>(height),false));
        g.fillRect(getLocalBounds());
        g.setColour(juce::Colour::fromRGB(94,102,106).withAlpha(.72f));
        g.drawRect(getLocalBounds().reduced(1),1);
        g.setColour(juce::Colour::fromRGB(33,46,52));
        g.setFont(inter(17.f));
        g.drawText("Graphic EQ 31",19,13,250,31,juce::Justification::centredLeft);
        g.setFont(inter(10.f));
        g.setColour(juce::Colour::fromRGB(63,80,88));
        if(width>=920)
            g.drawText("ARSONMIX  /  1/3 OCTAVE  /  +/-12 dB",
                       216,22,310,20,juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(76,90,97).withAlpha(.45f));
        g.drawHorizontalLine(53,0.f,static_cast<float>(width));
        // Two calibrated scales, adjacent to the first and last visible
        // track: 11px Inter with matching frequency-number colour.
        const auto majorTickY=[this](float sourceY) noexcept {
            return juce::roundToInt(
                static_cast<float>(viewport_.getY())+
                sourceY*static_cast<float>(sliders_[0].getHeight())/2042.f);
        };
        g.setColour(juce::Colour(0xff344047));
        g.setFont(inter(11.f).boldened());
        for(const auto& tick:std::array<std::pair<float,const char*>,3>{{
                {208.75f,"+12"},{918.33f,"0"},{1627.92f,"-12"}}}) {
            const int labelY=majorTickY(tick.first)-9;
            g.drawText(tick.second,4,labelY,36,19,
                       juce::Justification::centredRight);
            g.drawText(tick.second,viewport_.getRight()+6,labelY,36,19,
                       juce::Justification::centredLeft);
        }
        g.setColour(juce::Colour(0xff344047));
        g.setFont(inter(10.f).boldened());
        g.drawText("OUTPUT",getWidth()-rightRailWidth-14,104,
                   rightRailWidth,17,juce::Justification::centred);
        g.setFont(inter(10.f));
        g.drawText(juce::String(level_.getValue(),1)+" dB",
                   getWidth()-rightRailWidth-14,224,
                   rightRailWidth,20,juce::Justification::centred);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // Same ImmerSIB visual language; from host readback only.
        const bool bypassed=hostBypassed_.value_or(false);
        const bool unavailable=hostLeaseWasValid_&&!hostBypassed_.has_value();
        if(!bypassed&&!unavailable)return;
        juce::Graphics::ScopedSaveState state(g);
        // Dim child SVG tracks / native sliders as well as background.
        // Keep Power/Close visible and accessible during bypass.
        g.excludeClipRegion(power_.getBounds());
        g.excludeClipRegion(close_.getBounds());
        g.setColour(juce::Colour(0xb803060a));
        g.fillRect(0,54,getWidth(),getHeight()-54);
        const auto label=juce::Rectangle<int>(
            viewport_.getX(),118,viewport_.getWidth(),115);
        auto font=juce::Font(juce::FontOptions(
            bypassed?51.f:34.f,juce::Font::bold));
        font.setExtraKerningFactor(0.055f);
        g.setFont(font);
        g.setColour(juce::Colour(0xb8000307));
        g.drawText(bypassed?"BYPASSED":"FX UNAVAILABLE",
                   label.translated(0,3),juce::Justification::centred,false);
        g.setColour(bypassed?juce::Colour(0xeeffa53c):
                    juce::Colour(0xffd0dce1));
        g.drawText(bypassed?"BYPASSED":"FX UNAVAILABLE",
                   label,juce::Justification::centred,false);
    }
    void resized() override {
        // Compact 80%-scale hardware: 31 x 40 px = 1240px, 229px tall.
        // Header is unchanged; 2px from divider to art, 12px bottom inset.
        constexpr int x0=45, artY=55, artH=229;
        viewport_.setBounds(x0,artY,
            juce::jmax(1,getWidth()-rightRailWidth-2*x0),238);
        level_.setBounds(getWidth()-106,127,84,84); // another 8px left
        bandSurface_.setSize(static_cast<int>(
            GraphicEq31Core::bandCount)*bandCellWidth,artH);
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            sliders_[i].setBounds(static_cast<int>(i)*bandCellWidth,0,
                                  bandCellWidth,artH);
        // Approved 24px-tall header positions are deliberately untouched.
        const int r=getWidth();
        preset_.setBounds(r-349,16,152,24);
        save_.setBounds(r-189,16,56,24);
        flat_.setBounds(r-125,16,44,24);
        power_.setBounds(r-73,16,24,24);
        close_.setBounds(r-41,16,24,24);
    }

private:
    bool isDragSurface(const juce::MouseEvent& event) const noexcept {
        // Original target identity avoids stealing mouse events from sliders
        // even on transparent/blank pixels within their SVG bounds.
        const auto* origin=event.originalComponent;
        return origin==this || origin==&viewport_ || origin==&bandSurface_;
    }
    void refreshHostPower() {
        const auto state=readHostPower_?readHostPower_():HostBypassState{};
        const bool bypassed=state.effectiveBypassed();
        // Unbound is not bypass; expired host lease is unavailable.
        const bool changed=(state.valid!=hostBypassed_.has_value())||
                           (state.valid&&*hostBypassed_!=bypassed);
        if(state.valid){hostLeaseWasValid_=true;hostBypassed_=bypassed;}
        else hostBypassed_.reset();
        const bool active=state.valid&&!bypassed;
        power_.setEnabled(state.valid&&requestBypass_&&
                          !state.globalBypassed&&!state.rackBypassed);
        power_.setToggleState(active,juce::dontSendNotification);
        power_.setTooltip(!state.valid?"FX instance unavailable":
            state.globalBypassed?"Global FX bypassed in mixer":
            state.rackBypassed?"FX Rack bypassed in mixer":
            state.slotBypassed?"Enable Graphic EQ 31":"Bypass Graphic EQ 31");
        power_.setColour(juce::TextButton::textColourOffId,
            active?juce::Colour::fromRGB(24,105,89):
                   juce::Colour::fromRGB(87,91,92));
        if(changed)repaint();
    }
    static float presetGain(int choice,float hz) noexcept {
        if(choice==3) // Voice presence, subtract mud without hype.
            return hz<100.f?-2.5f:hz<270.f?-1.5f:
                   hz>=1800.f&&hz<5600.f?2.f:0.f;
        if(choice==4)
            return hz<=160.f?2.f:hz>=5000.f?-1.f:0.f;
        if(choice==5)
            return hz<=125.f?-3.f:hz>=200.f&&hz<=500.f?-1.f:
                   hz>=1600.f&&hz<=4000.f?2.5f:0.f;
        return 0.f;
    }
    int matchingFactoryPreset() const {
        if(std::abs(processor_.acceptedLevelDb())>.06f)return 1;
        for(int preset=2;preset<=5;++preset) {
            bool match=true;
            for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
                const auto* value=processor_.parameters().getRawParameterValue(
                    "band"+juce::String(static_cast<int>(i)));
                if(!value||std::abs(value->load(std::memory_order_relaxed)-
                           presetGain(preset,graphicEq31Hz[i]))>.06f) {
                    match=false;break;
                }
            }
            if(match)return preset;
        }
        return 1;
    }
    void timerCallback() override {
        refreshHostPower();
        int accepted=matchingFactoryPreset();
        if(selectedUserId_.isNotEmpty()) {
            const auto current=processor_.acceptedBands();
            bool matches=true;
            for(std::size_t i=0;i<current.size();++i)
                if(std::abs(current[i]-savedUserBands_[i])>.06f) {
                    matches=false;break;
                }
            matches=matches&&std::abs(
                processor_.acceptedLevelDb()-savedUserLevelDb_)<=.06f;
            if(matches) {
                for(std::size_t i=0;i<userPresets_.size();++i)
                    if(userPresets_[i].id==selectedUserId_)
                        accepted=static_cast<int>(100+i);
            }else selectedUserId_.clear();
        }
        if(preset_.getSelectedId()!=accepted)
            preset_.setSelectedId(accepted,juce::dontSendNotification);
    }
    void refreshPresetList() {
        userPresets_=listUserPresets_?listUserPresets_():
            std::vector<UserPresetEntry>{};
        preset_.clear(juce::dontSendNotification);
        preset_.addItem("Custom",1);
        preset_.addItem("Flat",2);
        preset_.addItem("Vocal Presence",3);
        preset_.addItem("Warm",4);
        preset_.addItem("Speech Clarity",5);
        if(!userPresets_.empty())preset_.addSeparator();
        for(std::size_t i=0;i<userPresets_.size();++i)
            preset_.addItem(userPresets_[i].name,static_cast<int>(100+i));
        int selected=matchingFactoryPreset();
        for(std::size_t i=0;i<userPresets_.size();++i)
            if(userPresets_[i].id==selectedUserId_)
                selected=static_cast<int>(100+i);
        preset_.setSelectedId(selected,juce::dontSendNotification);
    }
    void selectPreset(int choice) {
        if(choice>=100) {
            const auto index=static_cast<std::size_t>(choice-100);
            if(index>=userPresets_.size()||!recallUserPreset_||
               !recallUserPreset_(userPresets_[index].id)) {
                preset_.setSelectedId(matchingFactoryPreset(),
                                      juce::dontSendNotification);
                return;
            }
            selectedUserId_=userPresets_[index].id;
            savedUserBands_=processor_.acceptedBands();
            savedUserLevelDb_=processor_.acceptedLevelDb();
            return;
        }
        if(choice>=2&&choice<=5) {
            selectedUserId_.clear();
            applyFactoryPreset(choice);
        }
    }
    void promptSavePreset() {
        if(!saveUserPreset_)return;
        auto* dialog=new juce::AlertWindow("Save Graphic EQ 31 preset",
            "Name this EQ setting",juce::MessageBoxIconType::NoIcon,this);
        dialog->addTextEditor("presetName","","Preset name:");
        dialog->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<GraphicEq31Editor> weak(this);
        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create([weak,dialog](int result) {
                if(result==1&&weak!=nullptr)
                    weak->saveNamedPreset(dialog->getTextEditorContents("presetName"));
            }),true);
    }
    void saveNamedPreset(const juce::String& rawName) {
        if(!saveUserPreset_)return;
        const auto name=rawName.trim();
        const auto id=saveUserPreset_(name);
        if(id.isEmpty())return; // Host displays the validation error.
        selectedUserId_=id;
        savedUserBands_=processor_.acceptedBands();
        savedUserLevelDb_=processor_.acceptedLevelDb();
        refreshPresetList();
    }
    void applyFactoryPreset(int choice) {
        if(choice<2||choice>5)return;
        std::array<float,GraphicEq31Core::bandCount> gains{};
        for(std::size_t i=0;i<gains.size();++i)
            gains[i]=presetGain(choice,graphicEq31Hz[i]);
        // Factory recall and user recall must be equally atomic at the
        // callback boundary, including the post-EQ Output Level.
        (void)processor_.setBandsFromPreset(gains,0.f);
    }
    // 512x256 image generated once per editor, never in the PCM callback.
    // The supplied original metal is still embedded unchanged. Only 8% of
    // its large-scale grain is used to avoid wood-like, dark horizontal bands.
    static juce::Image makeSatinBrushedMetal(const juce::Image& source) {
        constexpr int w=512,h=256;
        juce::Image result(juce::Image::RGB,w,h,false);
        juce::Image::BitmapData out(result,juce::Image::BitmapData::writeOnly);
        const auto hash=[](std::uint32_t v) noexcept {
            v^=v>>16;v*=0x7feb352du;
            v^=v>>15;v*=0x846ca68bu;
            return v^(v>>16);
        };
        for(int y=0;y<h;++y) {
            const int rowGrain=static_cast<int>(hash(
                static_cast<std::uint32_t>(y)+0x9e3779b9u)&31u)-15;
            const float sheen=6.f*std::sin(
                3.14159265f*static_cast<float>(y)/(h-1));
            for(int x=0;x<w;++x) {
                float sourceLuma=145.f;
                if(source.isValid()) {
                    const auto c=source.getPixelAt(
                        x*source.getWidth()/w,y*source.getHeight()/h);
                    sourceLuma=(static_cast<float>(c.getRed())+
                                2.f*static_cast<float>(c.getGreen())+
                                static_cast<float>(c.getBlue()))*.25f;
                }
                const int micro=static_cast<int>(hash(
                    static_cast<std::uint32_t>(x)*374761393u+
                    static_cast<std::uint32_t>(y)*668265263u)&15u)-7;
                const int gray=juce::jlimit(0,250,juce::roundToInt(
                    187.f+(sourceLuma-145.f)*.08f+
                    static_cast<float>(rowGrain)*.58f+
                    static_cast<float>(micro)*.15f+sheen));
                out.setPixelColour(x,y,juce::Colour::fromRGB(
                    static_cast<juce::uint8>(gray),
                    static_cast<juce::uint8>(gray+1),
                    static_cast<juce::uint8>(gray+3)));
            }
        }
        return result;
    }
    static juce::Font inter(float size) {return juce::Font("Inter",size,juce::Font::plain);}
    class EqSkin final : public juce::LookAndFeel_V4 {
    public:
        EqSkin() {
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            originalSvg_=juce::String::fromUTF8(BinaryData::slider_eq_grp_svg,
                static_cast<int>(BinaryData::slider_eq_grp_svgSize));
#endif
            lastGain_.fill(std::numeric_limits<float>::quiet_NaN());
            thumbGroups_.fill(nullptr);
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            knobImage_=juce::ImageCache::getFromMemory(
                BinaryData::geq31_knob_black_png,
                BinaryData::geq31_knob_black_pngSize);
#endif
            setColour(juce::TextButton::buttonColourId,juce::Colour::fromRGB(231,237,237));
            setColour(juce::TextButton::textColourOffId,juce::Colour::fromRGB(65,77,79));
        }
        juce::Font getTextButtonFont(juce::TextButton&,int) override {
            return GraphicEq31Editor::inter(11.f);
        }
        juce::Font getLabelFont(juce::Label&) override {
            return GraphicEq31Editor::inter(11.f);
        }
        juce::Font getComboBoxFont(juce::ComboBox&) override {
            return GraphicEq31Editor::inter(11.f);
        }
        void drawButtonBackground(juce::Graphics& g,juce::Button& button,
                                  const juce::Colour&,bool highlighted,bool down) override {
            auto r=button.getLocalBounds().toFloat().reduced(.6f);
            if(button.getComponentID()=="geq31.close") {
                // Approved /preview/ 24px close: navy face, red on hover/
                // press, #607d8c border and 4px corner radius.
                const auto top=down?juce::Colour(0xffef3e50):
                    highlighted?juce::Colour(0xff633840):juce::Colour(0xff3e5866);
                const auto bottom=down?juce::Colour(0xffc52d40):
                    highlighted?juce::Colour(0xff48292f):juce::Colour(0xff293e4b);
                g.setGradientFill(juce::ColourGradient(
                    top,0.f,r.getY(),bottom,0.f,r.getBottom(),false));
                g.fillRoundedRectangle(r,4.f);
                g.setColour(down?juce::Colour(0xffee8b93):
                            highlighted?juce::Colour(0xffb27078):
                                        juce::Colour(0xff607d8c));
                g.drawRoundedRectangle(r,4.f,1.f);
                return;
            }
            const bool active=button.getComponentID()=="geq31.host-bypass" &&
                              button.getToggleState();
            const auto top=active?juce::Colour::fromRGB(209,235,228):
                      juce::Colour::fromRGB(226,232,233);
            const auto bottom=active?juce::Colour::fromRGB(166,200,189):
                      juce::Colour::fromRGB(184,197,201);
            g.setGradientFill(juce::ColourGradient(
                down?bottom:top,0.f,r.getY(),
                down?top:bottom,0.f,r.getBottom(),false));
            g.fillRoundedRectangle(r,3.5f);
            g.setColour(highlighted?juce::Colour::fromRGB(65,84,91):
                       juce::Colour::fromRGB(115,132,138));
            g.drawRoundedRectangle(r,3.5f,1.f);
        }
        void drawButtonText(juce::Graphics& g,juce::TextButton& button,
                            bool highlighted,bool down) override {
            if(button.getComponentID()!="geq31.close") {
                juce::LookAndFeel_V4::drawButtonText(g,button,highlighted,down);
                return;
            }
            // Exact shared built-in FX X path, from CSS
            // --armix-floating-close-icon, scaled to 22/24px.
            juce::Path x;
            x.startNewSubPath(6.4f,19.f);
            x.lineTo(5.f,17.6f);x.lineTo(10.6f,12.f);
            x.lineTo(5.f,6.4f);x.lineTo(6.4f,5.f);
            x.lineTo(12.f,10.6f);x.lineTo(17.6f,5.f);
            x.lineTo(19.f,6.4f);x.lineTo(13.4f,12.f);
            x.lineTo(19.f,17.6f);x.lineTo(17.6f,19.f);
            x.lineTo(12.f,13.4f);x.closeSubPath();
            g.setColour(juce::Colour(0xfff4f8fa));
            g.fillPath(x,juce::AffineTransform::scale(22.f/24.f)
                .translated((button.getWidth()-22.f)*.5f,
                            (button.getHeight()-22.f)*.5f));
        }

        void drawRotarySlider(juce::Graphics& g,int x,int y,int width,
                              int height,float /*position*/,float,float,
                              juce::Slider& slider) override {
            if(slider.getComponentID()!="geq31.output-level")return;
            const float side=static_cast<float>(juce::jmin(width,height));
            const float bx=static_cast<float>(x)+(width-side)*.5f;
            const float by=static_cast<float>(y)+(height-side)*.5f;
            if(!knobImage_.isValid()) {
                g.setColour(juce::Colour(0xffa33434));
                g.drawFittedText("KNOB PNG MISSING",x,y,width,height,
                                 juce::Justification::centred,2);
                return;
            }
            {
                juce::Graphics::ScopedSaveState imageState(g);
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                g.drawImage(knobImage_,bx,by,side,side,
                            0,0,knobImage_.getWidth(),knobImage_.getHeight());
            }
            // Optical centre of PNG alpha, source is 192px square.
            const float cx=bx+side*(96.7f/192.f);
            const float cy=by+side*(97.97f/192.f);
            const auto polar=[cx,cy](float radius,float angle) noexcept {
                return juce::Point<float>(cx+radius*std::sin(angle),
                                          cy-radius*std::cos(angle));
            };
            const float db=static_cast<float>(slider.getValue());
            const float travel=db<=0.f?
                juce::jmap(db,-24.f,0.f,0.f,.5f):
                juce::jmap(db,0.f,6.f,.5f,1.f);
            const float t=juce::jlimit(0.f,1.f,travel);
            constexpr float pi=juce::MathConstants<float>::pi;
            const float start=-.75f*pi,end=start+1.5f*pi*t;
            // Arc radius = 70.08 source pixels, INSIDE 69..75 black groove.
            // Bezier arc is smoother than v11 segmented polygon on high-DPI.
            if(t>0.f) {
                juce::Path arc;
                arc.addCentredArc(cx,cy,side*.365f,side*.365f,
                                  0.f,start,end,true);
                g.setColour(juce::Colour(0xff26b8f6));
                g.strokePath(arc,juce::PathStrokeType(2.0f,
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            }
            // Dot halo remains inside machined face (source r ~62 < 65).
            const auto dot=polar(side*.292f,end);
            g.setColour(juce::Colour(0xd1042132));
            g.fillEllipse(dot.x-2.65f,dot.y-2.65f,5.3f,5.3f);
            g.setColour(juce::Colour(0xff27c8ff));
            g.fillEllipse(dot.x-1.9f,dot.y-1.9f,3.8f,3.8f);
        }
        void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                              float,float,float,juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            const auto id=slider.getComponentID();
            if(!id.startsWith("geq31-band-"))return;
            const int index=id.fromFirstOccurrenceOf("geq31-band-",false,false).getIntValue();
            if(index<0||index>=static_cast<int>(GraphicEq31Core::bandCount))return;
            const auto band=static_cast<std::size_t>(index);
            if(!drawables_[band])createBandArtwork(band);
            if(auto* art=drawables_[band].get()) {
                const auto gain=static_cast<float>(slider.getValue());
                if(lastGain_[band]!=gain&&thumbGroups_[band]!=nullptr) {
                    const float shift=thumbShift(gain);
                    thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift));
                    lastGain_[band]=gain;
                }
                // LOCAL thumb-only shadow: no per-band rectangular raster.
                // Authored vector THUMB bounds approximately (158..319,
                // 776..1055) in the 60..412 cropped SVG viewBox.
                const float shift=thumbShift(gain);
                const auto shadowBounds=juce::Rectangle<int>(
                    juce::roundToInt(x+width*98.f/352.f),
                    juce::roundToInt(y+(776.f+shift)*height/2042.f),
                    juce::roundToInt(width*161.f/352.f),
                    juce::roundToInt(height*280.f/2042.f));
                juce::DropShadow(juce::Colours::black.withAlpha(.30f),
                    4,{1,3}).drawForRectangle(g,shadowBounds);
                art->drawWithin(g,{static_cast<float>(x),static_cast<float>(y),
                    static_cast<float>(width),static_cast<float>(height)},
                    juce::RectanglePlacement::stretchToFit,1.f);
                // 7 SVG units used to collapse to ~0.98 physical px and
                // rasterised inconsistently. This screen-space index is
                // snapped to whole pixels, so all 31 faders get the same
                // legible, optically centred 2px WHITE ridge. The original
                // s11 path remains in the genuine SVG artwork.
                const int indexY=juce::roundToInt(y+
                    (indexCenterY+shift)*static_cast<float>(height)/2042.f);
                const int indexX=juce::roundToInt(x+
                    (160.3f-60.f)*static_cast<float>(width)/352.f);
                const int indexWidth=juce::jmax(2,juce::roundToInt(
                    154.5f*static_cast<float>(width)/352.f));
                g.setColour(juce::Colour(0xff0a0f12));
                g.fillRect(indexX,indexY-1,indexWidth,4);
                g.setColour(juce::Colours::white);
                g.fillRect(indexX,indexY,indexWidth,2);
            }else {
                g.setColour(juce::Colours::red);
                g.drawFittedText("SVG ERROR",x,y,width,height,
                    juce::Justification::centred,2);
            }
        }
    private:
        // Exact ends measured from the owner's original vector SVG paths:
        // slot outer Y=147.08..1696.67, THUMB Y=776.33..1055.50.
        // The WHITE index line (not the body's upper/lower edge) must meet
        // the actual ±12 tick path. White ridge s11: y=913..920,
        // centre=916.5; SVG major ticks: y=208.75 / 918.33 / 1627.92.
        static constexpr float indexCenterY=916.5f;
        static float thumbShift(float gain) noexcept {
            const float t=juce::jlimit(-12.f,12.f,gain)/12.f;
            return t>=0.f ? -(indexCenterY-208.75f)*t :
                             -(1627.92f-indexCenterY)*t;
        }
        void createBandArtwork(std::size_t band) {
            if(originalSvg_.isEmpty())return;
            auto document=juce::XmlDocument::parse(originalSvg_);
            if(!document)return;
            auto* fader=document->getChildByAttribute("id","Slider_EQ");
            if(!fader)return;
            // Crop only empty source margins for readable track geometry.
            document->setAttribute("viewBox","60 0 352 2042");
            auto* labelGroup=fader->getChildByAttribute("id","Text");
            auto* thumbGroup=fader->getChildByAttribute("id","THUMB");
            if(!labelGroup||!thumbGroup)return;
            // The source stays byte-for-byte intact; adjust just this
            // UI-thread SVG clone. Uniform Inter text, same two-line layout
            // and visible Hz/kHz units as equalizer-interface.svg.
            const auto nominal=juce::String(graphicEq31Labels[band]);
            const bool isKhz=nominal.endsWithChar('k');
            const auto number=isKhz?nominal.dropLastCharacters(1):nominal;
            auto* frequency=labelGroup->getChildByAttribute("id","32");
            auto* tspan=frequency?frequency->getFirstChildElement():nullptr;
            auto* unitGroup=labelGroup->getChildByAttribute("id","Hz");
            auto* unitText=unitGroup?unitGroup->getFirstChildElement():nullptr;
            auto* unitSpan=unitText?unitText->getFirstChildElement():nullptr;
            if(!tspan||!unitSpan)return;
            tspan->deleteAllTextElements();
            tspan->addTextElement(number);
            tspan->setAttribute("style",
                "font-family: Inter; font-size: 27px; font-weight: 700; fill: #344047");
            frequency->setAttribute("text-anchor","middle");
            frequency->setAttribute("style",
                "transform: matrix(4.167,0,0,4.167,237.76,1850.466); font-family: Inter");
            unitSpan->deleteAllTextElements();
            unitSpan->addTextElement(isKhz?"kHz":"Hz");
            unitSpan->setAttribute("style",
                "font-family: Inter; font-size: 18px; font-weight: 600; fill: #5b6a72");
            unitText->setAttribute("text-anchor","middle");
            unitText->setAttribute("style",
                "transform: matrix(4.167,0,0,4.167,237.76,1933.799); font-family: Inter");

            auto* trackGroup=fader->getChildByAttribute("id","Track");
            if(!trackGroup)return;
            // Original small coloured endpoint dots add no signal meaning.
            for(int j=trackGroup->getNumChildElements()-1;j>=0;--j) {
                auto* node=trackGroup->getChildElement(j);
                if(node->getChildByAttribute("class","s5")||
                   node->getChildByAttribute("class","s6"))
                    trackGroup->removeChildElement(node,true);
            }
            // SVG Track path geometry is preserved. Use prominent black ticks
            // on BOTH sides; change only this UI XML clone, not owner source.
            for(int row=0;row<2;++row) {
                if(auto* tickGroup=trackGroup->getChildElement(row)) {
                    for(int p=0;p<tickGroup->getNumChildElements();++p) {
                        auto* tick=tickGroup->getChildElement(p);
                        if(tick->hasTagName("path")&&
                           tick->getStringAttribute("class")=="s0")
                            tick->setAttribute("style",
                              "fill:none;stroke:#131a1d;stroke-width:7.9;"
                              "stroke-linecap:butt;stroke-miterlimit:10");
                    }
                }
            }
            // Recessed outer lip: the original SVG g1 gradient is visually
            // lighter than the aluminium surface, flattening the channel.
            // Tint the UI clone only, to a darker chamfer around the hole.
            if(auto* defs=document->getChildByName("defs")) {
                if(auto* lip=defs->getChildByAttribute("id","g1")) {
                    const char* shades[]={"#929b9f","#a6afb3",
                                          "#969ea3","#858d92"};
                    for(int j=0;j<lip->getNumChildElements();++j)
                        lip->getChildElement(j)->setAttribute(
                            "stop-color",shades[juce::jmin(j,3)]);
                }
            }
            // Deep recessed black hardware slot (existing SVG g2 gradient).
            if(auto* defs=document->getChildByName("defs")) {
                if(auto* gradient=defs->getChildByAttribute("id","g2")) {
                    const char* shades[]={"#080c0f","#12181c",
                                          "#1b2126","#20272b","#242a2d"};
                    for(int j=0;j<gradient->getNumChildElements();++j)
                        gradient->getChildElement(j)->setAttribute(
                            "stop-color",shades[juce::jmin(j,4)]);
                }
            }
            // Anodized-black finish: recolour only the four authored
            // THUMB gradients, never the Track or original image shadow.
            if(auto* defs=document->getChildByName("defs")) {
                const char* ids[]={"g3","g4","g5","g6"};
                const char* outer[]={"#090b0e","#13181c","#262d32",
                                     "#323a40","#262d32","#13181c","#090b0e"};
                const char* top[]={"#30383e","#252d32","#141a1e",
                                   "#252d32","#30383e"};
                const char* bottom[]={"#252e33","#171e22","#0d1216",
                                      "#171e22","#252e33"};
                const char* ridge[]={"#646e74","#88969e","#b4bdc1",
                                     "#88969e","#5b656b"};
                const char* const* palettes[]={outer,top,bottom,ridge};
                const int maxima[]={6,4,4,4};
                for(int group=0;group<4;++group)
                    if(auto* gradient=defs->getChildByAttribute("id",ids[group]))
                        for(int i=0;i<gradient->getNumChildElements();++i)
                            gradient->getChildElement(i)->setAttribute(
                                "stop-color",
                                palettes[group][juce::jmin(i,maxima[group])]);
            }
            // Restore the OWNERS' AUTHORED raster _Image3 shadow,
            // inside THUMB. No synthetic shadow graphic or flattened image.
            // Only its opacity is slightly moderated to fit the tight pitch.
            if(auto* rasterShadow=thumbGroup->getFirstChildElement()) {
                if(rasterShadow->getChildByAttribute("id","_Image3"))
                    rasterShadow->setAttribute("opacity",0.87);
            }
            auto drawable=juce::Drawable::createFromSVG(*document);
            if(!drawable)return;
            auto* authoredFader=drawable->findChildWithID("Slider_EQ");
            auto* authoredThumb=authoredFader?authoredFader->findChildWithID("THUMB"):nullptr;
            if(!authoredThumb)return;
            thumbGroups_[band]=authoredThumb;
            drawables_[band]=std::move(drawable);
        }
        juce::String originalSvg_;
        juce::Image knobImage_;
        std::array<std::unique_ptr<juce::Drawable>,GraphicEq31Core::bandCount> drawables_{};
        std::array<juce::Component*,GraphicEq31Core::bandCount> thumbGroups_{};
        std::array<float,GraphicEq31Core::bandCount> lastGain_{};
    };

    using Attachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    static constexpr int bandCellWidth=40; // 80% of authored 50px pitch
    static constexpr int rightRailWidth=100; // v7 fader bank is unchanged
    GraphicEq31Processor& processor_;
    EqSkin skin_;
    juce::Image brushedMetal_;
    juce::Component bandSurface_;
    juce::Viewport viewport_;
    std::array<GraphicEq31DetentSlider,GraphicEq31Core::bandCount> sliders_;
    GraphicEq31DetentSlider level_;
    std::unique_ptr<Attachment> levelAttachment_;
    std::vector<std::unique_ptr<Attachment>> attachments_;
    juce::ComboBox preset_;
    juce::TextButton flat_,save_,power_,close_;
    std::vector<UserPresetEntry> userPresets_;
    juce::String selectedUserId_;
    std::array<float,GraphicEq31Core::bandCount> savedUserBands_{};
    float savedUserLevelDb_=0.f;
    std::optional<bool> hostBypassed_;
    bool hostLeaseWasValid_=false;
    std::function<std::vector<UserPresetEntry>()> listUserPresets_;
    std::function<juce::String(const juce::String&)> saveUserPreset_;
    std::function<bool(const juce::String&)> recallUserPreset_;
    std::function<bool(bool)> requestBypass_;
    std::function<HostBypassState()> readHostPower_;
    juce::ComponentDragger windowDragger_;
    juce::Component::SafePointer<juce::DocumentWindow> dragWindow_;
};
inline juce::AudioProcessorEditor* GraphicEq31Processor::createEditor() {
    return new GraphicEq31Editor(*this);
}
} // namespace armix::builtin_fx
