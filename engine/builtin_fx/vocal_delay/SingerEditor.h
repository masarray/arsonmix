// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "JuceVocalDelayProcessor.h"
#include "VocalDelayHardwareSkin.h"
namespace armix::builtin_fx::vocal_delay {
class SingerEditor final:public juce::AudioProcessorEditor,private juce::Timer{
public:
    explicit SingerEditor(JuceVocalDelayProcessor& p):AudioProcessorEditor(p),processor_(p){
        setSize(620,300);setResizable(false,false);
        look_.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffc6e9e3));
        look_.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff141d23));
        look_.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff34444d));
        setupCombo(preset_,"vd.preset");setupCombo(mode_,"vd.mode");setupCombo(division_,"vd.division");
        setupCombo(algorithm_,"vd.algorithm");
        for(const auto* n:{"Classic","Voiced","Dual","Multitap","Tape"})algorithm_.addItem(n,algorithm_.getNumItems()+1);
        algorithm_.onChange=[this]{const int id=algorithm_.getSelectedId();if(id>=1&&id<=5){auto p=processor_.singerDisplay();p.style=static_cast<EchoStyle>(id-1);offer(p);}};
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
        configure(attack_,"vd.attack","DUCK ATTACK",2,100,1," ms");
        configure(release_,"vd.release","DUCK RELEASE",40,1000,1," ms");
        setupButton(power_,"vd.power");setupButton(tap_,"vd.tap");tap_.setButtonText("TAP");
        setupButton(expert_,"vd.expert");expert_.setButtonText("Expert");
        setupButton(smart_,"vd.smart");smart_.setButtonText("Smart voicing");
        setupButton(hit_,"vd.hit");hit_.setTooltip("Chorus highlight: more wet echo and repeats. Click again to return to base sound.");
        hit_.onClick=[this]{auto p=processor_.singerDisplay();p.hit=!p.hit;offer(p);};
        setupButton(tail_,"vd.tail");tail_.setTooltip("Keep existing echo tails when effect power is switched off. Rack bypass remains immediate.");
        tail_.onClick=[this]{auto p=processor_.singerDisplay();p.spillover=!p.spillover;offer(p);};
        bpm_.setComponentID("vd.bpm");bpm_.setTitle("Manual BPM");bpm_.setSliderStyle(juce::Slider::IncDecButtons);
        bpm_.setTextBoxStyle(juce::Slider::TextBoxLeft,false,68,21);bpm_.setRange(40,240,.25);
        bpm_.setTextValueSuffix(" BPM");bpm_.setLookAndFeel(&look_);addAndMakeVisible(bpm_);
        bpm_.onValueChange=[this]{applyTempo(bpm_.getValue());};
        setupButton(focus_,"vd.focus");focus_.setTooltip("Adaptive wet echo reduction follows vocal activity. Off restores the classic detector.");
        focus_.onClick=[this]{auto p=processor_.singerDisplay();p.adaptiveFocus=!p.adaptiveFocus;offer(p);};
        smart_.setTooltip("Balance echo tone and ducking for your current time, repeats and level. Dry vocal stays unchanged.");
        expert_.setTooltip("Show precise controls without changing your sound.");
        expert_.onClick=[this]{expertMode_=!expertMode_;resized();refresh();};
        smart_.onClick=[this]{offer(smartVoicing(processor_.singerDisplay()));};
        attack_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duckAttackMs=float(attack_.getValue());offer(p);};
        release_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duckReleaseMs=float(release_.getValue());offer(p);};
        power_.onClick=[this]{auto p=processor_.singerDisplay();p.enabled=!p.enabled;offer(p);};
        preset_.onChange=[this]{const int id=preset_.getSelectedId();if(id>0&&id<=int(singerPresets.size())){
            taps_.reset();tempoNotice_.clear();
            offer(presetSettings(std::size_t(id-1),processor_.singerDisplay().enabled));}};
        mode_.onChange=[this]{const int id=mode_.getSelectedId();if(id>=1&&id<=3){auto p=processor_.singerDisplay();p.mode=static_cast<StereoMode>(id-1);offer(p);}};
        division_.onChange=[this]{const auto p=processor_.singerDisplay();applyTempo(p.bpmQuarter?double(p.bpmQuarter)*.25:120.);};
        tap_.onClick=[this]{double bpm=0;if(taps_.tap(juce::Time::getMillisecondCounterHiRes(),bpm)){applyTempo(bpm);}else{tempoNotice_="TAP 3+ BEATS";noticeUntil_=juce::Time::getMillisecondCounterHiRes()+1500.;repaint();}};
        time_.onValueChange=[this]{auto p=processor_.singerDisplay();freeTime(p,float(time_.getValue()));taps_.reset();tempoNotice_.clear();offer(p);};
        feedback_.onValueChange=[this]{auto p=processor_.singerDisplay();p.feedback=float(feedback_.getValue())*.01f;offer(p);};
        echo_.onValueChange=[this]{auto p=processor_.singerDisplay();p.echo=float(echo_.getValue())*.01f;offer(p);};
        duck_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duck=float(duck_.getValue())*.01f;offer(p);};
        hp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(hp_.getValue());p.highPassHz=v<20?0.f:std::max(40.f,v);offer(p);};
        lp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(lp_.getValue());p.lowPassHz=v<500?0.f:std::max(1000.f,v);offer(p);};
        admitted_=processor_.beginSingerEditor();resized();refresh();startTimerHz(30);
    }
    ~SingerEditor() override{
        stopTimer();if(admitted_)processor_.endSingerEditor();
        for(auto* d:dials())d->setLookAndFeel(nullptr);bpm_.setLookAndFeel(nullptr);
        for(auto* c:{&preset_,&mode_,&division_,&algorithm_})c->setLookAndFeel(nullptr);
        for(auto* b:{&power_,&tap_,&expert_,&smart_,&focus_,&hit_,&tail_})b->setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override{
        hardware::chassis(g,getWidth(),getHeight());
        hardware::inset(g,{174,15,424,39},5);
        g.setColour(hardware::col(0xdde2e3));g.setFont(hardware::font(20.f));g.drawText("Vocal Delay",23,15,148,24,juce::Justification::centredLeft);
        g.setColour(hardware::col(0xa1afb1));g.setFont(hardware::font(10.f));g.drawText("ARSONMIX   /   LIVE SERIES",24,40,150,13,juce::Justification::centredLeft);
        g.setColour(hardware::col(0x101416));g.drawLine(17,65,603,65,1.2f);
        g.setColour(hardware::col(0x555b5e));g.drawLine(17,66,603,66,1.f);
        g.setFont(hardware::font(11.f));g.setColour(hardware::col(0xc5cccd));
        if(expertMode_){
            const std::array<const char*,8> names{{"TIME","REPEATS","ECHO","DUCK","LOW CUT","HIGH CUT","ATTACK","RELEASE"}};
            for(int i=0;i<8;++i)g.drawText(names[std::size_t(i)],14+i*74,78,74,18,juce::Justification::centred);
        }else{
            const std::array<const char*,3> names{{"TIME","REPEATS","ECHO LEVEL"}};
            for(int i=0;i<3;++i)g.drawText(names[std::size_t(i)],24+i*132,78,122,18,juce::Justification::centred);
            // Read actual block telemetry; no inferred or decorative waveform.
            hardware::inset(g,{426,79,172,138},5);
            g.setColour(hardware::col(0x9be7cf));g.setFont(hardware::font(11.f));
            g.drawText("VOCAL FOCUS",438,87,148,16,juce::Justification::centredLeft);
            drawMeter(g,"DUCK",meterFresh_?meter_.duckReduction:0.f,108);
            drawMeter(g,"WET",meterFresh_?meter_.wetPeak:0.f,140);
            g.setColour(hardware::col(0x91aaa3));g.setFont(hardware::font(9.f));
            g.drawText(meterFresh_?"LIVE BLOCK PEAK":"NO LIVE PCM",438,161,148,12,juce::Justification::centredLeft);
        }
        const auto p=processor_.singerDisplay();
        const auto tempo=tempoNotice_.isNotEmpty()?tempoNotice_:(p.bpmQuarter?"SYNCED / MANUAL-TAP":"FREE TIME");
        g.setColour(juce::Colour(0xff829ca4));g.drawText(tempo,180,266,195,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff5fd7c3));g.drawText(!admitted_?"OFFLINE":processor_.singerFault()?"REOPEN REQUIRED":processor_.singerPending()?"APPLYING":"DSP READY",375,266,221,18,juce::Justification::centredRight);
    }
    void resized() override{
        preset_.setBounds(180,19,178,31);mode_.setBounds(366,19,92,31);
        expert_.setBounds(466,19,67,31);power_.setBounds(541,19,53,31);
        mode_.setVisible(expertMode_);focus_.setBounds(expertMode_?366:250,235,110,25);algorithm_.setVisible(expertMode_);algorithm_.setBounds(250,235,110,25);expert_.setButtonText(expertMode_?"Singer":"Expert");
        auto ds=dials();
        for(std::size_t i=0;i<ds.size();++i){
            ds[i]->setVisible(expertMode_||i<3);
            if(expertMode_){ds[i]->setBounds(14+int(i)*74,98,74,120);ds[i]->setTextBoxStyle(juce::Slider::TextBoxBelow,false,68,23);}
            else if(i<3){ds[i]->setBounds(24+int(i)*132,98,122,120);ds[i]->setTextBoxStyle(juce::Slider::TextBoxBelow,false,94,23);}
        }
        smart_.setVisible(!expertMode_);smart_.setBounds(440,177,144,27);
        tap_.setBounds(24,235,74,25);division_.setBounds(108,235,130,25);
        hit_.setBounds(484,235,52,25);tail_.setBounds(542,235,54,25);bpm_.setBounds(24,265,148,22);
    }
    void refresh(){
        const bool available=admitted_&&processor_.isReady()&&!processor_.singerFault();
        const auto p=processor_.singerDisplay();
        setDial(time_,p.timeMs);setDial(feedback_,p.feedback*100);setDial(echo_,p.echo*100);
        setDial(duck_,p.duck*100);setDial(hp_,p.highPassHz);setDial(lp_,p.lowPassHz);
        setDial(attack_,p.duckAttackMs);setDial(release_,p.duckReleaseMs);
        const auto style=styleIndex(p);
        preset_.setSelectedId(style<0?99:style+1,juce::dontSendNotification);
        smart_.setEnabled(available);
        hit_.setEnabled(available&&p.enabled);hit_.setButtonText(p.hit?"HIT ON":"HIT");
        hit_.setColour(juce::TextButton::buttonColourId,hardware::col(p.hit?0x88652d:0x41494d));
        tail_.setEnabled(available);tail_.setButtonText(p.spillover?"TAIL ON":"TAIL");
        tail_.setColour(juce::TextButton::buttonColourId,hardware::col(p.spillover?0x27695c:0x41494d));
        bpm_.setEnabled(available);setDial(bpm_,p.bpmQuarter?double(p.bpmQuarter)*.25:120.);
        division_.setSelectedId(int(p.division)+1,juce::dontSendNotification);
        focus_.setEnabled(available);focus_.setButtonText(p.adaptiveFocus?"Focus ON":"Focus OFF");
        focus_.setColour(juce::TextButton::buttonColourId,hardware::col(p.adaptiveFocus?0x27695c:0x41494d));
        mode_.setSelectedId(int(p.mode)+1,juce::dontSendNotification);
        algorithm_.setSelectedId(int(p.style)+1,juce::dontSendNotification);algorithm_.setEnabled(available);
        power_.setButtonText(p.enabled?"ON":"OFF");
        power_.setColour(juce::TextButton::buttonColourId,juce::Colour(p.enabled?0xff27695c:0xff3a444a));
        for(auto* d:dials())d->setEnabled(available);
        preset_.setEnabled(available);mode_.setEnabled(available);power_.setEnabled(available);tap_.setEnabled(available);division_.setEnabled(available);
        repaint();
    }
