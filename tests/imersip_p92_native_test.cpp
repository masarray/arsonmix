// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT) || !ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT
#error ImerSip P9.2 native test requires explicit P9 experimental research
#endif
#include "../engine/builtin_fx/ImerSipP92Processor.h"
#include <cassert>
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

using armix::builtin_fx::ImerSipP92Processor;
using armix::builtin_fx::imersipP92ReferenceScene;
using armix::builtin_fx::imersipP92Factory;
using armix::sonku::SonKuSpatialCore;
constexpr double pi=3.14159265358979323846;

static juce::MemoryBlock oldImrsV5() {
    juce::MemoryBlock bytes;
    {
        juce::MemoryOutputStream out(bytes,false);
        out.writeInt(0x494D5253);out.writeInt(5);
        out.writeInt(0);   // original concert
        out.writeFloat(.88f);out.writeFloat(.78f);
        out.writeFloat(.67f);out.writeFloat(.70f);
        out.writeFloat(1.f);out.writeFloat(.60f);
        out.writeFloat(1.f);out.writeFloat(.25f);
        out.writeFloat(.78f);out.writeFloat(0.f);
    }
    assert(bytes.getSize()==52);
    return bytes;
}

static void nativeParity(int rate) {
    auto fx=std::make_unique<ImerSipP92Processor>();
    fx->prepareToPlay(static_cast<double>(rate),128);
    assert(fx->isReady());
    const auto reference=imersipP92Factory(2);
    // Reference has NO selectable preset ID. ID 6 is now Symphonic Palace.
    const auto neutral=imersipP92ReferenceScene();
    assert(armix::builtin_fx::validImerSipP92Scene(reference));
    assert(armix::builtin_fx::validImerSipP92Scene(neutral));
    const auto actual=fx->acceptedSettings();
    assert(actual.renderMode==9&&!actual.moving);
    assert(actual.spacePreset==3);
    assert(std::abs(actual.stereoSpreadDegrees-150.f)<.001f);
    assert(std::abs(actual.sourceDistanceMeters-2.2f)<.001f);
    assert(std::abs(actual.roomAmount-1.f)<.001f);
    assert(std::abs(actual.ambience-.91f)<.001f);
    assert(std::abs(actual.outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(4.8f))<.001f);
    assert(std::abs(neutral.stereoSpreadDegrees-82.f)<.001f);
    assert(std::abs(neutral.ambience-.33f)<.001f);
    assert(std::abs(neutral.outputTrim-1.f)<.001f);
    assert(!actual.musicCompatibleLowFrequency);

    auto native=std::make_unique<SonKuSpatialCore>();
    assert(native->prepare(rate,
        armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(native->setSettings(reference));

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> pcm(2,128);
    float left[128]{},right[128]{};
    double dryEnergy=0.,changeEnergy=0.,stereoEnergy=0.;
    for(int block=0;block<256;++block){
        for(int i=0;i<128;++i) {
            const double t=(block*128+i)/static_cast<double>(rate);
            const float mid=static_cast<float>(
                .15*std::sin(2*pi*311*t)+
                .060*std::sin(2*pi*1130*t)+
                .045*std::sin(2*pi*8250*t));
            const float side=static_cast<float>(
                .075*std::sin(2*pi*633*t)+.04*std::sin(2*pi*3200*t));
            left[i]=mid+side;right[i]=mid-side;
            pcm.setSample(0,i,left[i]);pcm.setSample(1,i,right[i]);
        }
        native->process(left,right,128);
        fx->processBlock(pcm,midi);
        for(int i=0;i<128;++i){
            const float l=pcm.getSample(0,i),r=pcm.getSample(1,i);
            assert(std::isfinite(l)&&std::isfinite(r));
            // EXACT same P9.2 native callback, no second ImerSip DSP.
            assert(std::abs(l-left[i])<2.e-6f);
            assert(std::abs(r-right[i])<2.e-6f);
            if(block<32)continue; // settle measured KEMAR/room histories
            const double t=(block*128+i)/static_cast<double>(rate);
            const double m=.15*std::sin(2*pi*311*t)+
                .060*std::sin(2*pi*1130*t)+
                .045*std::sin(2*pi*8250*t);
            const double s=.075*std::sin(2*pi*633*t)+
                .04*std::sin(2*pi*3200*t);
            const double dryL=m+s,dryR=m-s;
            dryEnergy+=dryL*dryL+dryR*dryR;
            changeEnergy+=(l-dryL)*(l-dryL)+(r-dryR)*(r-dryR);
            stereoEnergy+=(l-r)*(l-r);
        }
    }
    const double delta=std::sqrt(changeEnergy/(dryEnergy+1.e-15));
    std::fprintf(stderr,
        "IMERSIP_P92_NATIVE_PARITY rate=%d delta=%.6f stereo=%.4f\n",
        rate,delta,stereoEnergy);
    assert(delta>.05 && stereoEnergy>.01);

    // v6 IMRS wraps native SONK v11, not an invented shadow DSP schema.
    juce::MemoryBlock state;
    fx->getStateInformation(state);
    assert(state.getSize()==71);
    auto restored=std::make_unique<ImerSipP92Processor>();
    restored->prepareToPlay(rate,128);
    assert(restored->restoreUnpublishedState(state.getData(),
                                          static_cast<int>(state.getSize())));
    assert(restored->acceptedSettings().renderMode==9);
    assert(restored->acceptedSettings().stereoSpreadDegrees==150.f);

    // The embedded v11 SONK mode field is at byte 50 of its 63-byte
    // record (after the 8-byte IMRS header). Corrupting P9 -> P8.2 mode
    // must fail BEFORE modifying a prepared FX's accepted PCM scene.
    auto wrongMode=state;
    auto* wrongBytes=static_cast<std::uint8_t*>(wrongMode.getData());
    assert(wrongBytes[8+50]==9);
    wrongBytes[8+50]=0;
    assert(!restored->restoreUnpublishedState(
        wrongMode.getData(),static_cast<int>(wrongMode.getSize())));
    assert(restored->acceptedSettings().renderMode==9);
    auto movingWorld=state;
    auto* movingBytes=static_cast<std::uint8_t*>(movingWorld.getData());
    movingBytes[8+28]=1; // dynamic orbit not allowed by ImerSip's scene
    assert(!restored->restoreUnpublishedState(
        movingWorld.getData(),static_cast<int>(movingWorld.getSize())));
    assert(restored->acceptedSettings().renderMode==9);

    // Original IMRS 52-byte v5 restores without silently launching P8 DSP.
    auto old=oldImrsV5();
    auto legacy=std::make_unique<ImerSipP92Processor>();
    legacy->prepareToPlay(rate,128);
    assert(legacy->restoreUnpublishedState(old.getData(),
                                         static_cast<int>(old.getSize())));
    const auto migrated=legacy->acceptedSettings();
    assert(migrated.renderMode==9 && !migrated.moving);
    assert(std::abs(migrated.stereoSpreadDegrees-130.5f)<.01f);
    assert(migrated.spacePreset==3);
    // Exercise the *virtual JUCE* restoration path as well as registry
    // restore. A base-class SONK parser must not swallow ImerSip IMRS.
    auto viaJuce=std::make_unique<ImerSipP92Processor>();
    viaJuce->prepareToPlay(rate,128);
    juce::AudioProcessor& conventionalHost=*viaJuce;
    conventionalHost.setStateInformation(
        old.getData(),static_cast<int>(old.getSize()));
    assert(std::abs(viaJuce->acceptedSettings().stereoSpreadDegrees-
                    130.5f)<.01f);
    auto corrupt=old;
    corrupt.getData(); // corrupted byte count must reject before mutation
    auto invalid=std::make_unique<ImerSipP92Processor>();
    invalid->prepareToPlay(rate,128);
    assert(!invalid->restoreUnpublishedState(corrupt.getData(),51));

    // Edits travel through the SAME native callback and audio ACK.
    auto changed=fx->acceptedSettings();
    changed.stereoSpreadDegrees=100.f;
    assert(fx->queueSettings(changed,72));
    assert(fx->acceptedSettings().stereoSpreadDegrees==150.f);
    pcm.clear();
    fx->processBlock(pcm,midi);
    armix::builtin_fx::SonKuSpatialProcessor::Completion ack{};
    assert(fx->pollCompletion(ack));
    assert(ack.requestId==72 && ack.applied);
    assert(std::abs(fx->acceptedSettings().stereoSpreadDegrees-100.f)<.01f);
    assert(fx->stateSnapshotQuiescent());

    // Even when the caller holds the SonKu BASE pointer, its public
    // settings queue must not admit an old MusicBed or orbit mode into
    // an ImerSip world-speaker insert.
    armix::builtin_fx::SonKuSpatialProcessor& baseControl=*fx;
    auto wrongP8=fx->acceptedSettings();
    wrongP8.renderMode=0;
    assert(!baseControl.queueSettings(wrongP8,73));
    auto wrongOrbit=fx->acceptedSettings();
    wrongOrbit.moving=true;
    assert(!baseControl.queueSettings(wrongOrbit,74));
    assert(fx->stateSnapshotQuiescent());

    fx->releaseResources();
    restored->releaseResources();
    legacy->releaseResources();
    viaJuce->releaseResources();
    invalid->releaseResources();
}
int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    auto editorFx=std::make_unique<ImerSipP92Processor>();
    editorFx->prepareToPlay(48000.,128);
    std::unique_ptr<juce::AudioProcessorEditor> editor(editorFx->createEditor());
    assert(editor && editor->findChildWithID("imersip.preset"));
    assert(editor->getWidth()==1100 && editor->getHeight()==680);
    auto* visualEditor=dynamic_cast<armix::builtin_fx::ImerSipP92Editor*>(editor.get());
    assert(visualEditor && visualEditor->areVisualAssetsReady());
    for(const float stageAngle:{30.f,50.f,150.f,180.f}){
        for(const float metres:{1.6f,2.2f,5.f,7.4f}){
            const auto b=armix::builtin_fx::imersipP92ConcertBoxes(stageAngle,metres);
            assert(b.safe());
            assert(b.leftArray.getRight()<b.rightArray.getX());
            assert(b.leftSub.getRight()<b.rightSub.getX());
        }
    }
    assert(armix::builtin_fx::imersipP92ConcertBoxes(
        std::numeric_limits<float>::quiet_NaN(),-999.f).safe());
    auto visual=editor->createComponentSnapshot(editor->getLocalBounds());
    assert(visual.isValid() && visual.getWidth()==1100 &&
           visual.getHeight()==680);
    assert(visual.getPixelAt(480,120)!=visual.getPixelAt(480,484));
    auto* stage=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.stage"));
    auto* master=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.gain"));
    assert(stage && stage->getSliderStyle()==juce::Slider::LinearHorizontal);
    assert(master && master->getSliderStyle()==
           juce::Slider::RotaryHorizontalVerticalDrag);
    auto* power=dynamic_cast<juce::TextButton*>(
        editor->findChildWithID("imersip.power"));
    assert(power && !power->isEnabled());
    auto* selector=dynamic_cast<juce::ComboBox*>(
        editor->findChildWithID("imersip.preset"));
    assert(selector&&selector->getNumItems()==7); // Custom + six strong scenes
    assert(selector->getSelectedId()==2);
    for(int i=0;i<selector->getNumItems();++i){
        const auto name=selector->getItemText(i);
        assert(!name.containsIgnoreCase("neutral"));
        assert(!name.containsIgnoreCase("a/b"));
    }
    assert(armix::builtin_fx::imersipP92PresetById(6)!=nullptr);
    assert(armix::builtin_fx::imersipP92PresetById(8)==nullptr);
    // Front Row Power must be a real applied audio edit, not UI illusion.
    selector->setSelectedId(5,juce::sendNotificationSync);
    juce::MidiBuffer editMidi;
    juce::AudioBuffer<float> editPcm(2,128);
    editPcm.clear();
    editorFx->processBlock(editPcm,editMidi);
    armix::builtin_fx::SonKuSpatialProcessor::Completion selected{};
    assert(editorFx->pollCompletion(selected)&&selected.applied);
    assert(std::abs(editorFx->acceptedSettings().stereoSpreadDegrees-150.f)
           <.001f);
    assert(std::abs(editorFx->acceptedSettings().sourceDistanceMeters-1.6f)
           <.001f);
    assert(std::abs(editorFx->acceptedSettings().ambience-.97f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(5.2f))<.001f);
    auto* actualEditor=dynamic_cast<armix::builtin_fx::ImerSipP92Editor*>(
        editor.get());
    assert(actualEditor);
    bool bypassed=false;
    actualEditor->bindHostPower(
        [&](bool desired){bypassed=desired;return true;},
        [&](){return bypassed;});
    assert(power->isEnabled());
    power->onClick();assert(bypassed);
    power->onClick();assert(!bypassed);
    editor.reset();
    editorFx->releaseResources();
    nativeParity(44100);
    nativeParity(48000);
    std::puts("PASS IMERSIP P92 JUCE: exact P9 PCM, 44.1/48k, native state, legacy IMRS, editor, audio ACK");
}
