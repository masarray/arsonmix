// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "JuceVocalDelayProcessor.h"
namespace armix::builtin_fx::vocal_delay {
class SingerDialLook final:public juce::LookAndFeel_V4{
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,
                         float start,float end,juce::Slider& s) override{
        const float size=float(juce::jmin(w,h))-12.f;
        if(size<=4)return;
        const float cx=float(x)+float(w)*.5f,cy=float(y)+float(h)*.5f,r=size*.5f;
        const auto face=juce::Rectangle<float>(cx-r+7,cy-r+7,size-14,size-14);
        juce::Path rail;rail.addCentredArc(cx,cy,r,r,0,start,end,true);
        g.setColour(juce::Colour(0xff36424a));g.strokePath(rail,juce::PathStrokeType(2.6f));
        juce::Path arc;const float angle=start+pos*(end-start);
        arc.addCentredArc(cx,cy,r,r,0,start,angle,true);
        g.setColour(juce::Colour(s.isEnabled()?0xff5fd7c3:0xff52636a));
        g.strokePath(arc,juce::PathStrokeType(3.f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff46525a),cx,cy-r,
                              juce::Colour(0xff202930),cx,cy+r,false));
        g.fillEllipse(face);g.setColour(juce::Colour(0xff64747d));g.drawEllipse(face,1.f);
        juce::Path mark;mark.addRoundedRectangle(-1.5f,-r+13.f,3.f,12.f,1.f);
        mark.applyTransform(juce::AffineTransform::rotation(angle).translated(cx,cy));
        g.setColour(juce::Colour(0xffe6f4f0));g.fillPath(mark);
    }
};
class SingerEditor final:public juce::AudioProcessorEditor,private juce::Timer{
public:
    explicit SingerEditor(JuceVocalDelayProcessor& p):AudioProcessorEditor(p),processor_(p){
        setSize(620,300);setResizable(false,false);
        look_.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffc6e9e3));
        look_.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff141d23));
        look_.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff34444d));
        setupCombo(preset_,"vd.preset");setupCombo(mode_,"vd.mode");setupCombo(division_,"vd.division");
        preset_.addItem("Custom",99);
        for(std::size_t i=0;i<singerPresets.size();++i)preset_.addItem(singerPresets[i].name,int(i+1));
        mode_.addItem("Mono",1);mode_.addItem("Stereo",2);mode_.addItem("Ping Pong",3);
        const std::array<const char*,7> divisions{{"1/16","1/8","Dotted 1/8","1/4","Dotted 1/4","1/2","Whole"}};
        for(std::size_t i=0;i<divisions.size();++i)division_.addItem(divisions[i],int(i+1));
        division_.setSelectedId(4,juce::dontSendNotification);
        configure(time_, "vd.time","TIME",20,4000,1," ms");
        configure(feedback_,"vd.feedback","FEEDBACK",0,90,1," %");
        configure(echo_,"vd.echo","ECHO",0,100,1," %");
        configure(duck_,"vd.duck","DUCK",0,100,1," %");
        configure(hp_,"vd.hp","LOW CUT",0,1000,1," Hz");
        configure(lp_,"vd.lp","HIGH CUT",0,20000,1," Hz");
        time_.setSkewFactorFromMidPoint(400);hp_.setSkewFactorFromMidPoint(200);lp_.setSkewFactorFromMidPoint(5000);
        setupButton(power_,"vd.power");setupButton(tap_,"vd.tap");tap_.setButtonText("TAP");
        power_.onClick=[this]{auto p=processor_.singerDisplay();p.enabled=!p.enabled;offer(p);};
        preset_.onChange=[this]{const int id=preset_.getSelectedId();if(id>0&&id<=int(singerPresets.size()))
            offer(presetSettings(std::size_t(id-1),processor_.singerDisplay().enabled));};
        mode_.onChange=[this]{const int id=mode_.getSelectedId();if(id>=1&&id<=3){auto p=processor_.singerDisplay();p.mode=static_cast<StereoMode>(id-1);offer(p);}};
        division_.onChange=[this]{applyTempo();};
        tap_.onClick=[this]{double bpm=0;if(taps_.tap(juce::Time::getMillisecondCounterHiRes(),bpm)){bpm_=bpm;applyTempo();}else tempoStatus_="TAP 3+ beats";repaint();};
        time_.onValueChange=[this]{auto p=processor_.singerDisplay();p.timeMs=float(time_.getValue());tempoStatus_="FREE TIME";bpm_=0;offer(p);};
        feedback_.onValueChange=[this]{auto p=processor_.singerDisplay();p.feedback=float(feedback_.getValue())*.01f;offer(p);};
        echo_.onValueChange=[this]{auto p=processor_.singerDisplay();p.echo=float(echo_.getValue())*.01f;offer(p);};
        duck_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duck=float(duck_.getValue())*.01f;offer(p);};
        hp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(hp_.getValue());p.highPassHz=v<20?0.f:std::max(40.f,v);offer(p);};
        lp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(lp_.getValue());p.lowPassHz=v<500?0.f:std::max(1000.f,v);offer(p);};
        admitted_=processor_.beginSingerEditor();refresh();startTimerHz(30);
    }
    ~SingerEditor() override{
        stopTimer();if(admitted_)processor_.endSingerEditor();
        for(auto* d:dials())d->setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override{
        g.fillAll(juce::Colour(0xff141c22));
        g.setColour(juce::Colour(0xff202c33));g.fillRoundedRectangle(12,10,float(getWidth()-24),50,7);
        g.setColour(juce::Colour(0xffdbe9e7));g.setFont(17.f);g.drawText("Vocal Delay",24,17,160,22,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff829ca4));g.setFont(10.f);g.drawText("CONTINUOUS  /  LIVE",24,39,155,13,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff243139));g.fillRoundedRectangle(12,70,float(getWidth()-24),157,7);
        const std::array<const char*,6> names{{"TIME","FEEDBACK","ECHO","DUCK","LOW CUT","HIGH CUT"}};
        g.setFont(11.f);g.setColour(juce::Colour(0xffb5c8cb));
        for(int i=0;i<6;++i)g.drawText(names[std::size_t(i)],18+i*98,78,94,18,juce::Justification::centred);
        g.setColour(juce::Colour(0xff829ca4));g.drawText(tempoStatus_,24,266,220,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff5fd7c3));g.drawText(!admitted_?"OFFLINE":processor_.singerFault()?"REOPEN REQUIRED":processor_.singerPending()?"APPLYING":"DSP READY",375,266,221,18,juce::Justification::centredRight);
    }
    void resized() override{
        preset_.setBounds(190,19,190,31);mode_.setBounds(392,19,110,31);power_.setBounds(514,19,80,31);
        auto ds=dials();for(int i=0;i<6;++i)ds[std::size_t(i)]->setBounds(18+i*98,98,94,120);
        tap_.setBounds(24,235,74,25);division_.setBounds(108,235,130,25);
    }
    void refresh(){
        const bool available=admitted_&&processor_.isReady()&&!processor_.singerFault();
        const auto p=processor_.singerDisplay();
        setDial(time_,p.timeMs);setDial(feedback_,p.feedback*100);setDial(echo_,p.echo*100);
        setDial(duck_,p.duck*100);setDial(hp_,p.highPassHz);setDial(lp_,p.lowPassHz);
        int id=99;for(std::size_t i=0;i<singerPresets.size();++i)
            if(sameParameters(p,presetSettings(i,p.enabled)))id=int(i+1);
        preset_.setSelectedId(id,juce::dontSendNotification);
        mode_.setSelectedId(int(p.mode)+1,juce::dontSendNotification);
        power_.setButtonText(p.enabled?"ON":"OFF");
        power_.setColour(juce::TextButton::buttonColourId,juce::Colour(p.enabled?0xff27695c:0xff3a444a));
        for(auto* d:dials())d->setEnabled(available);
        preset_.setEnabled(available);mode_.setEnabled(available);power_.setEnabled(available);tap_.setEnabled(available);division_.setEnabled(available);
        repaint();
    }
