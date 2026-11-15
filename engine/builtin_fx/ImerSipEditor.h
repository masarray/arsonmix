// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Included from ImerSipProcessor.h after ImerSipProcessor is fully defined.
// The standard JUCE plugin editor is hosted by the EXISTING FxEditorWindow.
// UI gesture -> bounded SPSC -> native audio callback -> completion/readback.
// The editor cannot directly mutate audio-thread DSP state.
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace armix::builtin_fx {
class ImerSipLook final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
        float pos,float start,float end,juce::Slider&) override {
        const float cx=x+w*.5f,cy=y+h*.5f;
        const float radius=std::min(w,h)*.37f;
        const auto ink=juce::Colour(0xff58cfaa);
        juce::Path track,active;
        track.addCentredArc(cx,cy,radius,radius,0.f,start,end,true);
        active.addCentredArc(cx,cy,radius,radius,0.f,start,
                             start+pos*(end-start),true);
        g.setColour(juce::Colour(0xffa2b9bd));
        g.strokePath(track,juce::PathStrokeType(3.f));
        g.setColour(ink);
        g.strokePath(active,juce::PathStrokeType(3.f));
        juce::ColourGradient silver(juce::Colour(0xfffaffff),cx,cy-radius,
                                    juce::Colour(0xffb4c7cc),cx,cy+radius,false);
        g.setGradientFill(silver);
        g.fillEllipse(cx-radius*.74f,cy-radius*.74f,
                      radius*1.48f,radius*1.48f);
        g.setColour(juce::Colour(0xff789aa2));
        g.drawEllipse(cx-radius*.74f,cy-radius*.74f,
                      radius*1.48f,radius*1.48f,1.f);
        const float a=start+pos*(end-start);
        const juce::Point<float> center(cx,cy);
        const auto tip=center+juce::Point<float>(std::sin(a)*radius*.58f,
                                                -std::cos(a)*radius*.58f);
        const auto base=center+juce::Point<float>(std::sin(a)*radius*.18f,
                                                 -std::cos(a)*radius*.18f);
        g.setColour(juce::Colour(0xff244e59));
        g.drawLine(base.x,base.y,tip.x,tip.y,2.6f);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,
        const juce::Colour&,bool hover,bool pressed) override {
        const auto bounds=b.getLocalBounds().toFloat().reduced(.5f);
        const bool on=b.getToggleState();
        const bool head=b.getComponentID()=="imersip.power"||
                        b.getComponentID()=="imersip.close";
        g.setColour(head?(on?juce::Colour(0xff2f7469):
                           hover?juce::Colour(0xff365f69):juce::Colour(0xff294b55)):
                         on?juce::Colour(0xff529d8c):
                         hover?juce::Colour(0xffe4f1f0):juce::Colour(0xffedf4f4));
        g.fillRoundedRectangle(bounds,5.f);
        g.setColour(head?juce::Colour(0xff80cabb):
                         on?juce::Colour(0xff2d8776):
                         pressed?juce::Colour(0xff7ca3a8):juce::Colour(0xffadc3c7));
        g.drawRoundedRectangle(bounds,5.f,1.f);
    }
};

class ImerSipEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer {
public:
    static constexpr int editorWidth=980,editorHeight=560;
    static armix::imersip::Settings factoryPreset(int id) noexcept {
        armix::imersip::Settings s{};
        switch(id) {
        case 3: // Warm Hi-Fi: dry bass, controlled wet room
            s.immersion=.88f;s.width=.74f;s.space=.62f;
            s.lateRoom=.15f;s.bassImpact=.90f;s.detail=.42f;
            s.depth=.70f;break;
        case 4: // Grand Concert: real wide speakers, controlled hall
            s.immersion=.96f;s.width=.92f;s.space=.90f;
            s.lateRoom=.31f;s.bassImpact=.73f;s.detail=.65f;
            s.depth=.88f;break;
        case 5: // Open & Sparkling
            s.immersion=.96f;s.width=1.f;s.space=.76f;
            s.lateRoom=.18f;s.bassImpact=.72f;s.detail=.91f;
            s.depth=.80f;break;
        case 6: // Studio Reference, low room for clean A/B
            s.immersion=.68f;s.width=.70f;s.space=.28f;
            s.lateRoom=.05f;s.bassImpact=.40f;s.detail=.38f;
            s.depth=.55f;break;
        default:break;
        }
        return s;
    }
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<bool()> readback) {
        hostBypassRequest_=std::move(request);
        hostBypassReadback_=std::move(readback);
        power_.setEnabled(static_cast<bool>(hostBypassRequest_) &&
                          static_cast<bool>(hostBypassReadback_));
        refreshHostPower();
    }
    void resetTransientComparison() noexcept {
        originalAb_.setToggleState(false,juce::dontSendNotification);
        fx_.setCompareDry(false);
    }
    explicit ImerSipEditor(ImerSipProcessor& processor)
        :juce::AudioProcessorEditor(processor),fx_(processor) {
        setLookAndFeel(&look_);
        preset_.setComponentID("imersip.preset");
        preset_.addItem("Custom",1);
        preset_.addItem("Music Default",2);
        preset_.addItem("Warm Hi-Fi",3);
        preset_.addItem("Grand Concert",4);
        preset_.addItem("Open & Sparkling",5);
        preset_.addItem("Studio Reference",6);
        preset_.setColour(juce::ComboBox::backgroundColourId,
                          juce::Colour(0xff31535f));
        preset_.setColour(juce::ComboBox::textColourId,
                          juce::Colour(0xfff0f9f7));
        preset_.setColour(juce::ComboBox::outlineColourId,
                          juce::Colour(0xff649e9a));
        preset_.setColour(juce::ComboBox::arrowColourId,
                          juce::Colour(0xffa7f1d7));
        preset_.onChange=[this] {
            const auto id=preset_.getSelectedId();
            if(id<2||id>6)return;
            resetTransientComparison();
            sync(factoryPreset(id));
            submit(); // one existing native ACK, not a second renderer
        };
        addAndMakeVisible(preset_);
        close_.setComponentID("imersip.close");
        close_.setButtonText("X");
        close_.setColour(juce::TextButton::textColourOffId,juce::Colours::white);
        close_.onClick=[this] {
            resetTransientComparison();
            if(auto* w=findParentComponentOfClass<juce::DocumentWindow>())
                w->closeButtonPressed();
            else setVisible(false);
        };
        addAndMakeVisible(close_);
        for(int i=0;i<3;++i) {
            auto& button=venues_[static_cast<std::size_t>(i)];
            button.setButtonText(i==0?"CONCERT HALL":i==1?"CINEMA":"LIVE ROOM");
            button.setClickingTogglesState(false);
            button.setColour(juce::TextButton::textColourOffId,
                             juce::Colour(0xff244b58));
            button.setColour(juce::TextButton::textColourOnId,
                             juce::Colours::white);
            button.onClick=[this,i]{venue_=i;refreshVenues();submit();};
            addAndMakeVisible(button);
        }
        // The previous 6-knob editor hid stereo fidelity and linked early
        // and late energy; maxing all knobs could make music nearly mono.
        const char* names[]{"IMMERSION","STAGE WIDTH","EARLY SPACE","LATE TAIL",
                            "STEREO KEEP","BASS IMPACT","AIR DETAIL","OUTPUT"};
        const char* ids[]{"imersip.immersion","imersip.width","imersip.early",
                          "imersip.late","imersip.stereo","imersip.bass",
                          "imersip.air","imersip.output"};
        for(int i=0;i<8;++i) {
            auto& slider=knobs_[static_cast<std::size_t>(i)];
            slider.setComponentID(ids[i]);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setRange(i==7?-12.:0.,i==7?3.5:100.,i==7?.1:1.);
            slider.setRotaryParameters(juce::degreesToRadians(225.f),
                                       juce::degreesToRadians(495.f),true);
            slider.setDoubleClickReturnValue(true,
                i==0?88.:i==1?67.:i==2?78.:i==3?25.:
                i==4?100.:i==5?70.:i==6?60.:0.);
            slider.onValueChange=[this,i]{
                refreshReadout(i);
                preset_.setSelectedId(1,juce::dontSendNotification);
                // Previously ALL DSP changes were postponed to mouse-up.
                // During continuous rotary drag, feed the latest value to
                // the 25Hz control→PCM SPSC queue, without flooding it.
                if(knobs_[static_cast<std::size_t>(i)].isMouseButtonDown()) {
                    deferred_=readControls();hasDeferred_=true;
                } else submit();
                repaint(stage_);
            };
            slider.onDragEnd=[this]{submit();};
            addAndMakeVisible(slider);
            auto& cap=captions_[static_cast<std::size_t>(i)];
            cap.setText(names[i],juce::dontSendNotification);
            cap.setJustificationType(juce::Justification::centred);
            cap.setFont(juce::FontOptions(9.f,juce::Font::plain));
            cap.setColour(juce::Label::textColourId,juce::Colour(0xff54747d));
            addAndMakeVisible(cap);
            auto& value=values_[static_cast<std::size_t>(i)];
            value.setJustificationType(juce::Justification::centred);
            value.setFont(juce::FontOptions(12.f,juce::Font::plain));
            value.setColour(juce::Label::textColourId,juce::Colour(0xff183947));
            addAndMakeVisible(value);
        }
        // Depth is a REAL source-to-ear/wall distance: the old ImerSip
        // renderer was hard-coded at 1.6m with no externalization dial.
        depth_.setComponentID("imersip.depth");
        depth_.setSliderStyle(juce::Slider::LinearHorizontal);
        depth_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        depth_.setRange(0.,100.,1.);
        depth_.setDoubleClickReturnValue(true,78.);
        depth_.onValueChange=[this]{
            preset_.setSelectedId(1,juce::dontSendNotification);
            if(depth_.isMouseButtonDown()) {
                deferred_=readControls();hasDeferred_=true;
            } else submit();
            repaint(stage_);
        };
        depth_.onDragEnd=[this]{submit();};
        addAndMakeVisible(depth_);
        // Separate measured KEMAR DIRECT elevation; physical sources and
        // six room-wall directions remain at ear height (no fake floor XYZ).
        earAlign_.setComponentID("imersip.ear-align");
        earAlign_.setSliderStyle(juce::Slider::LinearHorizontal);
        earAlign_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        earAlign_.setRange(-30.,20.,.5);
        earAlign_.setDoubleClickReturnValue(true,0.);
        earAlign_.onValueChange=[this]{
            preset_.setSelectedId(1,juce::dontSendNotification);
            if(earAlign_.isMouseButtonDown()){
                deferred_=readControls();hasDeferred_=true;
            }else submit();
            repaint(stage_);
        };
        earAlign_.onDragEnd=[this]{submit();};
        addAndMakeVisible(earAlign_);
        originalAb_.setButtonText("ORIGINAL / A-B");
        originalAb_.setClickingTogglesState(true);
        originalAb_.setColour(juce::TextButton::textColourOffId,
                             juce::Colour(0xffaac9d6));
        originalAb_.setColour(juce::TextButton::textColourOnId,
                             juce::Colour(0xffe8faff));
        originalAb_.onClick=[this] {
            fx_.setCompareDry(originalAb_.getToggleState());
            status_=originalAb_.getToggleState()
                ?"ORIGINAL SOURCE / A-B ON (NOT SAVED)"
                :"PROCESSED OUTPUT / A-B OFF";
            repaint();
        };
        addAndMakeVisible(originalAb_);
        musicalReset_.setButtonText("MUSIC DEFAULT");
        musicalReset_.onClick=[this] {
            resetTransientComparison();
            sync(factoryPreset(2));
            submit();
        };
        addAndMakeVisible(musicalReset_);
        power_.setComponentID("imersip.power");
        power_.setButtonText("INSERT POWER");
        power_.setEnabled(false); // fail closed when not native-bound
        power_.onClick=[this] {
            if(!hostBypassRequest_||!hostBypassReadback_)return;
            const bool wantBypassed=!hostBypassReadback_();
            if(hostBypassRequest_(wantBypassed)) {
                status_=wantBypassed?"FX BYPASSED · NATIVE":"FX ENABLED · NATIVE";
                refreshHostPower();
            } else status_="POWER REJECTED · REFRESH FX RACK";
            repaint();
        };
        addAndMakeVisible(power_);
        sync(fx_.acceptedSettings());
        status_="READY · NATIVE AUDIO CONTROLS";
        setSize(editorWidth,editorHeight);
        startTimerHz(25);
    }

    ~ImerSipEditor() override {
        stopTimer();
        // Closing the popup must NEVER leave the signal monitor bypassed.
        resetTransientComparison();
        setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff0e1c26));
        g.setColour(juce::Colour(0xff1e3c48));
        g.fillRect(0,0,getWidth(),72);
        g.setColour(juce::Colour(0xff69d4ad));
        g.fillRect(0,71,getWidth(),1);
        g.setColour(juce::Colour(0xffebf7fb));
        g.setFont(juce::FontOptions(17.f,juce::Font::plain));
        g.drawText("◇ ImerSip",22,17,158,30,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff91adbb));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        g.drawText("BINAURAL SPATIAL ENGINE · NATIVE FX",
                   180,23,283,24,juce::Justification::centredLeft);
        const auto stageBox=stage_.toFloat();
        const auto controls=juce::Rectangle<float>(377.f,82.f,585.f,438.f);
        g.setColour(juce::Colour(0xff122632));
        g.fillRoundedRectangle(stageBox,7.f);
        g.setColour(juce::Colour(0xffd9e6e8));
        g.fillRoundedRectangle(controls,7.f);
        g.setColour(juce::Colour(0xff2c4c5b));
        g.drawRoundedRectangle(stageBox,7.f,1.f);
        g.setColour(juce::Colour(0xffafc4c7));
        g.drawRoundedRectangle(controls,7.f,1.f);
        paintStage(g);
        g.setColour(juce::Colour(0xff35616c));
        g.setFont(juce::FontOptions(10.5f,juce::Font::plain));
        g.drawText("VENUE",395,97,110,17,juce::Justification::centredLeft);
        g.drawText("SPACE / DIRECTION",395,207,210,16,
                   juce::Justification::centredLeft);
        g.drawText("FIDELITY / TONE",395,353,210,16,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffafc5c8));
        g.drawLine(395.f,344.f,944.f,344.f,1.f);
        g.setColour(juce::Colour(0xff5b7a83));
        g.setFont(juce::FontOptions(10.f,juce::Font::plain));
        // This toggles the individual native insert's bypassRequested;
        // channel FX rack master power remains a separate host authority.
        g.drawText("SLOT BYPASS · NATIVE FX    /    ORIGINAL A-B · TEMPORARY",
                   395,499,542,15,juce::Justification::centred);
        g.setColour(juce::Colour(0xff244452));
        g.fillRect(0,527,editorWidth,1);
        g.setColour(juce::Colour(0xff84bbc7));
        g.drawText(status_,22,534,444,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff90d6e5));
        g.drawText(liveReadout_,470,534,290,18,juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff7da6b2));
        g.drawText("44.1 / 48 kHz · HEADPHONES",777,534,185,18,
                   juce::Justification::centredRight);
    }
    void resized() override {
        stage_={18,82,346,438};
        for(int i=0;i<3;++i)
            venues_[static_cast<std::size_t>(i)].setBounds(394+i*185,120,172,36);
        originalAb_.setBounds(395,169,168,29);
        musicalReset_.setBounds(580,169,168,29);
        preset_.setBounds(470,18,226,34);
        power_.setBounds(717,18,170,34);
        close_.setBounds(906,18,49,34);
        depth_.setBounds(60,408,258,24);
        earAlign_.setBounds(60,466,258,24);
        const int xs[]{404,543,682,821,404,543,682,821};
        const int ys[]{224,224,224,224,370,370,370,370};
        for(int i=0;i<8;++i) {
            const int x=xs[i],y=ys[i];
            knobs_[static_cast<std::size_t>(i)].setBounds(x+17,y,91,81);
            captions_[static_cast<std::size_t>(i)].setBounds(x,y+84,127,17);
            values_[static_cast<std::size_t>(i)].setBounds(x,y+103,127,18);
        }
    }
