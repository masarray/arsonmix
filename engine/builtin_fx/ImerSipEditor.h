// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Included from ImerSipProcessor.h after ImerSipProcessor is fully defined.
// The standard JUCE plugin editor is hosted by the EXISTING FxEditorWindow.
// UI gesture -> bounded SPSC -> native audio callback -> completion/readback.
// The editor cannot directly mutate audio-thread DSP state.
#include <array>
#include <cmath>
#include <cstdint>

namespace armix::builtin_fx {
class ImerSipLook final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
        float pos,float start,float end,juce::Slider&) override {
        const float cx=x+w*.5f,cy=y+h*.5f;
        const float radius=std::min(w,h)*.37f;
        const auto ink=juce::Colour(0xff86dcea);
        juce::Path track,active;
        track.addCentredArc(cx,cy,radius,radius,0.f,start,end,true);
        active.addCentredArc(cx,cy,radius,radius,0.f,start,
                             start+pos*(end-start),true);
        g.setColour(juce::Colour(0xff294757));
        g.strokePath(track,juce::PathStrokeType(3.f));
        g.setColour(ink);
        g.strokePath(active,juce::PathStrokeType(3.f));
        g.setColour(juce::Colour(0xff07151f));
        g.fillEllipse(cx-radius*.74f,cy-radius*.74f,
                      radius*1.48f,radius*1.48f);
        g.setColour(juce::Colour(0xff3f5866));
        g.drawEllipse(cx-radius*.74f,cy-radius*.74f,
                      radius*1.48f,radius*1.48f,1.f);
        const float a=start+pos*(end-start);
        const juce::Point<float> center(cx,cy);
        const auto tip=center+juce::Point<float>(std::sin(a)*radius*.58f,
                                                -std::cos(a)*radius*.58f);
        const auto base=center+juce::Point<float>(std::sin(a)*radius*.18f,
                                                 -std::cos(a)*radius*.18f);
        g.setColour(ink);
        g.drawLine(base.x,base.y,tip.x,tip.y,2.6f);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,
        const juce::Colour&,bool hover,bool pressed) override {
        const auto bounds=b.getLocalBounds().toFloat().reduced(.5f);
        const bool on=b.getToggleState();
        g.setColour(on?juce::Colour(0xff204c5d):
                    hover?juce::Colour(0xff1e3441):juce::Colour(0xff142832));
        g.fillRoundedRectangle(bounds,5.f);
        g.setColour(on?juce::Colour(0xff6dd2e5):
                    pressed?juce::Colour(0xff597a8b):juce::Colour(0xff385768));
        g.drawRoundedRectangle(bounds,5.f,1.f);
    }
};

class ImerSipEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer {
public:
    explicit ImerSipEditor(ImerSipProcessor& processor)
        :juce::AudioProcessorEditor(processor),fx_(processor) {
        setLookAndFeel(&look_);
        for(int i=0;i<3;++i) {
            auto& button=venues_[static_cast<std::size_t>(i)];
            button.setButtonText(i==0?"CONCERT HALL":i==1?"CINEMA":"LIVE ROOM");
            button.setClickingTogglesState(false);
            button.setColour(juce::TextButton::textColourOffId,
                             juce::Colour(0xffa9c7d4));
            button.setColour(juce::TextButton::textColourOnId,
                             juce::Colour(0xffe6faff));
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
                i==0?76.:i==1?68.:i==2?70.:i==3?34.:
                i==4?100.:i==5?55.:i==6?35.:0.);
            slider.onValueChange=[this,i]{
                refreshReadout(i);
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
            cap.setColour(juce::Label::textColourId,juce::Colour(0xff819ba8));
            addAndMakeVisible(cap);
            auto& value=values_[static_cast<std::size_t>(i)];
            value.setJustificationType(juce::Justification::centred);
            value.setFont(juce::FontOptions(12.f,juce::Font::plain));
            value.setColour(juce::Label::textColourId,juce::Colour(0xffdcf4fa));
            addAndMakeVisible(value);
        }
        // Depth is a REAL source-to-ear/wall distance: the old ImerSip
        // renderer was hard-coded at 1.6m with no externalization dial.
        depth_.setComponentID("imersip.depth");
        depth_.setSliderStyle(juce::Slider::LinearHorizontal);
        depth_.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        depth_.setRange(0.,100.,1.);
        depth_.setDoubleClickReturnValue(true,60.);
        depth_.onValueChange=[this]{
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
            originalAb_.setToggleState(false,juce::dontSendNotification);
            fx_.setCompareDry(false);
            sync(armix::imersip::Settings{});
            submit();
        };
        addAndMakeVisible(musicalReset_);
        sync(fx_.acceptedSettings());
        status_="READY · AUDIO-APPLIED CONTROLS";
        setSize(840,474);
        startTimerHz(25);
    }

