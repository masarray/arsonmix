// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GuitarRigProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#if defined(ARMIX_GFX_EMBED_PNG_SKINS)
#include "BinaryData.h"
#endif

// Single native JUCE editor opened by the ALREADY EXISTING FxInstance
// vendor-editor lease. No second WebView, callback, device or mixer.
// Decorative original PNGs are embedded into ARMixAssets ONLY in the
// production app; test binaries use a lightweight drawn fallback.
namespace armix::builtin_fx {
class GuitarRigEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer {
    using Program=armix::guitarfx::Program;
    using Module=armix::guitarfx::Module;
    struct HardwareKnobs final : juce::LookAndFeel_V4 {
        juce::Image image;
        void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
                              float pos,float start,float end,
                              juce::Slider& slider) override {
            if(!image.isValid()) {
                juce::LookAndFeel_V4::drawRotarySlider(g,x,y,w,h,pos,start,end,slider);
                return;
            }
            juce::Graphics::ScopedSaveState saved(g);
            const float cx=static_cast<float>(x+w/2),cy=static_cast<float>(y+h/2);
            const float radians=start+pos*(end-start);
            g.addTransform(juce::AffineTransform::rotation(radians,cx,cy));
            g.drawImageWithin(image,x+3,y+3,w-6,h-6,
                              juce::RectanglePlacement::centred);
        }
    };
    struct SkinButtons final : juce::LookAndFeel_V4 {
        juce::Image off,on;
        void drawButtonBackground(juce::Graphics& g,juce::Button& button,
                                  const juce::Colour&,bool hovered,bool down) override {
            // ON and OFF are pixel-aligned full-frame skins. Hover and
            // pressed feedback tint the inset face, NEVER the housing
            // geometry, so even rapid mouse interaction cannot flicker.
            const auto& bitmap=button.getToggleState()?on:off;
            if(bitmap.isValid())g.drawImageWithin(bitmap,0,0,button.getWidth(),
                button.getHeight(),juce::RectanglePlacement::stretchToFit);
            else juce::LookAndFeel_V4::drawButtonBackground(g,button,
                juce::Colour::fromRGB(38,42,47),hovered,down);
            if(!button.isEnabled())return;
            if(hovered||down) {
                auto face=juce::Rectangle<float>(
                    6.f,5.f,static_cast<float>(button.getWidth()-12),
                    static_cast<float>(button.getHeight()-10));
                if(face.getWidth()>4.f&&face.getHeight()>4.f) {
                    g.setColour(down?juce::Colours::black.withAlpha(0.20f):
                        juce::Colour::fromRGB(255,197,116).withAlpha(0.12f));
                    g.fillRoundedRectangle(face,
                        std::min(face.getHeight()*0.45f,11.f));
                }
            }
        }
    };
public:
    explicit GuitarRigEditor(GuitarRigProcessor& processor)
        : juce::AudioProcessorEditor(processor),processor_(processor) {
        setOpaque(true);
        // An earlier window may have closed after PCM applied a preset but
        // before its timer drained the completion. Reduce it before showing
        // the restored tone: opening another editor must not display stale
        // accepted settings from the previous window.
        processor_.serviceCompletions();
        selectedBank_=processor_.activeBank();
        bankDrafts_[0]=processor_.acceptedBankProgram(0);
        bankDrafts_[1]=processor_.acceptedBankProgram(1);
        draft_=bankDrafts_[selectedBank_];
        accepted_=draft_;
        loadArt();
        knobsLook_.image=knob_;
        buttonLook_.off=buttonOff_;buttonLook_.on=buttonOn_;
        switchLook_.off=switchOff_;switchLook_.on=switchOn_;
        stompLook_.off=stompOff_;stompLook_.on=stompOn_;
        setSize(1060,640);
        for(std::size_t i=0;i<armix::guitarfx::factoryToneNames.size();++i)
            preset_.addItem(armix::guitarfx::factoryToneNames[i],
                            static_cast<int>(i)+1);
        preset_.addItem("Custom / saved state",5);
        selectedPreset_=armix::guitarfx::identifyFactoryTone(draft_);
        preset_.setSelectedId(selectedPreset_?selectedPreset_:5,
                              juce::dontSendNotification);
        preset_.onChange=[this]{choosePreset(preset_.getSelectedId());};
        addAndMakeVisible(preset_);
        for(std::size_t i=0;i<2;++i){
            bankButtons_[i].setButtonText(i==0?"A":"B");
            bankButtons_[i].setLookAndFeel(&buttonLook_);
            bankButtons_[i].setClickingTogglesState(false);
            bankButtons_[i].onClick=[this,i] {
                requestBank(static_cast<std::uint8_t>(i));
            };
            addAndMakeVisible(bankButtons_[i]);
        }
        for(std::size_t i=0;i<4;++i){
            library_[i].setButtonText(armix::guitarfx::factoryToneNames[i]);
            library_[i].setLookAndFeel(&buttonLook_);
            library_[i].setClickingTogglesState(false);
            library_[i].onClick=[this,i]{
                const int id=static_cast<int>(i)+1;
                preset_.setSelectedId(id,juce::dontSendNotification);
                choosePreset(id);
            };
            addAndMakeVisible(library_[i]);
        }
        for(std::size_t i=0;i<4;++i){
            const auto* name=names_[i];
            focusButtons_[i].setButtonText(name);
            focusButtons_[i].setLookAndFeel(&buttonLook_);
            focusButtons_[i].setClickingTogglesState(false);
            focusButtons_[i].onClick=[this,i]{
                focus_=i;refreshWidgets();repaint();
            };
            addAndMakeVisible(focusButtons_[i]);
            power_[i].setButtonText("");
            power_[i].setTooltip(juce::String("Toggle ")+name);
            power_[i].setLookAndFeel(&switchLook_);
            power_[i].setClickingTogglesState(false);
            power_[i].onClick=[this,i]{
                if(draft_.count<=i)return;
                draft_.blocks[i].enabled=!draft_.blocks[i].enabled;
                markCustom();dirty_=true;refreshWidgets();repaint();
            };
            addAndMakeVisible(power_[i]);
        }
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sliders_[i].setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            sliders_[i].setRange(0.,1.,0.005);
            sliders_[i].setLookAndFeel(&knobsLook_);
            sliders_[i].onValueChange=[this,i] {
                if(refreshing_ || focus_>=draft_.count)return;
                draft_.blocks[focus_].values[i]=static_cast<float>(
                    sliders_[i].getValue());
                markCustom();dirty_=true;repaint();
            };
            addAndMakeVisible(sliders_[i]);
        }
        // A second ON/OFF hit target on the physical pedal footwell
        // forwards the SAME DSP intent as its top-row toggle.
        stomp_.setButtonText("");
        stomp_.setLookAndFeel(&stompLook_);
        stomp_.onClick=[this]{
            if(focus_<2 && draft_.count>focus_)power_[focus_].triggerClick();
        };
        addAndMakeVisible(stomp_);
        status_.setJustificationType(juce::Justification::centredRight);
        status_.setColour(juce::Label::textColourId,
                          juce::Colour::fromRGB(213,170,112));
        addAndMakeVisible(status_);
        refreshWidgets();
        startTimerHz(15); // UI only, never a second audio timer/callback.
    }
    ~GuitarRigEditor() override {
        stopTimer();
        // Closing a floating native editor must not discard the last knob
        // movement made between 15 Hz UI ticks. Ownership remains in the
        // processor after the window disappears; callback ACK can still be
        // reduced by .smix save/reopen, without any hidden editor timer.
        if(dirty_ && processor_.isReady()){
            const auto id=processor_.nextRequestId();
            if(id && processor_.queueProgram(draft_,id,selectedBank_))
                dirty_=false;
        }
        // If the performer selected B while A was applying, retain B as a
        // separate ordered intent when the editor closes mid-transition.
        if(requestedBank_>=0 && processor_.isReady()){
            const auto target=static_cast<std::uint8_t>(requestedBank_);
            const auto id=processor_.nextRequestId();
            if(id) (void)processor_.queueProgram(bankDrafts_[target],id,target);
        }
        for(auto& slider:sliders_)slider.setLookAndFeel(nullptr);
        for(auto& b:focusButtons_)b.setLookAndFeel(nullptr);
        for(auto& b:power_)b.setLookAndFeel(nullptr);
        for(auto& b:library_)b.setLookAndFeel(nullptr);
        for(auto& b:bankButtons_)b.setLookAndFeel(nullptr);
        stomp_.setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(18,21,26));
        const auto gold=juce::Colour::fromRGB(211,159,94);
        const auto muted=juce::Colour::fromRGB(155,166,179);
        g.setColour(juce::Colour::fromRGB(33,39,46));
        g.fillRect(0,0,getWidth(),64);
        g.fillRect(0,64,getWidth(),92);
        g.fillRect(0,156,204,getHeight()-156);
        g.setColour(juce::Colour::fromRGB(58,64,71));
        g.drawLine(0.f,64.f,static_cast<float>(getWidth()),64.f,1.f);
        g.drawLine(0.f,156.f,static_cast<float>(getWidth()),156.f,1.f);
        g.drawLine(204.f,156.f,204.f,static_cast<float>(getHeight()),1.f);
        g.setColour(gold);
        g.setFont(juce::Font(juce::FontOptions(20.f,juce::Font::bold)));
        g.drawText("ArSonMix  /  GUITAR FX RIG",18,17,390,31,
                   juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(11.f)));
        g.drawText("TONE",18,77,162,25,juce::Justification::centredLeft);
        g.drawText("SIGNAL CHAIN  •  click hardware to focus",224,70,540,20,
                   juce::Justification::centredLeft);
        g.drawText("FACTORY TONES",18,177,171,24,
                   juce::Justification::centredLeft);
        // Reusable PNG frame; the hardware BODY has empty mounting wells.
        // Every knob/stomp is its own JUCE interactive child on top, never
        // baked into a pedal JPG/PNG and never a decorative fake parameter.
        const auto stage=juce::Rectangle<int>(226,177,getWidth()-250,365);
        if(panel_.isValid())g.drawImageWithin(panel_,stage.getX(),stage.getY(),
            stage.getWidth(),stage.getHeight(),
            juce::RectanglePlacement::stretchToFit);
        else {
            g.setColour(juce::Colour::fromRGB(30,33,37));
            g.fillRoundedRectangle(stage.toFloat(),12.f);
        }
        const auto& hero=focus_==2?amp_:focus_==3?cab_:focus_==0?gate_:pedal_;
        const auto art=artBounds();
        if(hero.isValid())
            g.drawImageWithin(hero,juce::roundToInt(art.getX()),
                juce::roundToInt(art.getY()),juce::roundToInt(art.getWidth()),
                juce::roundToInt(art.getHeight()),
                juce::RectanglePlacement::centred);
        else {
            g.setColour(juce::Colour::fromRGB(59,48,40));
            g.fillRoundedRectangle(art,12.f);
        }
        g.setColour(gold);
        g.setFont(juce::Font(juce::FontOptions(13.f)));
        g.drawText(names_[focus_],246,184,242,26,
                   juce::Justification::centredLeft);
        // Labels and live DSP values are always vector text drawn by JUCE;
        // no PNG text or hidden controls underneath the art.
        g.setFont(juce::Font(juce::FontOptions(11.f)));
        for(std::size_t i=0;i<activeControlCount();++i){
            const bool amp=focus_==2;
            const int x=amp?juce::roundToInt(knobBounds(i).getCentreX())-65:840;
            const int y=amp?519:244+static_cast<int>(i)*64;
            const int ox=amp?x:840;
            g.setColour(muted);
            g.drawText(controlNames_[focus_][i],ox,y,130,18,
                       amp?juce::Justification::centred:
                           juce::Justification::centredLeft);
            g.setColour(gold);
            const float normalized=draft_.blocks[focus_].values[i];
            juce::String reading;
            if(focus_==0 && i==0)
                reading=juce::String(-80.f+50.f*normalized,0)+" dB";
            else if(focus_==0 && i==1)
                reading=juce::String(120.f*normalized,0)+" ms";
            else if(focus_==0 && i==2)
                reading=juce::String(12.f+228.f*normalized,0)+" ms";
            else reading=juce::String(normalized*100.f,0)+"%";
            g.drawText(reading,ox,y+18,130,18,
                       amp?juce::Justification::centred:
                           juce::Justification::centredLeft);
        }
        if(focus_==3){
            g.setColour(muted);
            g.drawText("65-TAP CABINET FIR · FIXED 48-SAMPLE FX PDC",
                       316,532,600,24,juce::Justification::centred);
        }
        g.setColour(juce::Colour::fromRGB(51,57,65));
        g.fillRect(205,getHeight()-37,getWidth()-205,37);
        g.setColour(muted);
        const auto serial=processor_.observedBlockSerial();
        const bool live=serial>0 && nowMs_-lastBlockMs_<900;
        g.drawText(live?"PCM CALLBACK OBSERVED":"PCM OFFLINE  ·  NO FAKE METER",
                   225,getHeight()-34,240,30,juce::Justification::centredLeft);
        const int peakX=getWidth()-363,peakW=125;
        const auto paintPeak=[&](int x,float peak,const juce::String& title){
            g.setColour(muted);
            g.drawText(title,x-32,getHeight()-32,28,26,
                       juce::Justification::centredRight);
            g.setColour(juce::Colour::fromRGB(24,29,34));
            g.fillRoundedRectangle(static_cast<float>(x),
                static_cast<float>(getHeight()-25),static_cast<float>(peakW),10.f,3.f);
            if(live){
                const auto magnitude=std::clamp(std::isfinite(peak)?peak:0.f,0.f,1.f);
                g.setColour(juce::Colour::fromRGB(107,193,143));
                g.fillRoundedRectangle(static_cast<float>(x),
                    static_cast<float>(getHeight()-25),
                    std::max(1.f,magnitude*peakW),10.f,3.f);
            }
        };
        paintPeak(peakX,processor_.observedInputPeak(),"IN");
        paintPeak(peakX+178,processor_.observedOutputPeak(),"OUT");
        g.setColour(live?juce::Colour::fromRGB(98,197,141):
                         juce::Colour::fromRGB(108,113,119));
        g.fillEllipse(static_cast<float>(getWidth()-17),
                      static_cast<float>(getHeight()-23),9.f,9.f);
    }
    void resized() override {
        preset_.setBounds(getWidth()-280,16,258,33);
        bankButtons_[0].setBounds(getWidth()-445,22,77,22);
        bankButtons_[1].setBounds(getWidth()-360,22,77,22);
        status_.setBounds(16,getHeight()-105,175,46);
        for(std::size_t i=0;i<4;++i){
            library_[i].setBounds(18,216+static_cast<int>(i)*54,168,43);
        }
        for(std::size_t i=0;i<4;++i){
            const int x=228+static_cast<int>(i)*199;
            focusButtons_[i].setBounds(x,104,137,39);
            power_[i].setBounds(x+141,113,52,20);
        }
        stomp_.setBounds(stompBounds());
        for(std::size_t i=0;i<3;++i)
            sliders_[i].setBounds(knobBounds(i));
    }
