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
    CHECK(p.isReady()&&p.getLatencySamples()==0);p.sealForRealtime();
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
        preset->setSelectedId(4,juce::sendNotificationSync);
        p.serviceSingerControl();
        time->setValue(850,juce::sendNotificationSync);
        echo->setValue(33,juce::sendNotificationSync);
        CHECK(p.singerDisplay().timeMs==850&&p.singerPending());
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
    }
    std::puts("PASS real JUCE continuous PCM, state ACK, bypass, restart, stale lease and native singer editor");
}
