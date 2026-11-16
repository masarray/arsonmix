// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Uses one SonKu P9.1/P9.2 processor and its native SPSC/ACK; no UI DSP.
#include <array>
#include <cmath>
#include <functional>
#include <utility>

namespace armix::builtin_fx {
class ImerSipP92Editor final : public juce::AudioProcessorEditor,
                              private juce::Timer {
public:
    explicit ImerSipP92Editor(ImerSipP92Processor& processor)
        :juce::AudioProcessorEditor(processor),fx_(processor) {
        setSize(788,416);
        preset_.setComponentID("imersip.preset");
        preset_.addItem("Custom",1);
        for(const auto& scene:imersipP92Presets)
            preset_.addItem(juce::String(scene.label),scene.id);
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
        room_.onChange=[this]{
            preset_.setSelectedId(1,juce::dontSendNotification);
            submit();
        };
        addAndMakeVisible(room_);

        const char* ids[]={"stage","distance","early","late",
                          "mix","gain","align","yaw"};
        const char* names[]={"STAGE WIDTH","SOURCE DISTANCE","EARLY ROOM",
                             "LATE ROOM","SPATIAL MIX","OUTPUT LEVEL",
                             "EAR ALIGN","HEAD YAW"};
        const double lower[]={30.,1.6,0.,0.,0.,-12.,-30.,-90.};
        const double upper[]={180.,7.4,100.,100.,100.,6.,20.,90.};
        const double step[]={1.,.1,1.,1.,1.,.1,1.,1.};
        for(int i=0;i<8;++i){
            auto& knob=knobs_[static_cast<std::size_t>(i)];
            knob.setComponentID(juce::String("imersip.")+ids[i]);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            knob.setRange(lower[i],upper[i],step[i]);
            knob.setColour(juce::Slider::rotarySliderFillColourId,
                           juce::Colour(0xff62c9ae));
            knob.setColour(juce::Slider::rotarySliderOutlineColourId,
                           juce::Colour(0xff516a76));
            knob.setColour(juce::Slider::thumbColourId,
                           juce::Colour(0xffe4f0f2));
            knob.onValueChange=[this]{
                preset_.setSelectedId(1,juce::dontSendNotification);
                submit();
            };
            addAndMakeVisible(knob);
            labels_[static_cast<std::size_t>(i)].setText(
                names[i],juce::dontSendNotification);
            labels_[static_cast<std::size_t>(i)].setJustificationType(
                juce::Justification::centred);
            labels_[static_cast<std::size_t>(i)].setColour(
                juce::Label::textColourId,juce::Colour(0xffa9c5ce));
            labels_[static_cast<std::size_t>(i)].setFont(
                juce::FontOptions(11.f));
            addAndMakeVisible(labels_[static_cast<std::size_t>(i)]);
        }
        power_.setComponentID("imersip.power");
        power_.setButtonText("FX POWER");
        power_.setEnabled(false); // fail closed without a retained host slot
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
        close_.onClick=[this] {
            if(auto* window=findParentComponentOfClass<juce::DocumentWindow>())
                window->closeButtonPressed();
            else setVisible(false);
        };
        addAndMakeVisible(close_);
        sync(fx_.acceptedSettings());
        startTimerHz(25);
    }
    ~ImerSipP92Editor() override {stopTimer();}
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<bool()> readback) {
        requestBypass_=std::move(request);
        readBypass_=std::move(readback);
        power_.setEnabled(static_cast<bool>(requestBypass_) &&
                          static_cast<bool>(readBypass_));
        refreshHostPower();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff14232c));
        g.setColour(juce::Colour(0xff203945));
        g.fillRect(0,0,getWidth(),60);
        g.setColour(juce::Colour(0xff65bca9));
        g.fillRect(0,59,getWidth(),1);
        g.setColour(juce::Colour(0xffeaf5f6));
        g.setFont(juce::FontOptions(17.f));
        g.drawText("ImerSip",19,12,140,34,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff91b7c2));
        g.setFont(juce::FontOptions(11.f));
        g.drawText("SINGLE ENGINE  |  8D-SonKu P9.1/P9.2",19,63,330,18,
                   juce::Justification::centredLeft);
        g.drawText("L / R WORLD SPEAKERS  |  WORLD-POSITIONED BASS",
                   370,63,399,18,juce::Justification::centredRight);
        for(int i=0;i<8;++i){
            const int row=i/4,col=i%4;
            const int x=28+col*190,y=91+row*136;
            const double value=knobs_[static_cast<std::size_t>(i)].getValue();
            const juce::String display=i==0?juce::String(value,0)+"°":
                i==1?juce::String(value,1)+" m":
                i==5?(value>0?"+":"")+juce::String(value,1)+" dB":
                i==6||i==7?juce::String(value,0)+"°":
                juce::String(value,0)+"%";
            g.setFont(juce::FontOptions(12.f));
            g.setColour(juce::Colour(0xffd8ebed));
            g.drawText(display,x+30,y+83,106,18,
                       juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xff8eb2bf));
        g.setFont(juce::FontOptions(11.f));
        g.drawText("AUDIO ACK: "+status_,18,385,751,18,
                   juce::Justification::centredLeft);
    }
    void resized() override {
        preset_.setBounds(169,13,219,32);
        power_.setBounds(563,13,105,32);
        close_.setBounds(679,13,89,32);
        room_.setBounds(377,351,207,27);
        for(int i=0;i<8;++i){
            const int row=i/4,col=i%4;
            const int x=28+col*190,y=91+row*136;
            knobs_[static_cast<std::size_t>(i)].setBounds(x+37,y,96,82);
            labels_[static_cast<std::size_t>(i)].setBounds(x,y+104,170,19);
        }
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
            // Show the *accepted* physical distance after a room resize
            // (Studio 7.2 m cannot fit a 7.4 m speaker).
            // Do not erase the newest unacknowledged drag while deferred.
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
        repaint(juce::Rectangle<int>(0,383,getWidth(),28));
    }

    ImerSipP92Processor& fx_;
    juce::ComboBox preset_{},room_{};
    juce::TextButton power_{},close_{};
    std::array<juce::Slider,8> knobs_{};
    std::array<juce::Label,8> labels_{};
    std::function<bool(bool)> requestBypass_;
    std::function<bool()> readBypass_;
    armix::sonku::Settings deferred_{};
    std::uint64_t pending_=0;
    bool hasDeferred_=false;
    juce::String status_="REFERENCE P9.2 READY";
};
inline juce::AudioProcessorEditor* ImerSipP92Processor::createEditor() {
    return new ImerSipP92Editor(*this);
}
} // namespace armix::builtin_fx
