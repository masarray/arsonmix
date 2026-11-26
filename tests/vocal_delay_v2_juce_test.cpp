// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/JuceVocalDelayProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace armix::builtin_fx::vocal_delay;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
    juce::ScopedJuceInitialiser_GUI init;
    JuceVocalDelayProcessor p;State s{};s.continuous.timeMs=20;s.continuous.echo=.5f;
    s.continuous.feedback=0;s.continuous.duck=0;s.continuous.highPassHz=0;s.continuous.lowPassHz=0;
    StateCodec::Bytes bytes{};CHECK(StateCodec::encode(s,bytes));
    CHECK(p.restoreUnpublishedState(bytes.data(),int(bytes.size())));
    Lease lease{1,2,3,4};CHECK(p.bindLeaseStopped(lease));p.prepareToPlay(48000,128);
    CHECK(p.isReady()&&p.getLatencySamples()==0&&!p.acceptedSnapshot().accepted.adaptiveFocus);p.sealForRealtime();
    CHECK(!p.restoreUnpublishedState(bytes.data(),int(bytes.size())));
    juce::AudioBuffer<float> pcm(2,2000);juce::MidiBuffer midi;
    pcm.clear();pcm.setSample(0,0,.8f);pcm.setSample(1,0,.8f);
    p.processBlock(pcm,midi);CHECK(pcm.getSample(0,0)==.8f&&std::abs(pcm.getSample(0,960)-.4f)<1.e-6f);
    auto q=s.continuous;q.timeMs=30;CHECK(p.submit({lease,1,q})==Submit::queued);
    juce::MemoryBlock saved;p.getStateInformation(saved);CHECK(saved.getSize()==0);
    p.onRealtimeBypass(true,true);p.consumeRealtimeEdits();
    Receipt r{};CHECK(p.poll(r)&&r.applied&&r.accepted.timeMs==30);
    p.getStateInformation(saved);CHECK(saved.getSize()==48);State restored{};
    CHECK(StateCodec::decode(saved.getData(),saved.getSize(),restored)&&restored.continuous.timeMs==30);
    p.releaseResources();CHECK(p.bindLeaseStopped({1,2,4,5}));p.prepareToPlay(96000,128);
    CHECK(p.isReady()&&p.acceptedSnapshot().accepted.timeMs==30);
    CHECK(p.submit({lease,2,q})==Submit::stale);
    pcm.setSize(2,4000);pcm.clear();pcm.setSample(0,0,.8f);pcm.setSample(1,0,.8f);
    p.processBlock(pcm,midi);CHECK(std::abs(pcm.getSample(0,2880)-.4f)<1.e-6f);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        CHECK(editor&&editor->getWidth()==620&&editor->getHeight()==300);
        CHECK(hardware::knob().isValid());
        auto capture=[&](const char* name){
            const auto im=editor->createComponentSnapshot(editor->getLocalBounds(),true,2.f);
            CHECK(im.isValid()&&im.getWidth()==1240&&im.getHeight()==600);
            auto file=juce::File::getCurrentWorkingDirectory().getChildFile(name);
            if(file.existsAsFile())CHECK(file.deleteFile());
            auto stream=file.createOutputStream();CHECK(stream&&juce::PNGImageFormat().writeImageToStream(im,*stream));
        };
        capture("vocal-delay-singer.png");
        auto* time=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.time"));
        auto* echo=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.echo"));
        auto* preset=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("vd.preset"));
        CHECK(time&&echo&&preset&&time->isEnabled());
        auto* hit=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.hit"));
        auto* tail=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.tail"));
        auto* bpm=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.bpm"));
        CHECK(hit&&tail&&bpm&&hit->isVisible()&&tail->isVisible()&&bpm->isVisible());
        auto* focus=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.focus"));CHECK(focus&&focus->isVisible()&&focus->getButtonText()=="Focus OFF");
        auto* expert=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.expert"));
        auto* duck=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.duck"));
        auto* attack=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.attack"));
        auto* smart=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.smart"));
        CHECK(expert&&duck&&attack&&smart&&!duck->isVisible()&&!attack->isVisible()&&smart->isVisible());
        const auto before=p.singerDisplay();const auto rev=p.acceptedSnapshot().revision;
        expert->onClick();CHECK(duck->isVisible()&&attack->isVisible()&&!smart->isVisible());
        CHECK(editor->findChildWithID("vd.algorithm")->isVisible());capture("vocal-delay-expert.png");
        expert->onClick();CHECK(!duck->isVisible()&&smart->isVisible());
        CHECK(sameParameters(before,p.singerDisplay())&&!p.singerPending()&&p.acceptedSnapshot().revision==rev);
        preset->setSelectedId(4,juce::sendNotificationSync);CHECK(p.singerDisplay().adaptiveFocus&&p.singerDisplay().spillover&&p.singerDisplay().smartStage);
        auto* stageView=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.stageView"));CHECK(stageView);
        stageView->onClick();CHECK(editor->findChildWithID("vd.stage.0")->isVisible()&&!time->isVisible());capture("vocal-delay-stage.png");
        stageView->onClick();CHECK(time->isVisible());
        bpm->setValue(100,juce::sendNotificationSync);CHECK(p.singerDisplay().bpmQuarter==400&&p.singerDisplay().timeMs==600);
        const auto base=p.singerDisplay();hit->onClick();CHECK(p.singerDisplay().hit&&p.singerDisplay().echo==base.echo);
        hit->onClick();CHECK(sameParameters(base,p.singerDisplay()));
        auto* power=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.power"));CHECK(power);
        hit->onClick();power->onClick();CHECK(!p.singerDisplay().enabled);
        power->onClick();CHECK(sameParameters(base,p.singerDisplay()));
        tail->onClick();CHECK(!p.singerDisplay().spillover);tail->onClick();CHECK(p.singerDisplay().spillover);
        focus->onClick();CHECK(!p.singerDisplay().adaptiveFocus);focus->onClick();CHECK(p.singerDisplay().adaptiveFocus);
        p.serviceSingerControl();
        time->setValue(850,juce::sendNotificationSync);
        echo->setValue(33,juce::sendNotificationSync);
        CHECK(p.singerDisplay().timeMs==850&&p.singerPending()&&p.singerDisplay().bpmQuarter==0);
        pcm.clear();p.processBlock(pcm,midi);p.serviceSingerControl();
        CHECK(p.singerDisplay().timeMs==850);
        editor.reset(); // final desired values survive closing
        editor.reset(p.createEditor()); // own pending edit cannot block reopen
        CHECK(editor->findChildWithID("vd.time")->isEnabled());
        editor.reset();
        pcm.clear();p.processBlock(pcm,midi);p.serviceSingerControl();
        CHECK(p.stateSnapshotQuiescent()&&p.acceptedSnapshot().accepted.timeMs==850);
        CHECK(std::abs(p.acceptedSnapshot().accepted.echo-.33f)<1.e-6f);
        editor.reset(p.createEditor());
        smart=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.smart"));CHECK(smart&&smart->isVisible());
        const auto musical=p.singerDisplay();smart->onClick();
        CHECK(sameParameters(p.singerDisplay(),smartVoicing(musical)));
        editor.reset();pcm.clear();p.processBlock(pcm,midi);p.serviceSingerControl();
        CHECK(p.stateSnapshotQuiescent()&&sameParameters(p.acceptedSnapshot().accepted,smartVoicing(musical)));
        p.getStateInformation(saved);CHECK(saved.getSize()==48&&StateCodec::decode(saved.getData(),saved.getSize(),restored)&&restored.continuous.adaptiveFocus);
    }
    {
        JuceVocalDelayProcessor hosted;CHECK(hosted.useHostLocalMailbox());
        hosted.prepareToPlay(48000,128);CHECK(hosted.isReady()&&hosted.isDetached());
        std::unique_ptr<juce::AudioProcessorEditor> editor(hosted.createEditor());
        auto* singer=dynamic_cast<SingerEditor*>(editor.get());CHECK(singer);
        auto* echo=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.echo"));CHECK(echo);
        bool access=true;singer->bindHostAccess([&]{return access;});
        auto* liveButton=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.stage.0"));
        auto* ballad=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.stage.2"));
        auto* stageToggle=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.stageView"));
        auto* stagePower=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.power"));
        CHECK(liveButton&&ballad&&stageToggle&&stagePower&&liveButton->isVisible()&&!echo->isVisible());
        stagePower->onClick();CHECK(!hosted.singerDisplay().enabled);
        ballad->onClick();CHECK(sameParameters(hosted.singerDisplay(),presetSettings(2))&&!hosted.singerPending());
        liveButton->onClick();CHECK(sameParameters(hosted.singerDisplay(),presetSettings(0)));
        stageToggle->onClick();CHECK(echo->isVisible()&&!liveButton->isVisible());
        echo->setValue(27,juce::sendNotificationSync);
        CHECK(!hosted.singerPending()&&std::abs(hosted.acceptedSnapshot().accepted.echo-.27f)<1.e-6f);
        auto* preset=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("vd.preset"));CHECK(preset);
        auto* save=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.save"));CHECK(save&&!save->isEnabled());
        singer->bindUserPresetService([]{return std::vector<SingerEditor::UserPresetEntry>{{"id","My Echo"}};},
            [](const juce::String&){return juce::String("id");},
            [&hosted](const juce::String& id)->std::optional<Parameters>{
                if(id!="id")return {};
                auto p=hosted.singerDisplay();p.echo=.31f;return p;
            });
        CHECK(save->isEnabled());preset->setSelectedId(1000,juce::sendNotificationSync);
        CHECK(preset->getSelectedId()==1000&&std::abs(hosted.singerDisplay().echo-.31f)<1.e-6f);
        echo->setValue(27,juce::sendNotificationSync);
        CHECK(preset->getSelectedId()!=1000);
        access=false;singer->refresh();CHECK(!echo->isEnabled()&&!save->isEnabled());
        echo->setValue(81,juce::sendNotificationSync);CHECK(std::abs(hosted.singerDisplay().echo-.27f)<1.e-6f);
        editor.reset();juce::MemoryBlock detached;hosted.getStateInformation(detached);CHECK(detached.getSize()==48);
        CHECK(hosted.submit({{1,1,1,1},1,{}})==Submit::invalid);
        hosted.sealForRealtime();CHECK(!hosted.isDetached());hosted.releaseResources();
        hosted.prepareToPlay(96000,128);CHECK(hosted.isReady()&&std::abs(hosted.singerDisplay().echo-.27f)<1.e-6f);
    }
    {
        JuceVocalDelayProcessor live;CHECK(live.useHostLocalMailbox());
        live.prepareToPlay(48000,128);live.sealForRealtime();
        std::unique_ptr<juce::AudioProcessorEditor> editor(live.createEditor());
        auto* singer=dynamic_cast<SingerEditor*>(editor.get());CHECK(singer);
        auto* hit=dynamic_cast<juce::TextButton*>(editor->findChildWithID("vd.hit"));CHECK(hit);
        auto bypass=SingerEditor::HostBypass::global;
        singer->bindHostBypass([&]{return bypass;});CHECK(!hit->isEnabled());
        bypass=SingerEditor::HostBypass::none;singer->refresh();CHECK(hit->isEnabled());
        CHECK(live.musicFollowing());CHECK(live.setMusicFollow(true));MusicObservation o{7,1,2,100,120,.8f,true};
        live.publishMusicObservation(o,9,0);o.frame=200;live.publishMusicObservation(o,9,500);
        CHECK(live.singerPending());pcm.clear();live.processBlock(pcm,midi);live.serviceSingerControl();
        CHECK(live.acceptedSnapshot().accepted.timeMs==500);
        editor.reset();o.bpm=100;o.frame=300;live.publishMusicObservation(o,9,1000);
        o.frame=400;live.publishMusicObservation(o,9,1500);
        pcm.clear();live.processBlock(pcm,midi);live.serviceSingerControl();
        CHECK(live.stateSnapshotQuiescent()&&live.acceptedSnapshot().accepted.timeMs==600);
        o.locked=false;live.publishMusicObservation(o,9,2000);
        CHECK(live.musicStatus()==MusicFollow::Status::holding&&live.acceptedSnapshot().accepted.timeMs==600);
        editor.reset(live.createEditor());auto* bpm=dynamic_cast<juce::Slider*>(editor->findChildWithID("vd.bpm"));
        CHECK(bpm);bpm->setValue(110,juce::sendNotificationSync);CHECK(!live.musicFollowing());
        editor.reset();pcm.clear();live.processBlock(pcm,midi);live.serviceSingerControl();
        CHECK(live.stateSnapshotQuiescent()&&live.acceptedSnapshot().accepted.bpmQuarter==440);
        live.releaseResources();live.prepareToPlay(96000,128);CHECK(!live.musicFollowing());
    }
    std::puts("PASS real JUCE continuous PCM, state ACK, bypass, restart, stale lease, detached native editor, retained access guard and music follow");
}