private:
    void timerCallback() override {
        processor_.serviceCompletions();
        const auto receipt=processor_.lastCompletion();
        if(waitingId_ && receipt.requestId==waitingId_){
            if(receipt.applied){
                accepted_=receipt.program;
            }else{
                dirty_=true; // retain unsent draft after failed/cancelled command
            }
            waitingId_=0;
        }
        const auto serial=processor_.observedBlockSerial();
        nowMs_=juce::Time::getMillisecondCounter();
        if(serial!=lastSerial_){lastSerial_=serial;lastBlockMs_=nowMs_;}
        if(requestedBank_>=0&&!dirty_&&!waitingId_) {
            const auto next=static_cast<std::uint8_t>(requestedBank_);
            requestedBank_=-1;
            activateBank(next);
        }
        // The existing host consumes native edits even while this FX
        // instance or the entire rack is fully bypassed. Do not wait for
        // observed wet PCM here: otherwise changing a pedal while bypassed
        // silently fails, and its state is not ready when bypass turns ON.
        // No callback -> the queued intent remains PENDING, never fake-ACKed.
        if(dirty_&&!waitingId_&&processor_.isReady()) {
            const auto id=processor_.nextRequestId();
            if(id && processor_.queueProgram(draft_,id,selectedBank_)){
                waitingId_=id;dirty_=false;
            }
        }
        juce::String message;
        if(waitingId_)message="APPLYING  ·  awaiting PCM ACK";
        else if(dirty_)message="PENDING  ·  tone not yet accepted";
        else message=(nowMs_-lastBlockMs_<900)?"DSP ACCEPTED":"DSP OFFLINE";
        if(requestedBank_>=0)message="A/B SWITCH QUEUED";
        status_.setText(message,juce::dontSendNotification);
        repaint();
    }
    void loadArt() {
#if defined(ARMIX_GFX_EMBED_PNG_SKINS)
        // JUCE Image uses reference-counted image storage. Decoding 12 PNGs
        // on every floating editor reopen is avoidable startup work; the
        // first UI creation populates this thread-safe function-local cache
        // and later editors only share image handles. No audio-thread IO.
        static const std::array<juce::Image,12> skins=[] {
            return std::array<juce::Image,12>{
                juce::ImageFileFormat::loadFrom(BinaryData::gfxamphead_png,
                    static_cast<std::size_t>(BinaryData::gfxamphead_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxcabinet_png,
                    static_cast<std::size_t>(BinaryData::gfxcabinet_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxpedalgatebody_png,
                    static_cast<std::size_t>(BinaryData::gfxpedalgatebody_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxpedaldrivebody_png,
                    static_cast<std::size_t>(BinaryData::gfxpedaldrivebody_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxknob_png,
                    static_cast<std::size_t>(BinaryData::gfxknob_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxswitchoff_png,
                    static_cast<std::size_t>(BinaryData::gfxswitchoff_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxswitchon_png,
                    static_cast<std::size_t>(BinaryData::gfxswitchon_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxbuttonoff_png,
                    static_cast<std::size_t>(BinaryData::gfxbuttonoff_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxbuttonon_png,
                    static_cast<std::size_t>(BinaryData::gfxbuttonon_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxpanel_png,
                    static_cast<std::size_t>(BinaryData::gfxpanel_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxstompoff_png,
                    static_cast<std::size_t>(BinaryData::gfxstompoff_pngSize)),
                juce::ImageFileFormat::loadFrom(BinaryData::gfxstompon_png,
                    static_cast<std::size_t>(BinaryData::gfxstompon_pngSize))
            };
        }();
        amp_=skins[0];cab_=skins[1];gate_=skins[2];pedal_=skins[3];
        knob_=skins[4];switchOff_=skins[5];switchOn_=skins[6];
        buttonOff_=skins[7];buttonOn_=skins[8];panel_=skins[9];
        stompOff_=skins[10];stompOn_=skins[11];
#endif
    }
    juce::Rectangle<float> artBounds() const {
        // Stable exact positioning shared by bitmap and hit-testing.
        const bool pedal=focus_<2;
        const float width=pedal?237.f:720.f;
        const float height=pedal?332.f:300.f;
        return {631.f-width*.5f,190.f+(332.f-height)*.5f,width,height};
    }
    juce::Rectangle<int> knobBounds(std::size_t i) const {
        const auto a=artBounds();
        float nx=.5f,ny=.74f;
        if(focus_==0){
            nx=i==0?.31f:i==1?.69f:.50f;
            ny=i==2?.38f:.28f;
        }else if(focus_==1){nx=.50f;ny=.38f;}
        else {nx=i==0?.30f:i==1?.50f:.70f;ny=.73f;}
        const int cx=juce::roundToInt(a.getX()+a.getWidth()*nx);
        const int cy=juce::roundToInt(a.getY()+a.getHeight()*ny);
        return {cx-34,cy-34,68,68};
    }
    juce::Rectangle<int> stompBounds() const {
        const auto a=artBounds();
        const int x=juce::roundToInt(a.getX()+a.getWidth()*.5f);
        const int y=juce::roundToInt(a.getY()+a.getHeight()*.82f);
        return {x-29,y-29,58,58};
    }
    std::size_t activeControlCount() const noexcept {
        return focus_==3?0u:(focus_==0||focus_==2)?3u:1u;
    }
    void refreshWidgets(){
        refreshing_=true;
        for(std::size_t i=0;i<2;++i)
            bankButtons_[i].setToggleState(i==selectedBank_,
                                         juce::dontSendNotification);
        for(std::size_t i=0;i<4;++i){
            library_[i].setToggleState(selectedPreset_==static_cast<int>(i)+1,
                                      juce::dontSendNotification);
            power_[i].setToggleState(draft_.blocks[i].enabled,
                                     juce::dontSendNotification);
            focusButtons_[i].setToggleState(i==focus_,
                                            juce::dontSendNotification);
        }
        stomp_.setVisible(focus_<2);
        stomp_.setToggleState(draft_.blocks[focus_].enabled,
                              juce::dontSendNotification);
        stomp_.setBounds(stompBounds());
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setVisible(i<activeControlCount());
            sliders_[i].setBounds(knobBounds(i));
            sliders_[i].setValue(draft_.blocks[focus_].values[i],
                                  juce::dontSendNotification);
        }
        refreshing_=false;
    }
    void markCustom() {
        bankDrafts_[selectedBank_]=draft_;
        selectedPreset_=0;
        preset_.setSelectedId(5,juce::dontSendNotification);
    }
    // Tone snapshots are processor-owned and serialized in GRX1 v2;
    // switching does NOT silently change to a different rack insert.
    // If the current bank is still pending, defer instead of discarding it.
    void requestBank(std::uint8_t target) {
        if(target>1)return;
        if(target==selectedBank_){requestedBank_=-1;return;}
        if(dirty_||waitingId_) {
            requestedBank_=static_cast<int>(target);
            return;
        }
        activateBank(target);
    }
    void activateBank(std::uint8_t target) {
        selectedBank_=target;
        draft_=bankDrafts_[target];
        selectedPreset_=armix::guitarfx::identifyFactoryTone(draft_);
        preset_.setSelectedId(selectedPreset_?selectedPreset_:5,
                              juce::dontSendNotification);
        dirty_=true;
        refreshWidgets();
        repaint();
    }
    void choosePreset(int id){
        if(id<1||id>4)return;
        draft_=armix::guitarfx::factoryTone(
            static_cast<std::size_t>(id-1));
        bankDrafts_[selectedBank_]=draft_;
        selectedPreset_=id;
        dirty_=true;
        refreshWidgets();repaint();
    }
    GuitarRigProcessor& processor_;
    HardwareKnobs knobsLook_{};
    SkinButtons buttonLook_{},switchLook_{},stompLook_{};
    juce::Image amp_,cab_,gate_,pedal_,knob_,panel_;
    juce::Image switchOff_,switchOn_,buttonOff_,buttonOn_,stompOff_,stompOn_;
    juce::TextButton stomp_;
    juce::ComboBox preset_;
    juce::Label status_;
    std::array<juce::TextButton,4> focusButtons_{};
    std::array<juce::TextButton,4> library_{};
    std::array<juce::TextButton,2> bankButtons_{};
    std::array<juce::TextButton,4> power_{};
    std::array<juce::Slider,3> sliders_{};
    Program draft_{},accepted_{};
    std::array<Program,2> bankDrafts_{};
    std::uint8_t selectedBank_=0;
    int requestedBank_=-1;
    std::size_t focus_=2;
    std::uint64_t waitingId_=0,lastSerial_=0;
    juce::uint32 nowMs_=0,lastBlockMs_=0;
    int selectedPreset_=1;
    bool refreshing_=false,dirty_=false;
    static constexpr std::array<const char*,4> names_{
        "GATE","OVERDRIVE","AMPLIFIER","CABINET"};
    static constexpr std::array<std::array<const char*,3>,4> controlNames_{{
        {"THRESHOLD","HOLD","RELEASE"},
        {"DRIVE","",""},
        {"GAIN","TONE","MASTER"},
        {"","",""}
    }};
};
inline juce::AudioProcessorEditor* GuitarRigProcessor::createEditor(){
    return new GuitarRigEditor(*this);
}
} // namespace armix::builtin_fx