    ~ImerSipEditor() override {
        stopTimer();
        // Closing the popup must NEVER leave the signal monitor bypassed.
        fx_.setCompareDry(false);
        setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff0e1c26));
        g.setColour(juce::Colour(0xff1a3443));
        g.fillRect(0,0,getWidth(),67);
        g.setColour(juce::Colour(0xff68cfe2));
        g.fillRect(0,66,getWidth(),1);
        g.setColour(juce::Colour(0xffebf7fb));
        g.setFont(juce::FontOptions(26.f,juce::Font::plain));
        g.drawText("ImerSip",22,14,190,31,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff91adbb));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        g.drawText("MUSIC STAGE  /  FRONTAL DEPTH  /  STEREO SAFE",
                   170,20,455,24,juce::Justification::centredLeft);
        const auto stageBox=stage_.toFloat();
        const auto controlBox=juce::Rectangle<float>(368.f,81.f,452.f,341.f);
        g.setColour(juce::Colour(0xff122632));
        g.fillRoundedRectangle(stageBox,9.f);
        g.fillRoundedRectangle(controlBox,9.f);
        g.setColour(juce::Colour(0xff2c4c5b));
        g.drawRoundedRectangle(stageBox,9.f,1.f);
        g.drawRoundedRectangle(controlBox,9.f,1.f);
        paintStage(g);
        g.setColour(juce::Colour(0xff8faebb));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        g.drawText("VENUE",385,94,110,20,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffa1c0cd));
        // No overlapping footer: the 8 readouts now occupy the lower row.
        g.setColour(juce::Colour(0xff244452));g.fillRect(0,440,840,1);
        g.setColour(juce::Colour(0xff84bbc7));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        g.drawText(status_,20,449,400,19,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff90d6e5));
        g.drawText(liveReadout_,422,449,233,19,
                   juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff6d8f9b));
        g.drawText("44.1 / 48 kHz  ·  HEADPHONES",650,449,170,19,
                   juce::Justification::centredRight);
    }
    void resized() override {
        stage_={19,81,335,341};
        for(int i=0;i<3;++i)
            venues_[static_cast<std::size_t>(i)].setBounds(385+i*141,121,134,35);
        originalAb_.setBounds(385,160,171,23);
        musicalReset_.setBounds(569,160,170,23);
        depth_.setBounds(63,337,248,22);
        earAlign_.setBounds(63,385,248,22);
        const int xs[]{382,488,594,700,382,488,594,700};
        const int ys[]{191,191,191,191,311,311,311,311};
        for(int i=0;i<8;++i) {
            const int x=xs[i],y=ys[i];
            knobs_[static_cast<std::size_t>(i)].setBounds(x+16,y,72,69);
            captions_[static_cast<std::size_t>(i)].setBounds(x,y+72,104,16);
            values_[static_cast<std::size_t>(i)].setBounds(x,y+91,104,17);
        }
    }
private:
    void paintStage(juce::Graphics& g) {
        const float cx=186.f,cy=226.f,r=89.f;
        g.setColour(juce::Colour(0xff80aebf));
        g.setFont(juce::FontOptions(11.f,juce::Font::plain));
        const float width=static_cast<float>(knobs_[1].getValue())*.01f;
        const int totalAngle=32+static_cast<int>(std::round(width*94));
        g.drawText("FRONT STAGE  /  "+juce::String(totalAngle)+"°",
                   74,99,225,18,juce::Justification::centred);
        g.setColour(juce::Colour(0xff79bdc9));
        const float depth=static_cast<float>(depth_.getValue())*.01f;
        const float metres=1.6f+5.8f*depth;
        g.drawText("SPEAKER DISTANCE   "+juce::String(metres,1)+" m",
                   56,314,260,18,juce::Justification::centred);
        g.drawText("EAR ALIGN  ·  KEMAR HRTF   "+
                   juce::String(earAlign_.getValue(),1)+"°",
                   56,362,260,18,juce::Justification::centred);
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
        const float angle=juce::degreesToRadians(16.f+47.f*width);
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
    void refreshVenues() {
        for(int i=0;i<3;++i)venues_[static_cast<std::size_t>(i)]
            .setToggleState(i==venue_,juce::dontSendNotification);
    }
    void refreshReadout(int i) {
        const float v=static_cast<float>(
            knobs_[static_cast<std::size_t>(i)].getValue());
        juce::String out;
        if(i==1)out=juce::String(32+static_cast<int>(std::round(.94*v)))+"°";
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
        repaint(stage_);
    }
    void submit() {
        const auto wanted=readControls();
        if(!armix::imersip::valid(wanted)) {
            status_="INVALID CONTROL VALUE · NO CHANGE";repaint();return;
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
        repaint(juce::Rectangle<int>(422,448,235,22));
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
    juce::TextButton originalAb_{},musicalReset_{};
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
