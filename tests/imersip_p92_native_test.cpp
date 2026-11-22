// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT) || !ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT
#error ImerSip P9.2 native test requires explicit P9 experimental research
#endif
#include "../engine/builtin_fx/ImerSipP92Processor.h"
#include "../src/SonKuNativeBridgeContract.h"
#include <cassert>
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <array>

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

static void sonkuProductIsolation(int rate) {
    using armix::builtin_fx::SonKuSpatialProcessor;
    auto sonku=std::make_unique<SonKuSpatialProcessor>();
    sonku->prepareToPlay(static_cast<double>(rate),128);
    assert(sonku->isReady());
    const auto original=sonku->acceptedSettings();
    assert(armix::sonku::valid8DSonKuInsertSettings(original));
    // These controls are deliberately valid for the shared ImmerSIB
    // DSP, but must NEVER be admitted to the ordinary SonKu product.
    auto near=original;
    near.sourceDistanceMeters=1.1f;
    assert(armix::sonku::valid(near));
    assert(!sonku->queueSettings(near,5101));
    auto excessiveEarly=original;
    excessiveEarly.roomAmount=1.7f;
    assert(armix::sonku::valid(excessiveEarly));
    assert(!sonku->queueSettings(excessiveEarly,5102));
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    auto foreignVoicing=original;
    foreignVoicing.sceneVoicing=4;
    assert(armix::sonku::valid(foreignVoicing));
    assert(!sonku->queueSettings(foreignVoicing,5103));
    armix::sonku::Settings parsed{};
    const auto foreignJson=
        armix::builtin_fx::sonku_bridge::acceptedSettings(foreignVoicing);
    assert(!armix::builtin_fx::sonku_bridge::decodeSettings(
        foreignJson,parsed));
#endif
    assert(sonku->stateSnapshotQuiescent());
    assert(sonku->acceptedSettings().sourceDistanceMeters==
           original.sourceDistanceMeters);
    // Approved P9.9/Signature SonKu still queues, applies and round-trips.
    assert(sonku->queueSettings(original,5104));
    juce::AudioBuffer<float> buffer(2,128);
    juce::MidiBuffer midi;
    buffer.clear();
    sonku->processBlock(buffer,midi);
    SonKuSpatialProcessor::Completion ack{};
    assert(sonku->pollCompletion(ack) && ack.applied &&
           ack.requestId==5104);
    juce::MemoryBlock saved;
    sonku->getStateInformation(saved);
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(saved.getSize()==67);
    auto forgedVoicing=saved;
    const int imersibOnly=4;
    std::memcpy(static_cast<std::uint8_t*>(forgedVoicing.getData())+63,
                &imersibOnly,sizeof(imersibOnly));
    SonKuSpatialProcessor restored;
    restored.prepareToPlay(static_cast<double>(rate),128);
    assert(!restored.restoreUnpublishedState(
        forgedVoicing.getData(),static_cast<int>(forgedVoicing.getSize())));
    assert(restored.acceptedSettings().sceneVoicing==original.sceneVoicing);
    auto forgedEarly=saved;
    const float imersibEarly=1.7f;
    std::memcpy(static_cast<std::uint8_t*>(forgedEarly.getData())+38,
                &imersibEarly,sizeof(imersibEarly));
    assert(!restored.restoreUnpublishedState(
        forgedEarly.getData(),static_cast<int>(forgedEarly.getSize())));
    assert(restored.restoreUnpublishedState(
        saved.getData(),static_cast<int>(saved.getSize())));
    assert(restored.acceptedSettings().sceneVoicing==original.sceneVoicing);
    restored.releaseResources();
#endif
    sonku->releaseResources();
}

