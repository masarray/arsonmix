// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GuitarRigProcessor.h"
#include "guitar/GuitarHardwareGeometry.h"
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
            // PNG pointer is drawn at 12 o\'clock; JUCE angle zero is 3 o\'clock.
            const float radians=start+pos*(end-start)+juce::MathConstants<float>::halfPi;
            g.addTransform(juce::AffineTransform::rotation(radians,cx,cy));
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
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
                        juce::Colour::fromRGB(179,203,207).withAlpha(0.10f));
                    g.fillRoundedRectangle(face,
                        std::min(face.getHeight()*0.45f,11.f));
                }
            }
        }
    };
    struct NavigationButtons final : juce::LookAndFeel_V4 {
        void drawButtonBackground(juce::Graphics& g,juce::Button& b,
            const juce::Colour&,bool hover,bool pressed) override {
            const auto r=b.getLocalBounds().toFloat().reduced(1.f);
            if(b.getToggleState()) {
                g.setColour(juce::Colour::fromRGB(77,112,116).withAlpha(0.18f));
                g.fillRoundedRectangle(r,5.f);
                g.setColour(juce::Colour::fromRGB(128,199,199));
                g.fillRect(juce::Rectangle<float>{r.getX()+8.f,r.getBottom()-2.f,r.getWidth()-16.f,2.f});
            } else if(hover||pressed) {
                g.setColour(juce::Colours::white.withAlpha(pressed?.10f:.055f));
                g.fillRoundedRectangle(r,5.f);
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
        setResizable(true,false);
        setResizeLimits(900,545,1484,896);
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
            focusButtons_[i].setLookAndFeel(&navLook_);
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
        // The ONLY chrome surrounding hardware is a quiet shared work surface.
        // Render controls at the same logical coordinates as their JUCE hitboxes.
        g.fillAll(juce::Colour::fromRGB(22,24,26));
        const auto viewport=armix::guitarfx::art::Viewport::from(getWidth(),getHeight());
        juce::Graphics::ScopedSaveState scope(g);
        g.addTransform(juce::AffineTransform::scale(viewport.scale)
            .translated(viewport.dx,viewport.dy));
        const auto label=juce::Colour::fromRGB(210,215,215);
        const auto dim=juce::Colour::fromRGB(138,150,155);
        const auto accent=juce::Colour::fromRGB(139,199,198);
        g.setColour(juce::Colour::fromRGB(30,33,36));
        g.fillRect(0,0,1060,60);
        g.setColour(juce::Colour::fromRGB(47,52,54));
        g.drawLine(0.f,60.f,1060.f,60.f,1.f);
        g.setColour(label);
        g.setFont(juce::Font(juce::FontOptions(18.f)));
        g.drawText("ArSonMix   /   GUITAR FX",22,13,335,28,
                   juce::Justification::centredLeft);
        g.setColour(dim);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText("INPUT",24,89,68,20,juce::Justification::centredLeft);
        g.drawText("OUTPUT",1010,89,46,20,juce::Justification::centredLeft);
        for(int i=0;i<3;++i) {
            g.setColour(juce::Colour::fromRGB(78,90,94));
            const float x=430.f+204.f*i;
            g.drawLine(x,102.f,x+17.f,102.f,1.2f);
        }
        // Flat theatre: no bezel, no large framed backdrop, no decorative well.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour::fromRGB(36,39,42),0.f,158.f,
            juce::Colour::fromRGB(19,21,23),0.f,529.f,false));
        g.fillRect(juce::Rectangle<float>{0.f,150.f,1060.f,380.f});
        g.setColour(juce::Colour::fromRGB(56,62,64).withAlpha(.40f));
        g.drawLine(17.f,528.f,1043.f,528.f,1.f);
        // Artwork must preserve native aspect ratio and use metadata anchors.
        const auto picture=artBounds();
        const auto& hw=armix::guitarfx::art::modules[focus_];
        const auto& hero=focus_==2?amp_:focus_==3?cab_:focus_==0?gate_:pedal_;
        if(hero.isValid()) {
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImageWithin(hero,juce::roundToInt(picture.getX()),
                juce::roundToInt(picture.getY()),
                juce::roundToInt(picture.getWidth()),
                juce::roundToInt(picture.getHeight()),
                juce::RectanglePlacement::centred);
        } else {
            g.setColour(juce::Colour::fromRGB(51,55,57));
            g.fillRoundedRectangle(picture,12.f);
        }
        // Paint small readouts close to the hardware control they describe.
        // Neither values nor labels are burned into the device-body PNG.
        for(std::size_t i=0;i<activeControlCount();++i) {
            const auto k=knobBounds(i);
            const int centre=k.getCentreX();
            const int ty=k.getBottom()+3;
            g.setColour(dim);
            g.setFont(juce::Font(juce::FontOptions(10.f)));
            g.drawText(controlNames_[focus_][i],centre-61,ty,122,14,
                       juce::Justification::centred);
            const float normalized=draft_.blocks[focus_].values[i];
            juce::String reading;
            if(focus_==0&&i==0)
                reading=juce::String(-80.f+50.f*normalized,0)+" dB";
            else if(focus_==0&&i==1)
                reading=juce::String(120.f*normalized,0)+" ms";
            else if(focus_==0&&i==2)
                reading=juce::String(12.f+228.f*normalized,0)+" ms";
            else reading=juce::String(normalized*100.f,0)+"%";
            g.setColour(label);
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawText(reading,centre-61,ty+14,122,18,
                       juce::Justification::centred);
        }
        if(focus_==3) {
            g.setColour(dim);
            g.setFont(juce::Font(juce::FontOptions(11.f)));
            g.drawText("DUAL SPEAKER CABINET  /  65-TAP FIR",
                       310,501,630,20,juce::Justification::centred);
        }
        // A physical signal LED is vector-rendered in the artist-defined well;
        // it reflects the real mutable enable bit, not animation or fake audio.
        if(hw.led.diameter>0.f && draft_.count>focus_) {
            const auto nativePic=armix::guitarfx::art::fit(
                armix::guitarfx::art::logicalStage,hw);
            const auto led=armix::guitarfx::art::centeredControl(nativePic,hw,hw.led);
            const auto enabled=draft_.blocks[focus_].enabled;
            g.setColour(enabled?juce::Colour::fromRGB(89,196,161):
                                juce::Colour::fromRGB(65,70,70));
            g.fillEllipse(led.x,led.y,led.w,led.h);
        }
        g.setColour(dim);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText("FACTORY TONES",25,551,163,22,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(32,35,37));
        g.fillRect(0,593,1060,47);
        const auto serial=processor_.observedBlockSerial();
        const bool live=serial>0 && nowMs_-lastBlockMs_<900;
        g.setColour(dim);
        g.drawText(live?"PCM ACTIVE":"PCM OFFLINE  /  NO FAKE METER",
                   421,603,253,25,juce::Justification::centredLeft);
        const auto paintPeak=[&](int x,float peak,const char* caption) {
            g.setColour(dim);
            g.drawText(caption,x-32,601,27,25,juce::Justification::centredRight);
            g.setColour(juce::Colour::fromRGB(48,53,55));
            g.fillRoundedRectangle(float(x),611.f,117.f,6.f,3.f);
            if(live) {
                const float value=std::clamp(std::isfinite(peak)?peak:0.f,0.f,1.f);
                g.setColour(accent);
                if(value>0.f)g.fillRoundedRectangle(float(x),611.f,
                                              value*117.f,6.f,3.f);
            }
        };
        paintPeak(744,processor_.observedInputPeak(),"IN");
        paintPeak(912,processor_.observedOutputPeak(),"OUT");
    }
    void resized() override {
        const auto put=[this](juce::Component& c,float x,float y,float w,float h) {
            c.setBounds(mapBounds({x,y,w,h}));
        };
        put(preset_,783,14,255,31);
        put(bankButtons_[0],637,17,61,27);
        put(bankButtons_[1],705,17,61,27);
        put(status_,20,596,381,38);
        for(std::size_t i=0;i<4;++i) {
            put(library_[i],204.f+207.f*static_cast<float>(i),542,194,33);
            const float x=216.f+207.f*static_cast<float>(i);
            put(focusButtons_[i],x,79,145,44);
            put(power_[i],x+150.f,88,49,25);
        }
        stomp_.setBounds(mapBounds(toArtRect(stompBounds())));
        for(std::size_t i=0;i<3;++i)
            sliders_[i].setBounds(mapBounds(toArtRect(knobBounds(i))));
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
    static armix::guitarfx::art::Rect toArtRect(juce::Rectangle<int> r) {
        return {float(r.getX()),float(r.getY()),float(r.getWidth()),float(r.getHeight())};
    }
    static juce::Rectangle<int> asJuceRect(armix::guitarfx::art::Rect r) {
        return {juce::roundToInt(r.x),juce::roundToInt(r.y),
                juce::roundToInt(r.w),juce::roundToInt(r.h)};
    }
    juce::Rectangle<int> mapBounds(armix::guitarfx::art::Rect r) const {
        return asJuceRect(armix::guitarfx::art::Viewport::from(
            getWidth(),getHeight()).map(r));
    }
    juce::Rectangle<float> artBounds() const {
        const auto box=armix::guitarfx::art::fit(
            armix::guitarfx::art::logicalStage,
            armix::guitarfx::art::modules[focus_]);
        return {box.x,box.y,box.w,box.h};
    }
    juce::Rectangle<int> knobBounds(std::size_t i) const {
        const auto& h=armix::guitarfx::art::modules[focus_];
        return asJuceRect(armix::guitarfx::art::centeredControl(
            armix::guitarfx::art::fit(armix::guitarfx::art::logicalStage,h),
            h,h.knobs[i]));
    }
    juce::Rectangle<int> stompBounds() const {
        const auto& h=armix::guitarfx::art::modules[focus_];
        return asJuceRect(armix::guitarfx::art::centeredControl(
            armix::guitarfx::art::fit(armix::guitarfx::art::logicalStage,h),
            h,h.footswitch));
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
        stomp_.setBounds(mapBounds(toArtRect(stompBounds())));
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setVisible(i<activeControlCount());
            sliders_[i].setBounds(mapBounds(toArtRect(knobBounds(i))));
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
    NavigationButtons navLook_{};
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
