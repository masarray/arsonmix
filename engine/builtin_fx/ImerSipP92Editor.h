// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Image-backed concert-room editor. AUDIO IS STILL 100% SonKu P9.1/P9.2.
// Room, speakers, slider rails, knobs are paint-only. The sliders preserve
// the exact existing host PCM/ACK and IMRS state paths.
#include <BinaryData.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include "ImerSipP92VisualSkin.h"

namespace armix::builtin_fx {

class ImerSipP92Editor final : public juce::AudioProcessorEditor,
                              private juce::Timer {
public:
    explicit ImerSipP92Editor(ImerSipP92Processor& processor)
        :juce::AudioProcessorEditor(processor),fx_(processor) {
        setSize(1100,680);
        setResizable(false,false);
        preset_.setComponentID("imersip.preset");
        preset_.addItem("Custom",1);
        for(const auto& scene:imersipP92Presets)
            preset_.addItem(juce::String(scene.label),scene.id);
        preset_.setLookAndFeel(&skin_);
        preset_.onChange=[this]{
            const int id=preset_.getSelectedId();
            if(!imersipP92PresetById(id))return;
            sync(imersipP92Factory(id));
            submit();
        };
        addAndMakeVisible(preset_);
        room_.setComponentID("imersip.room");
        room_.addItem("ROOM OFF",1);
        room_.addItem("STUDIO 7.2 M",2);
        room_.addItem("CHAMBER 14.4 M",3);
        room_.addItem("LARGE 28.8 M",4);
        room_.setLookAndFeel(&skin_);
        room_.onChange=[this]{
            preset_.setSelectedId(1,juce::dontSendNotification);
            submit();
        };
        addAndMakeVisible(room_);

        const char* ids[]={"stage","distance","early","late",
                          "mix","gain","align","yaw"};
        const char* names[]={"STAGE WIDTH","SOURCE DISTANCE","EARLY ROOM",
                             "LATE ROOM","SPATIAL MIX","MASTER GAIN",
                             "EAR ALIGN","HEAD YAW"};
        const double lower[]={30.,1.6,0.,0.,0.,-12.,-30.,-90.};
        const double upper[]={180.,7.4,100.,100.,100.,6.,20.,90.};
        const double step[]={1.,.1,1.,1.,1.,.1,1.,1.};
        for(int i=0;i<8;++i){
            auto& slider=knobs_[static_cast<std::size_t>(i)];
            slider.setComponentID(juce::String("imersip.")+ids[i]);
            slider.setSliderStyle(i==5?
                juce::Slider::RotaryHorizontalVerticalDrag:
                juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setRange(lower[i],upper[i],step[i]);
            slider.setLookAndFeel(&skin_);
            slider.onValueChange=[this]{
                preset_.setSelectedId(1,juce::dontSendNotification);
                submit();
                repaint();
            };
            addAndMakeVisible(slider);
            auto& label=labels_[static_cast<std::size_t>(i)];
            label.setText(names[i],juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centredLeft);
            label.setColour(juce::Label::textColourId,juce::Colour(0xffc9dce0));
            label.setFont(juce::FontOptions(11.f));
            addAndMakeVisible(label);
        }
        power_.setComponentID("imersip.power");
        power_.setButtonText("FX POWER");
        power_.setLookAndFeel(&skin_);
        power_.setEnabled(false); // no fake bypass authority
        power_.onClick=[this] {
            if(!requestBypass_ || !readBypass_)return;
            const bool wanted=!readBypass_();
            status_=requestBypass_(wanted)?"HOST BYPASS ACCEPTED":
                                          "HOST BYPASS REJECTED";
            refreshHostPower();
            repaint();
        };
        addAndMakeVisible(power_);
        close_.setComponentID("imersip.close");
        close_.setButtonText("CLOSE");
        close_.setLookAndFeel(&skin_);
        close_.onClick=[this] {
            if(auto* window=findParentComponentOfClass<juce::DocumentWindow>())
                window->closeButtonPressed();
            else setVisible(false);
        };
        addAndMakeVisible(close_);
        sync(fx_.acceptedSettings());
        startTimerHz(25);
    }
    ~ImerSipP92Editor() override {
        stopTimer();
        for(auto& slider:knobs_)slider.setLookAndFeel(nullptr);
        preset_.setLookAndFeel(nullptr);
        room_.setLookAndFeel(nullptr);
        power_.setLookAndFeel(nullptr);
        close_.setLookAndFeel(nullptr);
    }
    bool areVisualAssetsReady() const noexcept {return skin_.ready();}
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<bool()> readback) {
        requestBypass_=std::move(request);
        readBypass_=std::move(readback);
        power_.setEnabled(static_cast<bool>(requestBypass_) &&
                          static_cast<bool>(readBypass_));
        refreshHostPower();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff0e151a));
        // First-party scalable art: source image already has full viewport
        // coordinates; no resampling from 256x144 or tile/sprite bleeding.
        skin_.drawRoom(g);
        // Quiet brand/header rail: prevent white text blending into the
        // pale generated ceiling. Independent from visual PA sprites.
        g.setColour(juce::Colour(0xBB0B1419));
        g.fillRect(0,0,1100,73);
        // Subtle overlay keeps the background quiet behind the L/R PA.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0x77060d11),0.f,0.f,
            juce::Colour(0x00060d11),0.f,408.f,false));
        g.fillRect(0,0,1100,461);

        g.setColour(juce::Colour(0xffe6f1ef));
        g.setFont(juce::FontOptions(19.f));
        g.drawText("ImerSip",25,16,172,28,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff89b8b8));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText("8D-SONKU P9.1/P9.2  /  LIVE CONCERT WORLD",
                   25,45,380,19,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffd0e7e7));
        g.setFont(juce::FontOptions(13.f));
        g.drawText("LIVE CONCERT SPATIAL MONITOR",770,19,309,22,
                   juce::Justification::centredRight);

        // The visualization follows real stage spread and distance control.
        // It does NOT claim to be a calibrated 9.1.6 loudspeaker simulation.
        const auto concert=imersipP92ConcertBoxes(
            static_cast<float>(knobs_[0].getValue()),
            static_cast<float>(knobs_[1].getValue()));
        // Safe layout remains wholly within room for 30..180 degrees,
        // 1.6..7.4m; never crop the artist geometry or cover the controls.
        if(concert.safe())skin_.drawConcert(g,concert);
        g.setColour(juce::Colour(0xff88d6d2));
        g.setFont(juce::FontOptions(11.f));
        g.drawText("L  /  WORLD PA",80,413,260,18,juce::Justification::centred);
        g.drawText("R  /  WORLD PA",766,413,260,18,juce::Justification::centred);

        // Image-backed three-section control desk: presets / scene / master.
        skin_.drawPanel(g);
        // Real metal PNG stays underneath, but darker material contrast
        // keeps compact labels and 7 draggable tracks readable.
        g.setColour(juce::Colour(0xA60A1114));
        g.fillRect(0,461,1100,219);
        g.setColour(juce::Colour(0xff54797b));
        g.fillRect(0,461,1100,1);
        g.setColour(juce::Colour(0xc600090c));
        g.fillRect(251,468,2,204);
        g.fillRect(879,468,2,204);
        g.setColour(juce::Colour(0xffcfdadd));
        g.setFont(juce::FontOptions(12.f));
        g.drawText("SCENE SETUP",22,480,210,20,juce::Justification::centredLeft);
        g.drawText("SPATIAL FIELD",270,480,590,20,juce::Justification::centredLeft);
        g.drawText("OUTPUT",898,480,183,20,juce::Justification::centredLeft);

        // Values are text drawn by JUCE, not baked into texture.
        for(int i=0;i<8;++i){
            const double value=knobs_[static_cast<std::size_t>(i)].getValue();
            const juce::String display=i==0?juce::String(value,0)+" deg":
                i==1?juce::String(value,1)+" m":
                i==5?(value>0?"+":"")+juce::String(value,1)+" dB":
                i==6||i==7?juce::String(value,0)+" deg":
                juce::String(value,0)+"%";
            g.setColour(juce::Colour(0xff80e0d5));
            g.setFont(juce::FontOptions(10.5f));
            if(i==5) g.drawText(display,923,611,133,17,
                                juce::Justification::centred);
            else {
                const auto r=knobs_[static_cast<std::size_t>(i)].getBounds();
                g.drawText(display,r.getX()+4,r.getBottom()+1,
                           r.getWidth()-8,16,juce::Justification::centredRight);
            }
        }
        g.setColour(juce::Colour(0xff91b9bf));
        g.setFont(juce::FontOptions(10.f));
        g.drawText("NATIVE AUDIO: "+status_,20,658,837,15,
                   juce::Justification::centredLeft);
    }
    void resized() override {
        preset_.setBounds(20,523,213,31);
        room_.setBounds(20,591,213,30);
        const int idx[]={0,1,2,3,4,6,7};
        for(int j=0;j<7;++j) {
            const int row=j/4,col=j%4;
            const int x=273+col*151;
            const int y=518+row*78;
            auto& slider=knobs_[static_cast<std::size_t>(idx[j])];
            slider.setBounds(x,y+20,135,23);
            labels_[static_cast<std::size_t>(idx[j])].setBounds(x,y,135,18);
        }
        knobs_[5].setBounds(946,518,86,86);
        labels_[5].setBounds(917,619,140,17);
        power_.setBounds(894,646,99,29);
        close_.setBounds(1000,646,80,29);
    }