private:
    std::array<juce::Slider*,6> dials(){return {{&time_,&feedback_,&echo_,&duck_,&hp_,&lp_}};}
    void configure(juce::Slider& s,const char* id,const char* name,double min,double max,double step,const char* unit){
        s.setComponentID(id);s.setName(name);s.setTitle(name);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,23);s.setRange(min,max,step);
        s.setTextValueSuffix(unit);s.setRotaryParameters(juce::MathConstants<float>::pi*1.28f,juce::MathConstants<float>::pi*2.72f,true);
        s.setMouseDragSensitivity(220);s.setLookAndFeel(&look_);addAndMakeVisible(s);
    }
    void setupCombo(juce::ComboBox& c,const char* id){c.setComponentID(id);c.setTitle(id);addAndMakeVisible(c);}
    void setupButton(juce::TextButton& b,const char* id){b.setComponentID(id);b.setTitle(id);addAndMakeVisible(b);}
    static void setDial(juce::Slider& d,double v){if(!d.isMouseButtonDown(true)&&!(d.hasKeyboardFocus(true)&&!d.hasKeyboardFocus(false)))d.setValue(v,juce::dontSendNotification);}
    void offer(const Parameters& p){if(processor_.offerSinger(p))refresh();}
    void applyTempo(){
        if(bpm_<=0)return;
        float ms=0;const int d=division_.getSelectedId();
        if(d<=0||!beatTime(bpm_,std::size_t(d-1),ms)){tempoStatus_="TIME OUT OF RANGE";repaint();return;}
        auto p=processor_.singerDisplay();p.timeMs=ms;tempoStatus_=juce::String(bpm_,1)+" BPM";offer(p);
    }
    void timerCallback() override{refresh();}
    JuceVocalDelayProcessor& processor_;SingerDialLook look_;
    juce::Slider time_,feedback_,echo_,duck_,hp_,lp_;juce::ComboBox preset_,mode_,division_;
    juce::TextButton power_,tap_;TapTempo taps_;double bpm_=0;juce::String tempoStatus_="FREE TIME";bool admitted_=false;
};
inline juce::AudioProcessorEditor* JuceVocalDelayProcessor::createEditor(){return new SingerEditor(*this);}
} // namespace armix::builtin_fx::vocal_delay
