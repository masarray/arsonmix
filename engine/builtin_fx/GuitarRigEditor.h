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
            bankButtons_[i].onClick=[this,i] {
                requestBank(static_cast<std::uint8_t>(i));
            };
            addAndMakeVisible(bankButtons_[i]);
        }
        for(std::size_t i=0;i<4;++i){
            library_[i].setButtonText(armix::guitarfx::factoryToneNames[i]);
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
            focusButtons_[i].onClick=[this,i]{
                focus_=i;refreshWidgets();repaint();
            };
            addAndMakeVisible(focusButtons_[i]);
            power_[i].setButtonText("ON");
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
        // Focus shell deliberately uses only ONE major visual device.
        const auto stage=juce::Rectangle<int>(226,177,getWidth()-250,286);
        g.setColour(juce::Colour::fromRGB(36,39,44));
        g.fillRoundedRectangle(stage.toFloat(),12.f);
        g.setColour(juce::Colour::fromRGB(72,70,67));
        g.drawRoundedRectangle(stage.toFloat(),12.f,1.f);
        const auto hero=focus_==2?amp_:focus_==3?cab_:focus_==0?gate_:pedal_;
        const auto art=stage.reduced(20,20);
        if(hero.isValid())
            g.drawImageWithin(hero,art.getX(),art.getY(),art.getWidth(),
                art.getHeight(),juce::RectanglePlacement::centred);
        else {
            g.setColour(juce::Colour::fromRGB(71,62,54));
            g.fillRoundedRectangle(art.toFloat().reduced(25.f,14.f),15.f);
            g.setColour(gold);
            g.setFont(juce::Font(juce::FontOptions(26.f)));
            g.drawText(names_[focus_],art,juce::Justification::centred);
        }
        // Bitmap artwork never contains parameter legends or live values.
        g.setColour(gold);
        g.setFont(juce::Font(juce::FontOptions(14.f)));
        g.drawText(names_[focus_],246,184,290,27,
                   juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(12.f)));
        for(std::size_t i=0;i<activeControlCount();++i){
            const int cx=300+static_cast<int>(i)*188;
            g.drawText(controlNames_[focus_][i],cx-40,549,110,22,
                       juce::Justification::centred);
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
            g.drawText(reading,cx-40,570,110,18,
                       juce::Justification::centred);
            g.setColour(muted);
        }
        if(focus_==3){
            g.setFont(juce::Font(juce::FontOptions(12.f)));
            g.drawText("ORIGINAL 65-TAP CABINET FIR  ·  32-SAMPLE DSP PDC",
                       305,495,570,29,juce::Justification::centred);
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
        bankButtons_[0].setBounds(getWidth()-384,18,43,30);
        bankButtons_[1].setBounds(getWidth()-337,18,43,30);
        status_.setBounds(16,getHeight()-105,175,46);
        for(std::size_t i=0;i<4;++i){
            library_[i].setBounds(18,216+static_cast<int>(i)*54,168,43);
        }
        for(std::size_t i=0;i<4;++i){
            const int x=228+static_cast<int>(i)*199;
            focusButtons_[i].setBounds(x,104,151,39);
            power_[i].setBounds(x+155,108,38,30);
        }
        for(std::size_t i=0;i<3;++i)
            sliders_[i].setBounds(259+static_cast<int>(i)*188,473,116,80);
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
        // JUCE Image uses reference-counted image storage. Decoding 5 PNGs
        // on every floating editor reopen is avoidable startup work; the
        // first UI creation populates this thread-safe function-local cache
        // and later editors only share image handles. No audio-thread IO.
        static const std::array<juce::Image,5> skins=[] {
            return std::array<juce::Image,5>{
                juce::ImageFileFormat::loadFrom(
                    BinaryData::ampheadclassa_png,
                    static_cast<std::size_t>(BinaryData::ampheadclassa_pngSize)),
                juce::ImageFileFormat::loadFrom(
                    BinaryData::cabinetdual12_png,
                    static_cast<std::size_t>(BinaryData::cabinetdual12_pngSize)),
                juce::ImageFileFormat::loadFrom(
                    BinaryData::pedalgate_png,
                    static_cast<std::size_t>(BinaryData::pedalgate_pngSize)),
                juce::ImageFileFormat::loadFrom(
                    BinaryData::pedaloverdrive_png,
                    static_cast<std::size_t>(BinaryData::pedaloverdrive_pngSize)),
                juce::ImageFileFormat::loadFrom(
                    BinaryData::knobbrushedmetal_png,
                    static_cast<std::size_t>(BinaryData::knobbrushedmetal_pngSize))
            };
        }();
        amp_=skins[0];cab_=skins[1];gate_=skins[2];
        pedal_=skins[3];knob_=skins[4];
#endif
    }
    std::size_t activeControlCount() const noexcept {
        return focus_==3?0u:(focus_==0||focus_==2)?3u:1u;
    }
    void refreshWidgets(){
        refreshing_=true;
        for(std::size_t i=0;i<2;++i){
            bankButtons_[i].setColour(juce::TextButton::buttonColourId,
                i==selectedBank_?juce::Colour::fromRGB(119,79,40):
                                 juce::Colour::fromRGB(43,49,55));
        }
        for(std::size_t i=0;i<4;++i){
            library_[i].setColour(juce::TextButton::buttonColourId,
                selectedPreset_==static_cast<int>(i)+1?
                    juce::Colour::fromRGB(91,67,43):
                    juce::Colour::fromRGB(39,44,50));
            power_[i].setButtonText(draft_.blocks[i].enabled?"ON":"OFF");
            focusButtons_[i].setColour(juce::TextButton::buttonColourId,
                i==focus_?juce::Colour::fromRGB(93,67,43):
                          juce::Colour::fromRGB(49,55,61));
        }
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setVisible(i<activeControlCount());
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
    juce::Image amp_,cab_,gate_,pedal_,knob_;
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
