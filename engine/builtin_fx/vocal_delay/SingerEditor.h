// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "JuceVocalDelayProcessor.h"
#include "VocalDelayHardwareSkin.h"
#include <functional>
#include <optional>
#include <vector>
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
        preset_.addSectionHeading("Ready to Sing");
        for(std::size_t i=0;i<singerPresets.size();++i){
            if(i==stagePresetCount)preset_.addSectionHeading("Classic Presets");
            if(i==concertPresetStart)preset_.addSectionHeading("Concert Throw");
            preset_.addItem(singerPresets[i].name,int(i+1));
        }
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
        setupButton(music_,"vd.music");music_.setButtonText("MUSIC");music_.setTooltip("Choose shared backing music source or return to manual timing. Stage presets follow an existing shared clock; no source is guessed.");
        music_.onClick=[this]{chooseMusicSource();};
        setupButton(save_,"vd.save");save_.setButtonText("SAVE");save_.setTooltip("Save the accepted sound to My Presets");
        save_.onClick=[this]{promptSavePreset();};
        setupButton(expert_,"vd.expert");expert_.setButtonText("Expert");
        setupButton(smart_,"vd.smart");smart_.setButtonText("Smart");
        setupButton(stageToggle_,"vd.stageView");stageToggle_.setTooltip("Switch between one-click stage sounds and manual controls");
        stageToggle_.onClick=[this]{stageView_=!stageView_;resized();refresh();};
        setupButton(concertToggle_,"vd.concertView");concertToggle_.setTooltip("Switch continuous stage sounds and selective concert throws");
        concertToggle_.onClick=[this]{concertBank_=!concertBank_;resized();refresh();};
        for(std::size_t i=0;i<stagePresetCount;++i){
            const auto id="vd.stage."+juce::String(int(i));
            auto& b=stageButtons_[i];setupButton(b,id.toRawUTF8());
            b.setTitle(singerPresets[i].name);b.setButtonText(singerPresets[i].name);
            b.setTooltip(singerPresets[i].description);b.onClick=[this,i]{if(!concertBank_||i<concertPresetCount)recallFactory(concertBank_?concertPresetStart+i:i);};
        }
        setupButton(hit_,"vd.hit");hit_.setTooltip("Chorus highlight: more wet echo and repeats. Click again to return to base sound.");
        hit_.onClick=[this]{auto p=processor_.singerDisplay();p.hit=!p.hit;offer(p);};
        setupButton(tail_,"vd.tail");tail_.setTooltip("Keep existing echo tails when effect power is switched off. Rack bypass remains immediate.");
        tail_.onClick=[this]{auto p=processor_.singerDisplay();p.spillover=!p.spillover;offer(p);};
        bpm_.setComponentID("vd.bpm");bpm_.setTitle("Manual BPM");bpm_.setSliderStyle(juce::Slider::IncDecButtons);
        bpm_.setTextBoxStyle(juce::Slider::TextBoxLeft,false,68,21);bpm_.setRange(40,240,.25);
        bpm_.setTextValueSuffix(" BPM");bpm_.setLookAndFeel(&look_);addAndMakeVisible(bpm_);
        bpm_.onValueChange=[this]{if(hostAvailable())processor_.setMusicFollow(false);applyTempo(bpm_.getValue());};
        setupButton(focus_,"vd.focus");focus_.setTooltip("Adaptive wet echo reduction follows vocal activity. Off restores the classic detector.");
        focus_.onClick=[this]{auto p=processor_.singerDisplay();p.adaptiveFocus=!p.adaptiveFocus;offer(p);};
        smart_.setTooltip("Balance echo tone and ducking for your current time, repeats and level. Dry vocal stays unchanged.");
        expert_.setTooltip("Show precise controls without changing your sound.");
        expert_.onClick=[this]{expertMode_=!expertMode_;resized();refresh();};
        smart_.onClick=[this]{offer(smartVoicing(processor_.singerDisplay()));};
        attack_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duckAttackMs=float(attack_.getValue());offer(p);};
        release_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duckReleaseMs=float(release_.getValue());offer(p);};
        power_.onClick=[this]{auto p=processor_.singerDisplay();p.enabled=!p.enabled;if(p.enabled)p.hit=false;offer(p);};
        preset_.onChange=[this]{
            if(!hostAvailable())return;
            const int id=preset_.getSelectedId();
            if(id>0&&id<=int(singerPresets.size())){
                recallFactory(std::size_t(id-1));
            }else if(id>=1000&&std::size_t(id-1000)<userPresets_.size()&&recallUserPreset_){
                const auto entry=userPresets_[std::size_t(id-1000)];
                if(const auto p=recallUserPreset_(entry.id);p&&processor_.offerSinger(*p)){
                    processor_.setMusicFollow(false);
                    if(p->throwMode!=ThrowMode::off){stageView_=true;concertBank_=true;resized();}
                    selectedUserId_=entry.id;selectedUserSound_=*p;taps_.reset();tempoNotice_.clear();
                }
                refresh();
            }
        };
        mode_.onChange=[this]{const int id=mode_.getSelectedId();if(id>=1&&id<=3){auto p=processor_.singerDisplay();p.mode=static_cast<StereoMode>(id-1);offer(p);}};
        division_.onChange=[this]{if(!hostAvailable())return;processor_.setMusicFollow(false);const auto p=processor_.singerDisplay();applyTempo(p.bpmQuarter?double(p.bpmQuarter)*.25:120.);};
        tap_.onClick=[this]{if(!hostAvailable())return;processor_.setMusicFollow(false);double bpm=0;if(taps_.tap(juce::Time::getMillisecondCounterHiRes(),bpm)){applyTempo(bpm);}else{tempoNotice_="TAP 3+ BEATS";noticeUntil_=juce::Time::getMillisecondCounterHiRes()+1500.;repaint();}};
        time_.onValueChange=[this]{if(!hostAvailable())return;processor_.setMusicFollow(false);auto p=processor_.singerDisplay();freeTime(p,float(time_.getValue()));taps_.reset();tempoNotice_.clear();offer(p);};
        feedback_.onValueChange=[this]{auto p=processor_.singerDisplay();p.feedback=float(feedback_.getValue())*.01f;offer(p);};
        echo_.onValueChange=[this]{auto p=processor_.singerDisplay();p.echo=float(echo_.getValue())*.01f;offer(p);};
        duck_.onValueChange=[this]{auto p=processor_.singerDisplay();p.duck=float(duck_.getValue())*.01f;offer(p);};
        hp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(hp_.getValue());p.highPassHz=v<20?0.f:std::max(40.f,v);offer(p);};
        lp_.onValueChange=[this]{auto p=processor_.singerDisplay();const auto v=float(lp_.getValue());p.lowPassHz=v<500?0.f:std::max(1000.f,v);offer(p);};
        admitted_=processor_.beginSingerEditor();stageView_=processor_.singerDisplay().smartStage;concertBank_=processor_.singerDisplay().throwMode!=ThrowMode::off;resized();refresh();startTimerHz(30);
    }
    ~SingerEditor() override{
        stopTimer();if(admitted_)processor_.endSingerEditor();
        for(auto* d:dials())d->setLookAndFeel(nullptr);bpm_.setLookAndFeel(nullptr);
        for(auto* c:{&preset_,&mode_,&division_,&algorithm_})c->setLookAndFeel(nullptr);
        for(auto* b:{&power_,&tap_,&expert_,&smart_,&focus_,&hit_,&tail_,&save_,&music_,&stageToggle_,&concertToggle_})b->setLookAndFeel(nullptr);
        for(auto& b:stageButtons_)b.setLookAndFeel(nullptr);
    }
    // Host owns the exact retained FxInstance/workspace guard. No captured
    // slot ordinal is trusted after MOVE. UI may not edit a retired owner.
    void bindHostAccess(std::function<bool()> guard){hostAccess_=std::move(guard);refresh();}
    enum class HostBypass {none,slot,rack,global,unavailable};
    void bindHostBypass(std::function<HostBypass()> read){readBypass_=std::move(read);refresh();}
    struct MusicSource {std::uint64_t id=0,epoch=0,revision=0;juce::String name;};
    void bindMusicSources(std::function<std::vector<MusicSource>()> list,
                          std::function<bool(const MusicSource&)> select){
        listMusic_=std::move(list);selectMusic_=std::move(select);refresh();
    }
    struct UserPresetEntry {juce::String id,name;};
    void bindUserPresetService(std::function<std::vector<UserPresetEntry>()> list,
            std::function<juce::String(const juce::String&)> save,
            std::function<std::optional<Parameters>(const juce::String&)> recall){
        listUserPresets_=std::move(list);saveUserPreset_=std::move(save);recallUserPreset_=std::move(recall);
        refreshPresetList();refresh();
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
            if(stageView_){
                g.setColour(hardware::col(0xb2e7d3));g.drawText(concertBank_?"CONCERT THROW  /  SELECT A MOMENT":"READY TO SING  /  SELECT A SOUND",24,72,386,16,juce::Justification::centredLeft);
                if(concertBank_){g.setColour(hardware::col(0xa1afb1));g.drawText("Ordinary lyrics stay dry. Selected endings echo.",24,191,386,16,juce::Justification::centredLeft);}
            }else{
                const std::array<const char*,3> names{{"TIME","REPEATS","ECHO LEVEL"}};
                for(int i=0;i<3;++i)g.drawText(names[std::size_t(i)],24+i*132,78,122,18,juce::Justification::centred);
            }
            // Read actual block telemetry; no inferred or decorative waveform.
            hardware::inset(g,{426,79,172,138},5);
            g.setColour(hardware::col(0x9be7cf));g.setFont(hardware::font(11.f));
            g.drawText("VOCAL FOCUS",438,87,148,16,juce::Justification::centredLeft);
            drawMeter(g,"DUCK",meterFresh_?meter_.duckReduction:0.f,108);
            drawMeter(g,"WET",meterFresh_?meter_.wetPeak:0.f,140);
            g.setColour(hardware::col(0x91aaa3));g.setFont(hardware::font(9.f));
            const auto mode=processor_.singerDisplay().throwMode;
            const char* status="ARMED / WAITING";
            switch(meter_.throwStatus){case ConcertThrow::Status::tracking:status="TRACKING PHRASE";break;case ConcertThrow::Status::capture:status="THROW CAPTURE";break;case ConcertThrow::Status::cooldown:status="THROW COOLDOWN";break;default:break;}
            g.drawText(meterFresh_?(mode!=ThrowMode::off?status:(processor_.singerDisplay().smartStage&&processor_.singerDisplay().adaptiveFocus?"SMART STAGE / LIVE":"LIVE BLOCK PEAK")):"NO LIVE PCM",438,161,148,12,juce::Justification::centredLeft);
        }
        const auto p=processor_.singerDisplay();
        const auto tempo=tempoNotice_.isNotEmpty()?tempoNotice_:tempoLabel(p);
        g.setColour(juce::Colour(0xff829ca4));g.drawText(tempo,254,266,145,18,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff5fd7c3));g.drawText(statusLabel(),401,266,195,18,juce::Justification::centredRight);
    }
    void resized() override{
        preset_.setBounds(180,19,expertMode_?136:228,31);save_.setBounds(expertMode_?322:414,19,36,31);mode_.setBounds(366,19,92,31);
        expert_.setBounds(466,19,67,31);power_.setBounds(541,19,53,31);
        mode_.setVisible(expertMode_);focus_.setBounds(expertMode_?366:250,235,110,25);algorithm_.setVisible(expertMode_);algorithm_.setBounds(250,235,110,25);expert_.setButtonText(expertMode_?"Singer":"Expert");
        auto ds=dials();
        for(std::size_t i=0;i<ds.size();++i){
            ds[i]->setVisible(expertMode_||(!stageView_&&i<3));
            if(expertMode_){ds[i]->setBounds(14+int(i)*74,98,74,120);ds[i]->setTextBoxStyle(juce::Slider::TextBoxBelow,false,68,23);}
            else if(i<3){ds[i]->setBounds(24+int(i)*132,98,122,120);ds[i]->setTextBoxStyle(juce::Slider::TextBoxBelow,false,94,23);}
        }
        smart_.setVisible(!expertMode_);smart_.setBounds(440,177,69,27);
        stageToggle_.setVisible(!expertMode_);stageToggle_.setBounds(515,177,69,27);
        stageToggle_.setButtonText(stageView_?"Controls":"Presets");
        concertToggle_.setBounds(366,235,110,25);concertToggle_.setVisible(!expertMode_&&stageView_);
        for(std::size_t i=0;i<stagePresetCount;++i){
            auto& b=stageButtons_[i];b.setVisible(!expertMode_&&stageView_&&(!concertBank_||i<concertPresetCount));
            b.setBounds(24+int(i%3)*132,91+int(i/3)*31,126,26);
        }
        tap_.setBounds(24,235,74,25);division_.setBounds(108,235,130,25);
        hit_.setBounds(484,235,52,25);tail_.setBounds(542,235,54,25);bpm_.setBounds(24,265,148,22);music_.setBounds(180,265,68,22);
    }
    void refresh(){
        const bool available=admitted_&&hostAvailable()&&processor_.isReady()&&!processor_.singerFault();
        const auto p=processor_.singerDisplay();
        setDial(time_,p.timeMs);setDial(feedback_,p.feedback*100);setDial(echo_,p.echo*100);
        setDial(duck_,p.duck*100);setDial(hp_,p.highPassHz);setDial(lp_,p.lowPassHz);
        setDial(attack_,p.duckAttackMs);setDial(release_,p.duckReleaseMs);
        const auto style=styleIndex(p);
        int presetId=style<0?99:style+1;
        if(selectedUserId_.isNotEmpty()&&sameSound(p,selectedUserSound_)){
            for(std::size_t i=0;i<userPresets_.size();++i)if(userPresets_[i].id==selectedUserId_)presetId=int(i)+1000;
        }else selectedUserId_.clear();
        preset_.setSelectedId(presetId,juce::dontSendNotification);
        save_.setEnabled(available&&processor_.stateSnapshotQuiescent()&&bool(saveUserPreset_));
        smart_.setEnabled(available);stageToggle_.setEnabled(available);
        concertToggle_.setEnabled(available);concertToggle_.setButtonText(concertBank_?"Stage Bank":"Concert Bank");
        for(std::size_t i=0;i<stagePresetCount;++i){auto& b=stageButtons_[i];b.setEnabled(available);
            const auto idx=concertBank_&&i<concertPresetCount?concertPresetStart+i:i;
            b.setButtonText(singerPresets[idx].name);b.setTitle(singerPresets[idx].name);b.setTooltip(singerPresets[idx].description);
            const bool selected=style==int(idx)&&p.enabled&&selectedUserId_.isEmpty();
            b.setColour(juce::TextButton::textColourOffId,hardware::col(selected?0xc8f5e4:0xc5cccd));
            b.setColour(juce::TextButton::buttonColourId,hardware::col(selected?0x27695c:0x41494d));
        }
        hit_.setEnabled(available&&p.enabled&&!processor_.isDetached()&&hostBypass()==HostBypass::none);hit_.setButtonText(p.throwMode!=ThrowMode::off?(p.hit?"REARM":"THROW"):(p.hit?"HIT ON":"HIT"));
        hit_.setTooltip(p.throwMode!=ThrowMode::off?"One-shot recent vocal capture on rising edge; click to rearm":"Chorus highlight: more wet echo and repeats");
        hit_.setColour(juce::TextButton::buttonColourId,hardware::col(p.hit?0x88652d:0x41494d));
        tail_.setEnabled(available);tail_.setButtonText(p.spillover?"TAIL ON":"TAIL");
        tail_.setColour(juce::TextButton::buttonColourId,hardware::col(p.spillover?0x27695c:0x41494d));
        bpm_.setEnabled(available);setDial(bpm_,p.bpmQuarter?double(p.bpmQuarter)*.25:120.);
        division_.setSelectedId(int(p.division)+1,juce::dontSendNotification);
        focus_.setEnabled(available);focus_.setButtonText(p.adaptiveFocus?"Focus ON":"Focus OFF");
        focus_.setColour(juce::TextButton::buttonColourId,hardware::col(p.adaptiveFocus?0x27695c:0x41494d));
        mode_.setSelectedId(int(p.mode)+1,juce::dontSendNotification);
        algorithm_.setSelectedId(int(p.style)+1,juce::dontSendNotification);algorithm_.setEnabled(available);
        power_.setButtonText(p.enabled?"WET ON":"WET OFF");
        power_.setTooltip("Internal echo power. TAIL may continue; mixer rack bypass is separate.");
        music_.setEnabled(available&&!processor_.isDetached()&&bool(listMusic_)&&bool(selectMusic_));
        music_.setButtonText(processor_.musicFollowing()?"AUTO":"MUSIC");
        music_.setColour(juce::TextButton::buttonColourId,hardware::col(processor_.musicFollowing()?0x27695c:0x41494d));
        power_.setColour(juce::TextButton::buttonColourId,juce::Colour(p.enabled?0xff27695c:0xff3a444a));
        for(auto* d:dials())d->setEnabled(available);
        preset_.setEnabled(available);mode_.setEnabled(available);power_.setEnabled(available);tap_.setEnabled(available);division_.setEnabled(available);
        repaint();
    }