private:
    armix::sonku::Settings readControls() const {
        auto s=fx_.acceptedSettings();
        s.renderMode=9;s.moving=false;s.startAzimuthDegrees=0.f;
        s.musicCompatibleLowFrequency=false;
        s.spacePreset=std::clamp(room_.getSelectedId()-1,0,3);
        s.stereoSpreadDegrees=static_cast<float>(knobs_[0].getValue());
        s.sourceDistanceMeters=std::min(
            static_cast<float>(knobs_[1].getValue()),
            imersipP92MaxDistance(s.spacePreset));
        s.roomAmount=static_cast<float>(knobs_[2].getValue()*.01);
        s.ambience=static_cast<float>(knobs_[3].getValue()*.01);
        s.spatialMix=static_cast<float>(knobs_[4].getValue()*.01);
        s.outputTrim=static_cast<float>(
            std::pow(10.,knobs_[5].getValue()/20.));
        s.hrtfElevationTrimDegrees=
            static_cast<float>(knobs_[6].getValue());
        s.headYawDegrees=static_cast<float>(knobs_[7].getValue());
        return s;
    }
    void sync(const armix::sonku::Settings& s){
        room_.setSelectedId(s.spacePreset+1,juce::dontSendNotification);
        const double vals[]={s.stereoSpreadDegrees,s.sourceDistanceMeters,
            100.*s.roomAmount,100.*s.ambience,100.*s.spatialMix,
            20.*std::log10(s.outputTrim),s.hrtfElevationTrimDegrees,
            s.headYawDegrees};
        for(int i=0;i<8;++i)knobs_[static_cast<std::size_t>(i)].setValue(
            vals[i],juce::dontSendNotification);
        int preset=1;
        for(const auto& factory:imersipP92Presets){
            const int id=factory.id;
            const auto candidate=imersipP92Factory(id);
            if(s.spacePreset==candidate.spacePreset &&
               std::abs(s.stereoSpreadDegrees-candidate.stereoSpreadDegrees)<.01f &&
               std::abs(s.sourceDistanceMeters-candidate.sourceDistanceMeters)<.01f &&
               std::abs(s.roomAmount-candidate.roomAmount)<.001f &&
               std::abs(s.ambience-candidate.ambience)<.001f &&
               std::abs(s.spatialMix-candidate.spatialMix)<.001f &&
               std::abs(s.outputTrim-candidate.outputTrim)<.001f &&
               std::abs(s.headYawDegrees)<.001f &&
               std::abs(s.hrtfElevationTrimDegrees)<.001f) {preset=id;break;}
        }
        preset_.setSelectedId(preset,juce::dontSendNotification);
        repaint();
    }
    void submit() {
        const auto wanted=readControls();
        if(!validImerSipP92Scene(wanted)){
            status_="INVALID ROOM / SOURCE GEOMETRY";repaint();return;
        }
        if(pending_!=0){
            deferred_=wanted;hasDeferred_=true;
            status_="LATEST EDIT QUEUED";repaint();return;
        }
        const auto id=fx_.nextEditorRequestId();
        if(!fx_.queueSettings(wanted,id)){
            deferred_=wanted;hasDeferred_=true;
            status_="WAITING FOR AUDIO CALLBACK";repaint();return;
        }
        pending_=id;
        status_="PENDING NATIVE AUDIO ACK";
        repaint();
    }
    void refreshHostPower() {
        if(!readBypass_)return;
        power_.setButtonText(readBypass_()?"FX OFF":"FX ON");
    }
    void timerCallback() override {
        SonKuSpatialProcessor::Completion done{};
        while(fx_.pollCompletion(done)){}
        if(pending_!=0 && fx_.lastCompletedRequestId()==pending_){
            const bool ok=fx_.lastCompletionApplied();
            pending_=0;
            status_=ok?"APPLIED TO P9 PCM":"REJECTED BY P9 PCM";
            if(!ok || !hasDeferred_)sync(fx_.acceptedSettings());
        }
        if(pending_==0 && hasDeferred_){
            const auto next=deferred_;
            hasDeferred_=false;
            if(validImerSipP92Scene(next)){
                const auto id=fx_.nextEditorRequestId();
                if(fx_.queueSettings(next,id)){
                    pending_=id;status_="APPLYING LATEST EDIT";
                }else{
                    deferred_=next;hasDeferred_=true;
                }
            }
        }
        refreshHostPower();
        repaint(juce::Rectangle<int>(0,646,getWidth(),34));
    }
    ImerSipP92Processor& fx_;
    ImerSipP92VisualSkin skin_;
    juce::ComboBox preset_{},room_{};
    juce::TextButton power_{},close_{};
    std::array<juce::Slider,8> knobs_{};
    std::array<juce::Label,8> labels_{};
    std::function<bool(bool)> requestBypass_;
    std::function<bool()> readBypass_;
    armix::sonku::Settings deferred_{};
    std::uint64_t pending_=0;
    bool hasDeferred_=false;
    juce::String status_="P9.2 CONCERT ENGINE READY";
};
inline juce::AudioProcessorEditor* ImerSipP92Processor::createEditor() {
    return new ImerSipP92Editor(*this);
}
} // namespace armix::builtin_fx