private:
    void paintStage(juce::Graphics& g) {
        const float cx=191.f,cy=271.f,r=104.f;
        g.setColour(juce::Colour(0xff80aebf));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        const float width=static_cast<float>(knobs_[1].getValue())*.01f;
        const int totalAngle=30+static_cast<int>(std::round(width*150));
        const juce::String geometry=totalAngle>=150?
            "LATERAL STAGE  /  ":"FRONT STAGE  /  ";
        g.drawText(geometry+juce::String(totalAngle)+"°",
                   71,113,238,19,juce::Justification::centred);
        g.setColour(juce::Colour(0xff79bdc9));
        const float depth=static_cast<float>(depth_.getValue())*.01f;
        const float metres=1.6f+5.8f*depth;
        g.drawText("SPEAKER DISTANCE   "+juce::String(metres,1)+" m",
                   54,381,274,18,juce::Justification::centred);
        g.drawText("EAR ALIGN  ·  KEMAR HRTF   "+
                   juce::String(earAlign_.getValue(),1)+"°",
                   54,439,274,18,juce::Justification::centred);
        g.setColour(juce::Colour(0xff234452));
        g.drawEllipse(cx-r,cy-r,r*2,r*2,1.f);
        g.drawLine(cx-r,cy,cx+r,cy,1.f);
        g.drawLine(cx,cy-r,cx,cy+r,1.f);
        g.setColour(juce::Colour(0xff487486));
        g.fillEllipse(cx-19,cy-19,38,38);
        g.setColour(juce::Colour(0xff101f2b));
        g.fillEllipse(cx-14,cy-14,28,28);
        g.setColour(juce::Colour(0xffcceaf0));
        g.setFont(juce::FontOptions(12.f,juce::Font::plain));
        g.drawText("YOU",cx-22,cy+22,44,17,juce::Justification::centred);
        const float angle=juce::degreesToRadians(15.f+75.f*width);
        // Simple top-view cue: drawn radial distance tracks real DSP
        // source position rather than remaining decorative on all edits.
        const float positionRadius=(.60f+.38f*depth)*r;
        const float sx=std::sin(angle)*positionRadius;
        const float sy=std::cos(angle)*positionRadius;
        const auto left=juce::Point<float>(cx-sx,cy-sy);
        const auto right=juce::Point<float>(cx+sx,cy-sy);
        g.setColour(juce::Colour(0xff4d91a9));
        g.drawLine(cx,cy,left.x,left.y,1.4f);
        g.drawLine(cx,cy,right.x,right.y,1.4f);
        for(int i=0;i<2;++i) {
            const auto source=i==0?left:right;
            g.setColour(juce::Colour(0xff081c27));
            g.fillRoundedRectangle(source.x-17,source.y-28,34,55,6);
            g.setColour(juce::Colour(0xff7ed2e6));
            g.drawRoundedRectangle(source.x-17,source.y-28,34,55,6,1.3f);
            g.fillEllipse(source.x-5,source.y-16,10,10);
            g.drawEllipse(source.x-9,source.y,18,18,1.5f);
            g.setFont(juce::FontOptions(12.f,juce::Font::plain));
            g.drawText(i==0?"L":"R",
                       static_cast<int>(source.x-12),
                       static_cast<int>(source.y+29),24,17,
                       juce::Justification::centred);
        }
        // This is a TOP VIEW. HRTF ear-height adjustment changes spectral
        // pinna cues but must not move drawn source XYZ or reflections.
    }
    void refreshHostPower() {
        if(!hostBypassReadback_)return;
        const bool bypassed=hostBypassReadback_();
        const int state=bypassed?1:0;
        if(state==lastHostPowerState_)return;
        lastHostPowerState_=state;
        power_.setButtonText(bypassed?"INSERT OFF":"INSERT ON");
        power_.setToggleState(!bypassed,juce::dontSendNotification);
        power_.setTooltip(bypassed?
            "Enable this ImerSip insert without changing FX rack master power":
            "Bypass only this ImerSip insert with native rack fade");
    }
    void refreshVenues() {
        for(int i=0;i<3;++i)venues_[static_cast<std::size_t>(i)]
            .setToggleState(i==venue_,juce::dontSendNotification);
    }
    void refreshReadout(int i) {
        const float v=static_cast<float>(
            knobs_[static_cast<std::size_t>(i)].getValue());
        juce::String out;
        if(i==1)out=juce::String(30+static_cast<int>(std::round(1.5*v)))+"°";
        else if(i==7)out=(v>0.f?"+":"")+juce::String(v,1)+" dB";
        else out=juce::String(static_cast<int>(std::round(v)))+"%";
        values_[static_cast<std::size_t>(i)].setText(out,
            juce::dontSendNotification);
    }
    armix::imersip::Settings readControls() const {
        auto next=fx_.acceptedSettings();
        next.venue=venue_;
        next.immersion=static_cast<float>(knobs_[0].getValue()*.01);
        next.width=static_cast<float>(knobs_[1].getValue()*.01);
        next.space=static_cast<float>(knobs_[2].getValue()*.01);
        next.lateRoom=static_cast<float>(knobs_[3].getValue()*.01);
        next.stereoLock=static_cast<float>(knobs_[4].getValue()*.01);
        next.bassImpact=static_cast<float>(knobs_[5].getValue()*.01);
        next.detail=static_cast<float>(knobs_[6].getValue()*.01);
        next.outputTrim=static_cast<float>(
            std::pow(10.,knobs_[7].getValue()/20.));
        next.depth=static_cast<float>(depth_.getValue()*.01);
        next.earAlignDegrees=static_cast<float>(earAlign_.getValue());
        return next;
    }
    static bool matchesFactory(const armix::imersip::Settings& a,
                               const armix::imersip::Settings& b) noexcept {
        const auto near=[](float x,float y){return std::abs(x-y)<.001f;};
        return a.venue==b.venue&&near(a.immersion,b.immersion)&&
            near(a.space,b.space)&&near(a.width,b.width)&&
            near(a.bassImpact,b.bassImpact)&&near(a.detail,b.detail)&&
            near(a.outputTrim,b.outputTrim)&&near(a.stereoLock,b.stereoLock)&&
            near(a.lateRoom,b.lateRoom)&&near(a.depth,b.depth)&&
            near(a.earAlignDegrees,b.earAlignDegrees);
    }
    void sync(const armix::imersip::Settings& s) {
        venue_=s.venue;
        refreshVenues();
        depth_.setValue(s.depth*100.f,juce::dontSendNotification);
        earAlign_.setValue(s.earAlignDegrees,juce::dontSendNotification);
        const float vals[]{s.immersion*100.f,s.width*100.f,
                           s.space*100.f,s.lateRoom*100.f,
                           s.stereoLock*100.f,s.bassImpact*100.f,
                           s.detail*100.f,
                           static_cast<float>(20.*std::log10(s.outputTrim))};
        for(int i=0;i<8;++i) {
            knobs_[static_cast<std::size_t>(i)].setValue(vals[i],
                juce::dontSendNotification);
            refreshReadout(i);
        }
        int id=1;
        for(int i=2;i<=6;++i)
            if(matchesFactory(s,factoryPreset(i))) {id=i;break;}
        preset_.setSelectedId(id,juce::dontSendNotification);
        repaint(stage_);
    }
    void submit() {
        const auto wanted=readControls();
        if(!armix::imersip::valid(wanted)) {
            status_="INVALID CONTROL VALUE · NO CHANGE";repaint();return;
        }
        if(pending_!=0) {
            // If audio has ALREADY applied the previous gesture, consume
            // its latched ACK now. Waiting for the 25 Hz paint timer made
            // a rapid second drag appear inert, even though the DSP had
            // finished the first edit. This runs on JUCE CONTROL only.
            ImerSipProcessor::Completion ack{};
            while(fx_.pollCompletion(ack)) {}
            if(fx_.lastCompletedRequestId()==pending_) {
                const bool ok=fx_.lastCompletionApplied();
                pending_=0;
                if(!ok)status_="DSP REJECTED · PREVIOUS EDIT";
            }
        }
        if(pending_!=0) {
            deferred_=wanted;hasDeferred_=true;
            status_="APPLYING · LATEST EDIT QUEUED";repaint();return;
        }
        const auto number=fx_.nextEditorRequestId();
        if(!fx_.queueSettings(wanted,number)) {
            deferred_=wanted;hasDeferred_=true;
            status_="BUSY · AUDIO NOT YET APPLIED";repaint();return;
        }
        pending_=number;
        status_="APPLYING ON NATIVE AUDIO CALLBACK…";repaint();
    }
    void timerCallback() override {
        const auto frames=fx_.processedFrames();
        if(frames==lastObservedFrames_) {
            liveReadout_="NO PCM CALLBACK / BYPASSED";
        } else if(fx_.liveInputEnergy()<1.e-10f) {
            liveReadout_="SILENT INSERT INPUT";
        } else {
            // PCM difference alone was incorrectly certified as spatial
            // "quality". Show actual output SIDE energy relative to
            // original stereo, normalized to final output trim.
            const float db=10.f*std::log10(
                std::max(fx_.liveDifferenceEnergy(),1.e-12f));
            const float stereo=fx_.liveStereoRetention();
            liveReadout_=stereo<0.f?
                "MONO INPUT / PCM "+juce::String(db,1)+"dB":
                "STEREO "+juce::String(stereo,2)+"x / PCM "+
                juce::String(db,1)+"dB";
        }
        lastObservedFrames_=frames;
        refreshHostPower();
        repaint(juce::Rectangle<int>(470,532,294,23));
        // The host may save/serialize this FX while its editor is open.
        // Save also drains audio completions to capture accepted state.
        // Read the latched CONTROL-side record, not only a FIFO pop.
        ImerSipProcessor::Completion completion{};
        while(fx_.pollCompletion(completion)){}
        if(pending_!=0 && fx_.lastCompletedRequestId()==pending_) {
            const bool applied=fx_.lastCompletionApplied();
            pending_=0;
            status_=applied?"AUDIO APPLIED · VERIFIED":
                            "DSP REJECTED · SETTINGS UNCHANGED";
            if(!applied)sync(fx_.acceptedSettings());
            repaint();
        }
        if(pending_==0&&hasDeferred_) {
            hasDeferred_=false;
            submit();  // most recent dial gesture, not every mouse move
        }
    }
    ImerSipProcessor& fx_;
    ImerSipLook look_{};
    std::array<juce::TextButton,3> venues_{};
    juce::TextButton originalAb_{},musicalReset_{},power_{},close_{};
    juce::ComboBox preset_{};
    std::function<bool(bool)> hostBypassRequest_;
    std::function<bool()> hostBypassReadback_;
    int lastHostPowerState_=-1;
    juce::Slider depth_{},earAlign_{};
    std::array<juce::Slider,8> knobs_{};
    std::array<juce::Label,8> captions_{},values_{};
    juce::Rectangle<int> stage_{};
    armix::imersip::Settings deferred_{};
    juce::String status_;
    juce::String liveReadout_="PCM DELTA —";
    std::uint64_t lastObservedFrames_=0,pending_=0;
    int venue_=0;
    bool hasDeferred_=false;
};

inline juce::AudioProcessorEditor* ImerSipProcessor::createEditor() {
    return new ImerSipEditor(*this);
}
} // namespace armix::builtin_fx