private:
    HostBypass hostBypass() const{return readBypass_?readBypass_():HostBypass::none;}
    juce::String statusLabel() const{
        if(!admitted_||!hostAvailable())return "UNAVAILABLE";
        if(processor_.isDetached())return "OFFLINE EDIT";
        switch(hostBypass()){
            case HostBypass::slot:return "SLOT BYPASSED";
            case HostBypass::rack:return "RACK BYPASSED";
            case HostBypass::global:return "GLOBAL FX OFF";
            case HostBypass::unavailable:return "UNAVAILABLE";
            default:break;
        }
        return processor_.singerFault()?"REOPEN REQUIRED":processor_.singerPending()?"APPLYING":meterFresh_?(processor_.singerDisplay().enabled?"READY TO SING":"WET OFF"):"NO LIVE PCM";
    }
    juce::String tempoLabel(const Parameters& p) const{
        using Status=MusicFollow::Status;
        switch(processor_.musicStatus()){
            case Status::listening:return p.bpmQuarter?"LISTENING / HOLD":"AUTO / SAFE TIME";
            case Status::holding:return "TEMPO HOLD";
            case Status::range:return "DIVISION LIMIT";
            case Status::locked:return processor_.singerPending()?"AUTO APPLYING":"AUTO LOCK";
            default:return p.bpmQuarter?"MANUAL / TAP":"FREE TIME";
        }
    }
    void chooseMusicSource(){
        if(!hostAvailable()||!listMusic_||!selectMusic_||processor_.isDetached())return;
        const auto sources=listMusic_();
        juce::PopupMenu menu;
        menu.addItem(1,"Manual / TAP (keep current time)");
        menu.addSectionHeader("Shared backing music source");
        for(std::size_t i=0;i<sources.size();++i)menu.addItem(int(i)+2,sources[i].name);
        const juce::Component::SafePointer<SingerEditor> weak(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&music_),[weak,sources](int id){
            if(!weak||!weak->hostAvailable())return;
            if(id==1)weak->processor_.setMusicFollow(false);
            else if(id>=2&&std::size_t(id-2)<sources.size()&&weak->selectMusic_&&
                    weak->selectMusic_(sources[std::size_t(id-2)]))
                weak->processor_.setMusicFollow(true);
            weak->refresh();
        });
    }
    static bool sameSound(Parameters a,Parameters b) noexcept{
        a.hit=b.hit=false;a.enabled=b.enabled=true;return sameParameters(a,b);
    }
    void recallFactory(std::size_t index){
        if(!hostAvailable()||index>=singerPresets.size())return;
        const auto p=presetSettings(index,index<stagePresetCount||index>=concertPresetStart||processor_.singerDisplay().enabled);
        if(processor_.offerSinger(p)){
            if(p.throwMode!=ThrowMode::off){concertBank_=true;stageView_=true;resized();}
            else if(index<stagePresetCount&&concertBank_){concertBank_=false;resized();}
            processor_.armStageTempo(p);selectedUserId_.clear();taps_.reset();tempoNotice_.clear();
        }
        refresh();
    }
    void refreshPresetList(){
        userPresets_=listUserPresets_?listUserPresets_():std::vector<UserPresetEntry>{};
        preset_.clear(juce::dontSendNotification);
        preset_.addItem("Custom",99);
        preset_.addSectionHeading("Ready to Sing");
        for(std::size_t i=0;i<singerPresets.size();++i){
            if(i==stagePresetCount)preset_.addSectionHeading("Classic Presets");
            if(i==concertPresetStart)preset_.addSectionHeading("Concert Throw");
            preset_.addItem(singerPresets[i].name,int(i+1));
        }
        if(!userPresets_.empty())preset_.addSectionHeading("My Presets");
        for(std::size_t i=0;i<userPresets_.size();++i)preset_.addItem(userPresets_[i].name,int(i)+1000);
    }
    void promptSavePreset(){
        if(!hostAvailable()||!saveUserPreset_||!processor_.stateSnapshotQuiescent())return;
        auto* dialog=new juce::AlertWindow("Save Vocal Delay preset","Save the current confirmed sound",juce::MessageBoxIconType::NoIcon,this);
        dialog->addTextEditor("presetName","","Preset name:");
        dialog->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<SingerEditor> weak(this);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([weak,dialog](int result){
            if(result!=1||weak==nullptr||!weak->hostAvailable()||!weak->saveUserPreset_||!weak->processor_.stateSnapshotQuiescent())return;
            const auto id=weak->saveUserPreset_(dialog->getTextEditorContents("presetName").trim());
            if(id.isEmpty())return;
            weak->selectedUserId_=id;weak->selectedUserSound_=weak->processor_.acceptedSnapshot().accepted;
            weak->refreshPresetList();weak->refresh();
        }),true);
    }
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
    bool hostAvailable() const{return !hostAccess_||hostAccess_();}
    void offer(const Parameters& p){if(hostAvailable()&&processor_.offerSinger(p))refresh();}
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
    JuceVocalDelayProcessor& processor_;hardware::Look look_;std::function<bool()> hostAccess_;
    juce::Slider time_,feedback_,echo_,duck_,hp_,lp_,attack_,release_,bpm_;juce::ComboBox preset_,mode_,division_,algorithm_;
    juce::TextButton power_,tap_,expert_,smart_,focus_,hit_,tail_,save_,music_,stageToggle_,concertToggle_;
    std::array<juce::TextButton,stagePresetCount> stageButtons_;
    std::function<HostBypass()> readBypass_;
    std::function<std::vector<MusicSource>()> listMusic_;
    std::function<bool(const MusicSource&)> selectMusic_;
    std::vector<UserPresetEntry> userPresets_;juce::String selectedUserId_;Parameters selectedUserSound_{};
    std::function<std::vector<UserPresetEntry>()> listUserPresets_;
    std::function<juce::String(const juce::String&)> saveUserPreset_;
    std::function<std::optional<Parameters>(const juce::String&)> recallUserPreset_;
    ContinuousDelayCore::Meter meter_{};double lastMeterMs_=-1.;bool meterFresh_=false;TapTempo taps_;double noticeUntil_=0;juce::String tempoNotice_;bool admitted_=false,expertMode_=false,stageView_=false,concertBank_=false;
};
inline juce::AudioProcessorEditor* JuceVocalDelayProcessor::createEditor(){return new SingerEditor(*this);}
} // namespace armix::builtin_fx::vocal_delay