private:
    std::array<juce::Slider*,8> dials(){return {{&time_,&feedback_,&echo_,&duck_,&hp_,&lp_,&attack_,&release_}};}
    void configure(juce::Slider& s,const char* id,const char* name,double min,double max,double step,const char* unit){
        s.setComponentID(id);s.setName(name);s.setTitle(name);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,23);s.setRange(min,max,step);
        s.setTextValueSuffix(unit);s.setRotaryParameters(juce::MathConstants<float>::pi*1.28f,juce::MathConstants<float>::pi*2.72f,true);
        s.setMouseDragSensitivity(220);s.setLookAndFeel(&look_);addAndMakeVisible(s);
    }
    void setupCombo(juce::ComboBox& c,const char* id){c.setComponentID(id);c.setTitle(id);c.setLookAndFeel(&look_);c.setColour(juce::ComboBox::textColourId,hardware::col(0xb2e7d3));addAndMakeVisible(c);}
    void setupButton(juce::TextButton& b,const char* id){b.setComponentID(id);b.setTitle(id);b.setLookAndFeel(&look_);b.setColour(juce::TextButton::buttonColourId,hardware::col(0x41494d));addAndMakeVisible(b);}
    static void setDial(juce::Slider& d,double v){if(!d.isMouseButtonDown(true)&&!(d.hasKeyboardFocus(true)&&!d.hasKeyboardFocus(false)))d.setValue(v,juce::dontSendNotification);}
    void offer(const Parameters& p){if(processor_.offerSinger(p))refresh();}
    void applyTempo(double bpm){
        auto p=processor_.singerDisplay();
        const int d=division_.getSelectedId();
        if(d<=0||!setTempo(p,bpm,std::size_t(d-1))){
            tempoNotice_="TIME OUT OF RANGE";noticeUntil_=juce::Time::getMillisecondCounterHiRes()+1500.;refresh();return;
        }
        tempoNotice_.clear();offer(p);
    }
    void drawMeter(juce::Graphics& g,const char* name,float value,int y){
        g.setFont(hardware::font(9.f));g.setColour(hardware::col(0xa9bcb5));g.drawText(name,438,y,38,12,juce::Justification::centredLeft);
        for(int i=0;i<14;++i){
            const bool lit=value>0.f&&value>=float(i+1)/14.f;
            g.setColour(hardware::col(lit?0x75dab6:0x283d37));
            g.fillRoundedRectangle(478.f+float(i)*7.5f,float(y)+1.f,5.5f,10.f,1.f);
        }
    }
    void timerCallback() override{
        const auto now=juce::Time::getMillisecondCounterHiRes();
        if(now>=noticeUntil_)tempoNotice_.clear();
        const auto observed=processor_.meter();
        if(observed.sequence!=meter_.sequence){meter_=observed;lastMeterMs_=now;}
        meterFresh_=processor_.isReady()&&lastMeterMs_>=0&&now-lastMeterMs_<250.;
        refresh();
    }
    JuceVocalDelayProcessor& processor_;hardware::Look look_;
    juce::Slider time_,feedback_,echo_,duck_,hp_,lp_,attack_,release_,bpm_;juce::ComboBox preset_,mode_,division_,algorithm_;
    juce::TextButton power_,tap_,expert_,smart_,focus_,hit_,tail_;
    ContinuousDelayCore::Meter meter_{};double lastMeterMs_=-1.;bool meterFresh_=false;TapTempo taps_;double noticeUntil_=0;juce::String tempoNotice_;bool admitted_=false,expertMode_=false;
};
inline juce::AudioProcessorEditor* JuceVocalDelayProcessor::createEditor(){return new SingerEditor(*this);}
} // namespace armix::builtin_fx::vocal_delay