static void nativeParity(int rate) {
    auto fx=std::make_unique<ImerSipP92Processor>();
    assert(fx->getName()=="ImmerSIB");
    assert(juce::String(ImerSipP92Processor::catalogIdentity)==
           "armix.builtin.imersip.v1");
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
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(std::abs(actual.stereoSpreadDegrees-112.f)<.001f);
    assert(std::abs(actual.sourceDistanceMeters-2.f)<.001f);
    assert(actual.roomAmount==1.36f && actual.ambience==.84f);
    assert(actual.outputTrim==1.8f && actual.sceneVoicing==4);
#else
    assert(std::abs(actual.stereoSpreadDegrees-150.f)<.001f);
    assert(std::abs(actual.sourceDistanceMeters-2.2f)<.001f);
    assert(std::abs(actual.roomAmount-1.f)<.001f);
    assert(std::abs(actual.ambience-.91f)<.001f);
    assert(std::abs(actual.outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(4.8f))<.001f);
#endif
    assert(std::abs(neutral.stereoSpreadDegrees-82.f)<.001f);
    assert(std::abs(neutral.ambience-.33f)<.001f);
    assert(std::abs(neutral.outputTrim-1.f)<.001f);
    assert(!actual.musicCompatibleLowFrequency);

    auto native=std::make_unique<SonKuSpatialCore>();
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
    // JUCE's branded candidate constructor chooses fixed stage instead of
    // the released P99 room donor. Reference MUST choose the SAME mode.
    // Do not weaken epsilon tolerances or bypass this exact PCM test.
    native->optInImmerSIBStageMonitor();
#if defined(ARMIX_IMMERSIB_NATURAL_BALANCE_RESEARCH) && ARMIX_IMMERSIB_NATURAL_BALANCE_RESEARCH
    native->optInImmerSIBNaturalBalance();
#endif
#elif defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
    // Released alpha.26 reference, unchanged when the research flag is OFF.
    native->optInImmerSIBVocalRoom();
#endif
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
            // EXACT native callback with matching ImmerSIB product opt-in; no second DSP.
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
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(state.getSize()==75);
#else
    assert(state.getSize()==71);
#endif
    auto restored=std::make_unique<ImerSipP92Processor>();
    restored->prepareToPlay(rate,128);
    assert(restored->restoreUnpublishedState(state.getData(),
                                          static_cast<int>(state.getSize())));
    assert(restored->acceptedSettings().renderMode==9);
    assert(restored->acceptedSettings().stereoSpreadDegrees==
        reference.stereoSpreadDegrees);
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(restored->acceptedSettings().sceneVoicing==4);
    // Existing IMRS v7 shows saved at 10m are admitted with a nearfield
    // distance migration, but preserve voicing, early room, and gain.
    auto farState=state;
    const float farDistance=10.f;
    std::memcpy(static_cast<std::uint8_t*>(farState.getData())+42,
                &farDistance,sizeof(farDistance)); // SONK v12 distance
    auto migratedFar=std::make_unique<ImerSipP92Processor>();
    migratedFar->prepareToPlay(rate,128);
    assert(migratedFar->restoreUnpublishedState(
        farState.getData(),static_cast<int>(farState.getSize())));
    assert(migratedFar->acceptedSettings().sourceDistanceMeters==2.5f);
    assert(migratedFar->acceptedSettings().sceneVoicing==4);
    assert(migratedFar->acceptedSettings().roomAmount==1.36f);
    migratedFar->releaseResources();
    // Earlier Custom sessions may be 1.0m; migrate UP to 1.1m.
    auto nearState=state;
    const float near1m=1.f;
    std::memcpy(static_cast<std::uint8_t*>(nearState.getData())+42,
                &near1m,sizeof(near1m));
    auto migratedNear=std::make_unique<ImerSipP92Processor>();
    migratedNear->prepareToPlay(rate,128);
    assert(migratedNear->restoreUnpublishedState(
        nearState.getData(),static_cast<int>(nearState.getSize())));
    assert(migratedNear->acceptedSettings().sourceDistanceMeters==1.1f);
    assert(migratedNear->acceptedSettings().roomAmount==reference.roomAmount);
    assert(migratedNear->acceptedSettings().outputTrim==reference.outputTrim);
    migratedNear->releaseResources();
    // Compatibility proof: an old IMRS v6 carries SONK v11 exactly
    // (63 bytes, no sceneVoicing). Restore it in the P9.8 engine as 0.
    juce::MemoryBlock oldV6;
    {
        juce::MemoryOutputStream wire(oldV6,false);
        wire.writeInt(0x494D5253);wire.writeInt(6);
        wire.writeInt(0x534F4E4B);wire.writeInt(11);
        wire.write(static_cast<const std::uint8_t*>(state.getData())+16,55);
    }
    assert(oldV6.getSize()==71);
    auto migratedV6=std::make_unique<ImerSipP92Processor>();
    migratedV6->prepareToPlay(rate,128);
    assert(migratedV6->restoreUnpublishedState(
        oldV6.getData(),static_cast<int>(oldV6.getSize())));
    assert(migratedV6->acceptedSettings().sceneVoicing==0);
    assert(migratedV6->acceptedSettings().sourceDistanceMeters<=2.5f);
    assert(migratedV6->acceptedSettings().stereoSpreadDegrees==
           reference.stereoSpreadDegrees);
    migratedV6->releaseResources();
#endif

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
    assert(fx->acceptedSettings().stereoSpreadDegrees==
        reference.stereoSpreadDegrees);
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
    assert(editor->getWidth()==935 && editor->getHeight()==578);
    auto* visualEditor=dynamic_cast<armix::builtin_fx::ImerSipP92Editor*>(editor.get());
    assert(visualEditor && visualEditor->areVisualAssetsReady());

    // Actual compact native JUCE hit bounds: no overlapping controls,
    // labels/text remain independent from the background PNG scene.
    const char* controlIds[]={"imersip.preset","imersip.room",
      "imersip.stage","imersip.distance","imersip.early",
      "imersip.late","imersip.mix","imersip.gain",
      "imersip.align","imersip.yaw","imersip.power"};
    std::array<juce::Component*,11> controls{};
    for(std::size_t i=0;i<controls.size();++i){
        controls[i]=editor->findChildWithID(controlIds[i]);
        assert(controls[i]);
        const auto r=controls[i]->getBounds();
        assert(!r.isEmpty());
        assert(editor->getLocalBounds().contains(r));
        for(std::size_t j=0;j<i;++j)
            assert(!r.intersects(controls[j]->getBounds()));
    }
    // The native OS close control replaces the redundant in-editor CLOSE.
    assert(editor->findChildWithID("imersip.close")==nullptr);
    const auto powerBounds=controls[10]->getBounds();
    const auto gainBounds=controls[7]->getBounds();
    assert(powerBounds.getY()>=juce::roundToInt(461.f*.85f));
    assert(powerBounds.getRight()>gainBounds.getRight());
    assert(powerBounds.getX()>gainBounds.getRight());
    assert(powerBounds.getY()<gainBounds.getY());
    auto* distanceLabel=dynamic_cast<juce::Label*>(
        editor->findChildWithID("imersip.label.distance"));
    assert(distanceLabel && distanceLabel->getText()=="DISTANCE");
    auto* masterLabel=dynamic_cast<juce::Label*>(
        editor->findChildWithID("imersip.label.gain"));
    assert(masterLabel && masterLabel->getText()=="MASTER GAIN");
    assert(masterLabel->getX()<powerBounds.getX());
    // Seven non-master controls should occupy two clean vertical lanes
    // with uniform label/track spacing, not four cramped columns.
    const auto a=controls[2]->getBounds();
    const auto b=controls[3]->getBounds();
    const auto c=controls[4]->getBounds();
    assert(a.getY()==b.getY() && c.getY()>a.getY());
    assert(a.getX()<b.getX());
    assert((b.getX()-a.getX())>=200);

    // The rejected synthetic-speaker positioning engine was deleted.
    // Test the CURRENT rendering contract instead: full-range UI values
    // must keep the actual photo venue and controls inside the editor.
    // No audio edit is enqueued by these display-only extreme probes.
    auto* stageProbe=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.stage"));
    auto* distanceProbe=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.distance"));
    assert(stageProbe && distanceProbe);
    const auto priorStage=stageProbe->getValue();
    const auto priorDistance=distanceProbe->getValue();
    for(const double angle:{stageProbe->getMinimum(),stageProbe->getMaximum()}){
        for(const double metres:{distanceProbe->getMinimum(),
                                 distanceProbe->getMaximum()}){
            stageProbe->setValue(angle,juce::dontSendNotification);
            distanceProbe->setValue(metres,juce::dontSendNotification);
            const auto extreme=editor->createComponentSnapshot(
                editor->getLocalBounds());
            assert(extreme.isValid() && extreme.getWidth()==935 &&
                   extreme.getHeight()==578);
            for(auto* child:controls)
                assert(editor->getLocalBounds().contains(child->getBounds()));
        }
    }
    stageProbe->setValue(priorStage,juce::dontSendNotification);
    distanceProbe->setValue(priorDistance,juce::dontSendNotification);
    auto visual=editor->createComponentSnapshot(editor->getLocalBounds());
    assert(visual.isValid() && visual.getWidth()==935 &&
           visual.getHeight()==578);
    assert(visual.getPixelAt(480,120)!=visual.getPixelAt(480,484));
    // Genuine native JUCE component snapshots, not an HTML or design mock.
    // Render all requested Windows framebuffer density ratios while retaining
    // the original 1100x680 logical hit-testing and parameter geometry.
    for(const auto dpi : {100,125,150,200}) {
        const float scale=static_cast<float>(dpi)/100.f;
        // JUCE 8.0.15 provides a first-class scale-aware snapshot API.
        // paintEntireComponent() into a manually transformed offscreen
        // Graphics can produce blank/incorrect images on Windows headless
        // runners even when createComponentSnapshot() passes above.
        // Keep the strict non-blank/opaque assertions below.
        auto frame=editor->createComponentSnapshot(
            editor->getLocalBounds(),true,scale);
        assert(frame.isValid() &&
               frame.getWidth()==juce::roundToInt(935.f*scale) &&
               frame.getHeight()==juce::roundToInt(578.f*scale));
        // A single pixel comparison can falsely fail when two dark metal
        // points happen to have the same RGBA. Validate a distributed sample
        // of the REAL native pixel buffer instead, and still reject blank art.
        const auto first=frame.getPixelAt(frame.getWidth()/10,
                                           frame.getHeight()/10);
        int distinct=0,opaque=0;
        for(int yy=1;yy<10;++yy)
            for(int xx=1;xx<10;++xx){
                const auto sample=frame.getPixelAt(
                    xx*frame.getWidth()/10, yy*frame.getHeight()/10);
                if(sample!=first)++distinct;
                if(sample.getAlpha()>0)++opaque;
            }
        assert(distinct>=8 && opaque>=40);
        const auto file=juce::File::getCurrentWorkingDirectory()
            .getChildFile("ImerSip_P92_JUCE_"+juce::String(dpi)+".png");
        juce::FileOutputStream output(file);
        assert(output.openedOk());
        juce::PNGImageFormat encoder;
        assert(encoder.writeImageToStream(frame,output));
        output.flush();
        assert(file.existsAsFile() && file.getSize()>10000);
    }
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
    assert(power->getButtonText().isEmpty());
    assert(!power->getToggleState());
    auto* selector=dynamic_cast<juce::ComboBox*>(
        editor->findChildWithID("imersip.preset"));
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(selector&&selector->getNumItems()==8); // Custom + seven scenes
#else
    assert(selector&&selector->getNumItems()==7); // Historical P9.2
#endif
    assert(selector->getSelectedId()==2);
    for(int i=0;i<selector->getNumItems();++i){
        const auto name=selector->getItemText(i);
        assert(!name.containsIgnoreCase("neutral"));
        assert(!name.containsIgnoreCase("a/b"));
    }
    assert(armix::builtin_fx::imersipP92PresetById(6)!=nullptr);
    auto* distanceSlider=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.distance"));
    auto* earlySlider=dynamic_cast<juce::Slider*>(
        editor->findChildWithID("imersip.early"));
    assert(distanceSlider && earlySlider);
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(std::abs(distanceSlider->getMinimum()-1.1)<1.e-4);
    assert(distanceSlider->getMaximum()==2.5);
    assert(earlySlider->getMaximum()==180.0);
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(armix::builtin_fx::imersipP92PresetById(8)!=nullptr);
#else
    assert(armix::builtin_fx::imersipP92PresetById(8)==nullptr);
#endif
    // Front Row Power must be a real applied audio edit, not UI illusion.
    selector->setSelectedId(5,juce::sendNotificationSync);
    juce::MidiBuffer editMidi;
    juce::AudioBuffer<float> editPcm(2,128);
    editPcm.clear();
    editorFx->processBlock(editPcm,editMidi);
    armix::builtin_fx::SonKuSpatialProcessor::Completion selected{};
    assert(editorFx->pollCompletion(selected)&&selected.applied);
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    assert(std::abs(editorFx->acceptedSettings().stereoSpreadDegrees-104.f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().sourceDistanceMeters-1.3f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().ambience-.70f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().roomAmount-1.26f)<.001f);
    assert(editorFx->acceptedSettings().sceneVoicing==4);
    juce::MemoryBlock detailState;
    editorFx->getStateInformation(detailState);
    assert(detailState.getSize()==75);
    auto detailRestored=std::make_unique<ImerSipP92Processor>();
    detailRestored->prepareToPlay(48000.,128);
    assert(detailRestored->restoreUnpublishedState(
        detailState.getData(),static_cast<int>(detailState.getSize())));
    assert(detailRestored->acceptedSettings().sceneVoicing==4);
    assert(detailRestored->acceptedSettings().sourceDistanceMeters<=2.5f);
    assert(detailRestored->acceptedSettings().roomAmount>1.f);
    detailRestored->releaseResources();
    assert(std::abs(editorFx->acceptedSettings().outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(5.60f))<.001f);
#else
    assert(std::abs(editorFx->acceptedSettings().stereoSpreadDegrees-150.f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().sourceDistanceMeters-1.6f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().ambience-.97f)<.001f);
    assert(std::abs(editorFx->acceptedSettings().outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(5.2f))<.001f);
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    // The first editor's ACK has been consumed explicitly by the test,
    // while its Timer normally retires pending_ on the message thread.
    // Use a fresh real editor/processor for an independent owner-preset
    // transaction instead of inventing a second ACK for a deferred edit.
    auto ownerFx=std::make_unique<ImerSipP92Processor>();
    ownerFx->prepareToPlay(48000.,128);
    assert(ownerFx->isReady());
    std::unique_ptr<juce::AudioProcessorEditor> ownerEditor(ownerFx->createEditor());
    assert(ownerEditor);
    auto* ownerSelector=dynamic_cast<juce::ComboBox*>(
        ownerEditor->findChildWithID("imersip.preset"));
    assert(ownerSelector && ownerSelector->getSelectedId()==2);
    ownerSelector->setSelectedId(8,juce::sendNotificationSync);
    juce::AudioBuffer<float> ownerPcm(2,128);
    juce::MidiBuffer ownerMidi;
    ownerPcm.clear();
    ownerFx->processBlock(ownerPcm,ownerMidi);
    armix::builtin_fx::SonKuSpatialProcessor::Completion userScene{};
    assert(ownerFx->pollCompletion(userScene) && userScene.applied &&
           userScene.requestId!=0);
    const auto screenshot=ownerFx->acceptedSettings();
    assert(userScene.settings.stereoSpreadDegrees==134.f);
    assert(screenshot.spacePreset==3);
    assert(screenshot.stereoSpreadDegrees==134.f);
    assert(screenshot.sourceDistanceMeters==1.5f);
    assert(screenshot.roomAmount==1.70f);
    assert(screenshot.ambience==.94f && screenshot.spatialMix==1.f);
    assert(screenshot.hrtfElevationTrimDegrees==0.f && screenshot.headYawDegrees==0.f);
    assert(screenshot.sceneVoicing==4);
    assert(std::abs(screenshot.outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(4.4f))<.001f);
    juce::MemoryBlock ownerState;
    ownerFx->getStateInformation(ownerState);
    auto ownerRestored=std::make_unique<ImerSipP92Processor>();
    ownerRestored->prepareToPlay(48000.,128);
    assert(ownerRestored->restoreUnpublishedState(
        ownerState.getData(),static_cast<int>(ownerState.getSize())));
    assert(ownerRestored->acceptedSettings().stereoSpreadDegrees==134.f);
    assert(ownerRestored->acceptedSettings().roomAmount==1.70f);
    ownerRestored->releaseResources();
    ownerEditor.reset(); // Destroy editor before its processor.
    ownerFx->releaseResources();

    // Regression: the user's exact screenshot showed Live Concert controls
    // 112deg / 2m / ER136 / Late84 / +5.1dB, but the preset showed Custom.
    // Factory outputTrim=1.8f is NOT equal to the rounded slider's 5.1dB
    // conversion. An untouched preset must submit exact factory Settings,
    // retain its NAME after the real AUDIO ACK, and serialize unchanged.
    auto sceneFx=std::make_unique<ImerSipP92Processor>();
    sceneFx->prepareToPlay(48000.,128);
    assert(sceneFx->isReady());
    std::unique_ptr<juce::AudioProcessorEditor> sceneEditor(sceneFx->createEditor());
    assert(sceneEditor);
    auto* sceneSelector=dynamic_cast<juce::ComboBox*>(
        sceneEditor->findChildWithID("imersip.preset"));
    auto* sceneStage=dynamic_cast<juce::Slider*>(
        sceneEditor->findChildWithID("imersip.stage"));
    assert(sceneSelector && sceneStage && sceneSelector->getSelectedId()==2);
    juce::AudioBuffer<float> scenePcm(2,128);
    juce::MidiBuffer sceneMidi;
    const auto settleGui=[&]{
        // Native JUCE 25Hz editor timer must consume ACK and reconcile
        // the selected label, not leave this assertion at pre-ACK state.
        // The pinned headless JUCE build excludes the optional
        // MessageManager modal loop API. Wait for the 25Hz timer to
        // become due and pump the real JUCE timer callback synchronously.
        juce::Thread::sleep(65);
        juce::Timer::callPendingTimersSynchronously();
    };
    for(const auto& p:armix::builtin_fx::imersipP92Presets){
        sceneSelector->setSelectedId(p.id,juce::sendNotificationSync);
        assert(sceneSelector->getSelectedId()==p.id);
        scenePcm.clear();
        sceneFx->processBlock(scenePcm,sceneMidi);
        settleGui();
        assert(sceneSelector->getSelectedId()==p.id);
        const auto expected=imersipP92Factory(p.id);
        const auto accepted=sceneFx->acceptedSettings();
        assert(std::abs(accepted.outputTrim-expected.outputTrim)<1.e-7f);
        assert(std::abs(accepted.sourceDistanceMeters-
                        expected.sourceDistanceMeters)<1.e-6f);
        assert(std::abs(accepted.roomAmount-expected.roomAmount)<1.e-6f);
        assert(std::abs(accepted.ambience-expected.ambience)<1.e-6f);
        assert(accepted.sceneVoicing==expected.sceneVoicing);
    }
    sceneSelector->setSelectedId(2,juce::sendNotificationSync);
    scenePcm.clear();
    sceneFx->processBlock(scenePcm,sceneMidi);
    settleGui();
    assert(sceneSelector->getSelectedId()==2);
    assert(sceneFx->acceptedSettings().outputTrim==1.8f);
    const auto originalStage=sceneStage->getValue();
    sceneStage->setValue(originalStage+1.,juce::sendNotificationSync);
    assert(sceneSelector->getSelectedId()==1); // true parameter edit only
    scenePcm.clear();
    sceneFx->processBlock(scenePcm,sceneMidi);
    settleGui();
    assert(sceneSelector->getSelectedId()==1); // remains Custom after ACK
    // Changing STAGE must not round and silently shift MASTER GAIN.
    assert(sceneFx->acceptedSettings().outputTrim==1.8f);
    assert(sceneFx->acceptedSettings().stereoSpreadDegrees==113.f);
    sceneEditor.reset();
    sceneFx->releaseResources();
#endif
    auto* actualEditor=dynamic_cast<armix::builtin_fx::ImerSipP92Editor*>(
        editor.get());
    assert(actualEditor);
    bool bypassed=false,rackBypassed=false,globalBypassed=false;
    bool hostAvailable=true,acceptPower=true;
    int acceptedLocalPower=0;
    const auto hostState=[&] {
        armix::builtin_fx::ImerSipP92Editor::HostBypassState state{};
        state.valid=hostAvailable;
        state.slotBypassed=bypassed;
        state.rackBypassed=rackBypassed;
        state.globalBypassed=globalBypassed;
        return state;
    };
    actualEditor->bindHostPower(
        [&](bool desired) {
            if(!acceptPower)return false;
            bypassed=desired;
            ++acceptedLocalPower;
            return true;
        },hostState);
    assert(power->isEnabled() && power->getToggleState());
    // Host-connected native screenshots prove the two actual power glyph
    // states. The earlier DPI snapshots deliberately test disabled/fail-safe
    // before host binding; these frames additionally verify the real bypass
    // response without faking a DSP power state in the editor.
    const auto savePowerFrame=[&](const char* mode) {
        auto frame=editor->createComponentSnapshot(editor->getLocalBounds());
        assert(frame.isValid() && frame.getWidth()==935 &&
               frame.getHeight()==578);
        const auto file=juce::File::getCurrentWorkingDirectory()
            .getChildFile("ImerSip_P92_JUCE_POWER_"+juce::String(mode)+".png");
        juce::FileOutputStream output(file);
        assert(output.openedOk());
        juce::PNGImageFormat encoder;
        assert(encoder.writeImageToStream(frame,output));
        output.flush();
        assert(file.existsAsFile() && file.getSize()>10000);
        return frame;
    };
    const auto activeFrame=savePowerFrame("ON");
    power->onClick();
    assert(bypassed && !power->getToggleState());
    const auto bypassFrame=savePowerFrame("BYPASSED");

    // Validate the *real JUCE framebuffer*, not just an ON/OFF flag.
    // The stage must darken independently of the amber label, the label
    // must be legible near scene centre, and the lower controls must dim
    // while the power icon itself remains interactive and visible.
    const auto rgbEnergy=[](juce::Colour px){
        return 3*int(px.getRed())+6*int(px.getGreen())+int(px.getBlue());
    };
    int stageDarker=0;
    for(const int y:{75,110,150,285,320,350})
        for(const int x:{80,145,210,720,790,845})
            if(rgbEnergy(activeFrame.getPixelAt(x,y))-
               rgbEnergy(bypassFrame.getPixelAt(x,y))>=15)
                ++stageDarker;
    assert(stageDarker>=24); // No washed-out "barely bypassed" venue
    // Check relative scene luminance, not just binary "some pixel changed".
    // Keep a trace of the venue rather than turning the stage completely black.
    std::int64_t sceneOnEnergy=0,sceneMutedEnergy=0;
    for(const int y:{75,110,150,285,320,350})
        for(const int x:{80,145,210,720,790,845}) {
            sceneOnEnergy+=rgbEnergy(activeFrame.getPixelAt(x,y));
            sceneMutedEnergy+=rgbEnergy(bypassFrame.getPixelAt(x,y));
        }
    assert(sceneMutedEnergy*100<sceneOnEnergy*52);
    assert(sceneMutedEnergy*100>sceneOnEnergy*18);

    // Enforce actual glyph prominence: broad amber letter coverage across
    // at least 36 vertical framebuffer rows; a small thin 40px regular
    // caption would not pass this native pixel-level requirement.
    int amberLetters=0,amberRows=0;
    for(int y=115;y<285;++y) {
        int inThisRow=0;
        for(int x=200;x<740;x+=2) {
            const auto px=bypassFrame.getPixelAt(x,y);
            if(px.getRed()>170 && px.getGreen()>85 &&
               int(px.getRed())>int(px.getGreen())+55 &&
               int(px.getGreen())>int(px.getBlue())+35) {
                ++amberLetters;
                ++inThisRow;
            }
        }
        if(inThisRow>=8) ++amberRows;
    }
    assert(amberLetters>=380 && amberRows>=36);

    std::int64_t panelOnEnergy=0,panelMutedEnergy=0;
    for(const int y:{410,435,475,520,552})
        for(const int x:{45,120,225,305,415,550,675,775}) {
            panelOnEnergy+=rgbEnergy(activeFrame.getPixelAt(x,y));
            panelMutedEnergy+=rgbEnergy(bypassFrame.getPixelAt(x,y));
        }
    // A true UI mute is visually distinct, but labels remain decipherable.
    assert(panelMutedEnergy*100<panelOnEnergy*73);
    assert(panelMutedEnergy*100>panelOnEnergy*35);

    // Power is intentionally excluded from the panel veil. It must remain
    // conspicuous and actionable when bypassed, not darkened with the rack.
    int powerHighlightPixels=0;
    const auto activePowerBounds=power->getBounds();
    for(int y=activePowerBounds.getY()+3;y<activePowerBounds.getBottom()-3;++y)
        for(int x=activePowerBounds.getX()+3;x<activePowerBounds.getRight()-3;++x) {
            const auto px=bypassFrame.getPixelAt(x,y);
            if(px.getRed()>112 && px.getGreen()>120 && px.getBlue()>125)
                ++powerHighlightPixels;
        }
    assert(powerHighlightPixels>=15);

    int changedPowerPixels=0;
    const auto powerRegion=power->getBounds();
    for(int y=powerRegion.getY()+3;y<powerRegion.getBottom()-3;y+=3)
        for(int x=powerRegion.getX()+3;x<powerRegion.getRight()-3;x+=3)
            if(activeFrame.getPixelAt(x,y)!=bypassFrame.getPixelAt(x,y))
                ++changedPowerPixels;
    assert(changedPowerPixels>=10); // Visible true host ON/OFF indication
    power->onClick();
    assert(!bypassed && power->getToggleState());
    assert(acceptedLocalPower==2);

    // Rejected host commands do not optimistic-toggle the actual PCM state.
    acceptPower=false;
    power->onClick();
    assert(!bypassed && power->getToggleState());
    assert(acceptedLocalPower==2);
    acceptPower=true;

    // Mixer-header FX Power is a GLOBAL override, not a rewrite of
    // any slot's remembered bypass preference. With editor still open
    // it must immediately show BYPASSED and block misleading local power.
    globalBypassed=true;
    actualEditor->synchronizeHostPower();
    assert(!bypassed && !power->isEnabled() && !power->getToggleState());
    const auto globalFrame=savePowerFrame("GLOBAL_BYPASSED");
    assert(globalFrame.getPixelAt(450,195)!=activeFrame.getPixelAt(450,195));
    globalBypassed=false;
    actualEditor->synchronizeHostPower();
    assert(!bypassed && power->isEnabled() && power->getToggleState());

    // Rack Power is another independent additive gate; it must dim even
    // though this individual slot's preference is still enabled.
    rackBypassed=true;
    actualEditor->synchronizeHostPower();
    assert(!bypassed && !power->isEnabled() && !power->getToggleState());
    const auto rackFrame=savePowerFrame("RACK_BYPASSED");
    assert(rackFrame.getPixelAt(450,195)!=activeFrame.getPixelAt(450,195));
    rackBypassed=false;
    actualEditor->synchronizeHostPower();
    assert(!bypassed && power->isEnabled() && power->getToggleState());

    // An invalidated processor lease can neither change another slot nor
    // pretend that its UI owns an admitted host effect.
    hostAvailable=false;
    actualEditor->synchronizeHostPower();
    assert(!power->isEnabled() && !power->getToggleState());
    assert(power->getTooltip().contains("unavailable"));
    const auto unavailableFrame=savePowerFrame("LEASE_UNAVAILABLE");
    // A lost native processor lease must never look like an active
    // concert scene or an intentionally bypassed FX. It must remain
    // visually distinct from both, with no stale clickable Power.
    assert(unavailableFrame.getPixelAt(85,92)!=activeFrame.getPixelAt(85,92));
    assert(unavailableFrame.getPixelAt(450,195)!=bypassFrame.getPixelAt(450,195));
    const auto offlineCaption=unavailableFrame.getPixelAt(460,253);
    (void)offlineCaption;
    hostAvailable=true;
    actualEditor->synchronizeHostPower();
    assert(power->isEnabled() && power->getToggleState());

    // Original per-insert bypass must survive a global A/B roundtrip.
    power->onClick();
    assert(bypassed);
    globalBypassed=true;
    actualEditor->synchronizeHostPower();
    globalBypassed=false;
    actualEditor->synchronizeHostPower();
    assert(bypassed && !power->getToggleState() && power->isEnabled());
    power->onClick();
    assert(!bypassed && power->getToggleState());

    // Actual JUCE scene renders: exact same embedded venue images as shipped
    // with the native app. These captures are NOT HTML concept mockups.
    // Each of the six preset IDs must produce a distinct visual composition.
    std::uint64_t previousSceneSignature=0;
    for(int sceneId=2;sceneId<=7;++sceneId) {
        selector->setSelectedId(sceneId,juce::sendNotificationSync);
        auto photo=editor->createComponentSnapshot(editor->getLocalBounds());
        assert(photo.isValid() && photo.getWidth()==935 && photo.getHeight()==578);
        std::uint64_t signature=14695981039346656037ull;
        for(int y=100;y<385;y+=37)
            for(int x=65;x<860;x+=53) {
                const auto px=photo.getPixelAt(x,y).getARGB();
                signature=(signature ^ static_cast<std::uint64_t>(px))
                          *1099511628211ull;
            }
        assert(sceneId==2 || signature!=previousSceneSignature);
        previousSceneSignature=signature;
        // The exact same six embedded PNGs must reach BOTH left/right
        // viewport edges. Prior centred 2:1 images left uniform matte bands.
        for(const int edgeX:{8,photo.getWidth()-9}) {
            const auto first=photo.getPixelAt(edgeX,45);
            int matteCount=0;
            int varied=0;
            for(int y=52;y<385;y+=19) {
                const auto pixel=photo.getPixelAt(edgeX,y);
                if(pixel==juce::Colour(0xff0e161c))++matteCount;
                if(pixel!=first)++varied;
            }
            assert(matteCount<3 && varied>=7);
        }
        const auto sceneFile=juce::File::getCurrentWorkingDirectory()
            .getChildFile("ImerSip_P92_SCENE_"+juce::String(sceneId)+".png");
        juce::FileOutputStream stream(sceneFile);
        assert(stream.openedOk());
        juce::PNGImageFormat format;
        assert(format.writeImageToStream(photo,stream));
        stream.flush();
        assert(sceneFile.existsAsFile() && sceneFile.getSize()>10000);
    }
    editor.reset();
    editorFx->releaseResources();
    sonkuProductIsolation(44100);
    sonkuProductIsolation(48000);
    nativeParity(44100);
    nativeParity(48000);
    std::puts("PASS IMERSIP P92 JUCE: exact P9 PCM, 44.1/48k, native state, legacy IMRS, editor, audio ACK");
}
