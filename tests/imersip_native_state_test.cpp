// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipProcessor.h"
#include "../src/NativeBuiltInFx.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <memory>

#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error ImerSip native JUCE tests must use actual P8.2 six-wall room
#endif
using armix::builtin_fx::ImerSipProcessor;
using armix::imersip::Settings;

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    // CRITICAL: the P8.2 PCM research flag must register ImerSip in the
    // exact native catalog emitted by ConsoleWindow::emitCatalog().
    // Without this gate Windows tests once passed but the FX Picker omitted
    // the effect because CMake did not define the *old* macro.
    const auto* descriptor=armix::builtin_fx::findNativeBuiltInFx(
        ImerSipProcessor::catalogIdentity);
    assert(descriptor!=nullptr);
    assert(descriptor->showInPicker);
    assert(std::strcmp(descriptor->name,"ImerSip")==0);
    assert(std::strcmp(descriptor->category,"Spatial")==0);
    assert(descriptor->minimumLiveRate==44100.);
    assert(descriptor->maximumLiveRate==48000.);
    auto catalogInstance=descriptor->prepare(48000.,128,nullptr,0);
    assert(catalogInstance);
    assert(dynamic_cast<ImerSipProcessor*>(catalogInstance.get())!=nullptr);
    catalogInstance.reset();

    // This processor owns large, fixed DSP ring buffers. Keeping several
    // instances on the Windows test thread's ~1MB stack caused SegFault.
    auto fx=std::make_unique<ImerSipProcessor>();
    assert(fx->getName()=="ImerSip");
    // A verified native insert without a real editor is not a usable product.
    assert(fx->hasEditor());
    {
        std::unique_ptr<juce::AudioProcessorEditor> ui(fx->createEditor());
        assert(ui && ui->getWidth()==840 && ui->getHeight()==474);
        // No fake browser PCM or secondary processing engine.
    }
    assert(fx->isBusesLayoutSupported(fx->getBusesLayout()));
    fx->prepareToPlay(48000.,128);
    assert(fx->isReady());
    assert(fx->measuredStereoStageEnabled()); // real two KEMAR sources
    auto settings=fx->acceptedSettings();
    // Read the real initializer, never pin an obsolete preset value.
    // That mistake previously turned a good Windows build RED solely
    // because the MUSIC default Bass Impact changed from 75% to 55%.
    const Settings initialPreset{};
    assert(settings.bassImpact==initialPreset.bassImpact);
    assert(settings.immersion==initialPreset.immersion);
    assert(settings.space==initialPreset.space);
    assert(settings.width==initialPreset.width);
    assert(settings.depth==initialPreset.depth);
    assert(settings.stereoLock==initialPreset.stereoLock);
    juce::MemoryBlock before;
    fx->getStateInformation(before);
    assert(before.getSize()==52);
    assert(!fx->restoreUnpublishedState(before.getData(),51));
    std::array<std::uint8_t,52> corrupted{};
    std::memcpy(corrupted.data(),before.getData(),52);
    corrupted[0]=0;
    assert(!fx->restoreUnpublishedState(corrupted.data(),52));
    assert(!fx->queueSettings(settings,0));
    settings.venue=1;settings.immersion=.81f;
    settings.width=.9f;settings.bassImpact=.95f;settings.detail=.65f;
    settings.stereoLock=.92f;settings.lateRoom=.31f;
    settings.depth=.77f;settings.earAlignDegrees=-19.5f;
    assert(fx->queueSettings(settings,52));
    assert(!fx->stateSnapshotQuiescent());
    juce::AudioBuffer<float> pcm(2,128);
    juce::MidiBuffer midi;
    for(int i=0;i<128;++i) {
        const float s=.2f*std::sin(
            6.28318530718f*static_cast<float>(i)/128.f);
        pcm.setSample(0,i,s);
        pcm.setSample(1,i,-s);
    }
    fx->processBlock(pcm,midi);
    ImerSipProcessor::Completion ack{};
    assert(fx->pollCompletion(ack));
    assert(ack.requestId==52&&ack.applied);
    assert(ack.settings.venue==1);
    assert(fx->stateSnapshotQuiescent());
    assert(fx->acceptedSettings().width==.9f);
    // Reproduce Save immediately after an audio block: Save may consume
    // ACK before the editor's timer polls it. A latched record must retain
    // the applied identity and monotonic sequence across editor reopen.
    settings.detail=.72f;
    assert(fx->queueSettings(settings,53));
    fx->processBlock(pcm,midi);
    juce::MemoryBlock savedDuringEdit;
    fx->getStateInformation(savedDuringEdit);
    assert(savedDuringEdit.getSize()==52);
    assert(fx->lastCompletedRequestId()==53);
    assert(fx->lastCompletionApplied());
    assert(fx->acceptedSettings().detail==.72f);
    assert(fx->nextEditorRequestId()>53);
    for(int i=0;i<128;++i) {
        assert(std::isfinite(pcm.getSample(0,i)));
        assert(std::isfinite(pcm.getSample(1,i)));
    }
    juce::MemoryBlock snapshot;
    fx->getStateInformation(snapshot);
    assert(snapshot.getSize()==52);
    // This processor owns large, fixed DSP ring buffers. Keeping several
    // instances on the Windows test thread's ~1MB stack caused SegFault.
    auto restored=std::make_unique<ImerSipProcessor>();
    restored->prepareToPlay(48000.,128);
    assert(restored->isReady());
    assert(restored->restoreUnpublishedState(snapshot.getData(),
                                           static_cast<int>(snapshot.getSize())));
    auto selected=restored->acceptedSettings();
    assert(selected.venue==1&&selected.width==.9f);
    assert(selected.bassImpact==.95f&&selected.immersion==.81f);
    assert(selected.detail==.72f);
    assert(selected.stereoLock==.92f&&selected.lateRoom==.31f);
    assert(selected.depth==.77f);
    assert(selected.earAlignDegrees==-19.5f);
    restored->sealForRealtime();
    assert(!restored->restoreUnpublishedState(snapshot.getData(),52));
    // Old 32-byte IMRS v1 sessions must load WITHOUT unexpected Detail boost.
    juce::MemoryBlock legacy;
    {
        // JUCE MemoryOutputStream commits its destination MemoryBlock when
        // destroyed. Inspecting it while the stream is still alive used to
        // falsely reject a valid 32-byte legacy session fixture.
        juce::MemoryOutputStream v1(legacy,false);
        v1.writeInt(0x494D5253);
        v1.writeInt(1);
        v1.writeInt(selected.venue);
        v1.writeFloat(selected.immersion);
        v1.writeFloat(selected.space);
        v1.writeFloat(selected.width);
        v1.writeFloat(selected.bassImpact);
        v1.writeFloat(selected.outputTrim);
    }
    assert(legacy.getSize()==32);
    // This processor owns large, fixed DSP ring buffers. Keeping several
    // instances on the Windows test thread's ~1MB stack caused SegFault.
    auto oldSession=std::make_unique<ImerSipProcessor>();
    oldSession->prepareToPlay(48000.,128);
    assert(oldSession->isReady());
    assert(oldSession->restoreUnpublishedState(legacy.getData(),32));
    assert(oldSession->acceptedSettings().detail==0.f);
    assert(oldSession->acceptedSettings().stereoLock==1.f);
    assert(oldSession->acceptedSettings().lateRoom==.45f);
    assert(oldSession->acceptedSettings().depth==.60f);
    assert(oldSession->acceptedSettings().earAlignDegrees==0.f);
    // Legacy v2 (36-byte) must preserve DETAIL while migrating new
    // controls to music-safe defaults, not silently adopt full wet.
    juce::MemoryBlock v2;
    {
        juce::MemoryOutputStream writer(v2,false);
        writer.writeInt(0x494D5253);writer.writeInt(2);
        writer.writeInt(selected.venue);
        writer.writeFloat(selected.immersion);
        writer.writeFloat(selected.space);
        writer.writeFloat(selected.width);
        writer.writeFloat(selected.bassImpact);
        writer.writeFloat(selected.outputTrim);
        writer.writeFloat(selected.detail);
    }
    assert(v2.getSize()==36);
    auto v2Session=std::make_unique<ImerSipProcessor>();
    v2Session->prepareToPlay(48000.,128);
    assert(v2Session->restoreUnpublishedState(v2.getData(),36));
    assert(v2Session->acceptedSettings().detail==selected.detail);
    assert(v2Session->acceptedSettings().stereoLock==1.f);
    assert(v2Session->acceptedSettings().lateRoom==.45f);
    assert(v2Session->acceptedSettings().depth==.60f);
    assert(v2Session->acceptedSettings().earAlignDegrees==0.f);
    // A/B is a transient monitor control, not an accepted session edit.
    juce::MemoryBlock initialCompareState;
    v2Session->getStateInformation(initialCompareState);
    v2Session->setCompareDry(true);
    assert(v2Session->comparingOriginal());
    juce::MemoryBlock whileComparing;
    v2Session->getStateInformation(whileComparing);
    assert(whileComparing==initialCompareState);
    v2Session->setCompareDry(false);
    assert(!v2Session->comparingOriginal());
    // Existing IMRS v3 state (44 bytes) must ALSO recover on v4,
    // retaining stereo safety and late tail with new calibrated depth.
    juce::MemoryBlock v3;
    {
        juce::MemoryOutputStream w(v3,false);
        w.writeInt(0x494D5253);w.writeInt(3);
        w.writeInt(selected.venue);w.writeFloat(selected.immersion);
        w.writeFloat(selected.space);w.writeFloat(selected.width);
        w.writeFloat(selected.bassImpact);w.writeFloat(selected.outputTrim);
        w.writeFloat(selected.detail);w.writeFloat(selected.stereoLock);
        w.writeFloat(selected.lateRoom);
    }
    assert(v3.getSize()==44);
    auto v3Session=std::make_unique<ImerSipProcessor>();
    v3Session->prepareToPlay(48000.,128);
    assert(v3Session->restoreUnpublishedState(v3.getData(),44));
    assert(v3Session->acceptedSettings().stereoLock==selected.stereoLock);
    assert(v3Session->acceptedSettings().lateRoom==selected.lateRoom);
    assert(v3Session->acceptedSettings().depth==.60f);
    assert(v3Session->acceptedSettings().earAlignDegrees==0.f);
    // IMRS v4 from earlier headset sessions keeps its physical depth,
    // but cannot have a hidden HRTF alignment never stored by that ABI.
    juce::MemoryBlock v4;
    {
        juce::MemoryOutputStream w(v4,false);
        w.writeInt(0x494D5253);w.writeInt(4);
        w.writeInt(selected.venue);w.writeFloat(selected.immersion);
        w.writeFloat(selected.space);w.writeFloat(selected.width);
        w.writeFloat(selected.bassImpact);w.writeFloat(selected.outputTrim);
        w.writeFloat(selected.detail);w.writeFloat(selected.stereoLock);
        w.writeFloat(selected.lateRoom);w.writeFloat(selected.depth);
    }
    assert(v4.getSize()==48);
    auto v4Session=std::make_unique<ImerSipProcessor>();
    v4Session->prepareToPlay(48000.,128);
    assert(v4Session->restoreUnpublishedState(v4.getData(),48));
    assert(v4Session->acceptedSettings().depth==selected.depth);
    assert(v4Session->acceptedSettings().earAlignDegrees==0.f);
    // REAL JUCE processor PCM callback, not merely the standalone DSP
    // helper: prove the native FX insert produces a measurable wet room
    // output even for a CENTERED MONO orchestra instrument.
    // This processor owns large, fixed DSP ring buffers. Keeping several
    // instances on the Windows test thread's ~1MB stack caused SegFault.
    auto audible=std::make_unique<ImerSipProcessor>();
    audible->prepareToPlay(48000.,128);
    assert(audible->isReady());
    // Native JUCE editor sensitivity, not just a C++ setter.
    // The live DEEP STAGE slider MUST reach native PCM queue/accepted ACK.
    {
        std::unique_ptr<juce::AudioProcessorEditor> live(audible->createEditor());
        assert(live);
        auto* depth=dynamic_cast<juce::Slider*>(
            live->findChildWithID("imersip.depth"));
        auto* stereo=dynamic_cast<juce::Slider*>(
            live->findChildWithID("imersip.stereo"));
        auto* align=dynamic_cast<juce::Slider*>(
            live->findChildWithID("imersip.ear-align"));
        auto* power=dynamic_cast<juce::TextButton*>(
            live->findChildWithID("imersip.power"));
        auto* imer=dynamic_cast<armix::builtin_fx::ImerSipEditor*>(live.get());
        assert(depth&&stereo&&align&&power&&imer);
        assert(!power->isEnabled()); // host-controlled, no local fake power
        assert(live->getWidth()==armix::builtin_fx::ImerSipEditor::editorWidth);
        assert(live->getHeight()==armix::builtin_fx::ImerSipEditor::editorHeight);
        assert(!depth->getBounds().intersects(align->getBounds()));
        assert(!power->getBounds().intersects(stereo->getBounds()));
        bool bypassed=false;
        imer->bindHostPower([&](bool next){bypassed=next;return true;},
                            [&]{return bypassed;});
        assert(power->isEnabled()&&power->getButtonText()=="FX ON");
        power->onClick();
        assert(bypassed&&power->getButtonText()=="FX BYPASSED");
        power->onClick();
        assert(!bypassed&&power->getButtonText()=="FX ON");
        assert(std::abs(depth->getValue()-100.*Settings{}.depth)<.01);
        depth->setValue(87.,juce::sendNotificationSync);
        juce::AudioBuffer<float> pulse(2,128);pulse.clear();
        audible->processBlock(pulse,midi);
        ImerSipProcessor::Completion moved{};
        assert(audible->pollCompletion(moved));
        assert(moved.applied);
        assert(std::abs(moved.settings.depth-.87f)<1.e-5f);
        align->setValue(-19.5,juce::sendNotificationSync);
        pulse.clear();
        audible->processBlock(pulse,midi);
        ImerSipProcessor::Completion aligned{};
        assert(audible->pollCompletion(aligned)&&aligned.applied);
        assert(aligned.settings.earAlignDegrees==-19.5f);
    }
    // Reopening must not reset this insert's accepted native settings.
    {
        std::unique_ptr<juce::AudioProcessorEditor> again(audible->createEditor());
        assert(again && again->findChildWithID("imersip.power"));
        auto* restoredDepth=dynamic_cast<juce::Slider*>(
            again->findChildWithID("imersip.depth"));
        assert(restoredDepth&&std::abs(restoredDepth->getValue()-87.)<.01);
    }
    auto full=audible->acceptedSettings();
    full.venue=0;full.immersion=1.f;full.space=1.f;
    full.width=1.f;full.bassImpact=0.f;full.detail=0.f;
    full.outputTrim=1.f;
    assert(audible->queueSettings(full,6001));
    juce::AudioBuffer<float> inOut(2,128);
    double originalEnergy=0.,processedDeltaEnergy=0.;
    constexpr double pi=3.14159265358979323846;
    for(int block=0;block<256;++block) {
        for(int i=0;i<128;++i) {
            const int frame=block*128+i;
            const double time=frame/48000.;
            const float v=static_cast<float>(
                .16*std::sin(2.*pi*311.*time)+
                .045*std::sin(2.*pi*1270.*time));
            inOut.setSample(0,i,v);
            inOut.setSample(1,i,v);
        }
        audible->processBlock(inOut,midi);
        if(block>=32)for(int i=0;i<128;++i) {
            const int frame=block*128+i;
            const double time=frame/48000.;
            const double direct=.16*std::sin(2.*pi*311.*time)+
                                .045*std::sin(2.*pi*1270.*time);
            const double dl=inOut.getSample(0,i)-direct;
            const double dr=inOut.getSample(1,i)-direct;
            originalEnergy+=2.*direct*direct;
            processedDeltaEnergy+=dl*dl+dr*dr;
        }
    }
    ImerSipProcessor::Completion audibleAck{};
    assert(audible->pollCompletion(audibleAck));
    assert(audibleAck.requestId==6001&&audibleAck.applied);
    // The native editor acceptance test now processes two genuine 128f
    // PCM ACK blocks BEFORE the 256x128 full musical signal below.
    assert(audible->processedFrames()==32768u+256u);
    assert(audible->liveInputEnergy()>1.e-5f);
    assert(audible->liveDifferenceEnergy()>1.e-4f);
    const double actualDelta=std::sqrt(
        processedDeltaEnergy/(originalEnergy+1.e-16));
    std::fprintf(stderr,
        "IMERSIP_REAL_JUCE_WET_DELTA_48K=%.6f dB=%.2f\n",
        actualDelta,20.*std::log10(actualDelta+1.e-12));
    assert(actualDelta>.050);
    assert(actualDelta<.75);
    audible->releaseResources();

    fx->releaseResources();
    assert(!fx->isReady());
    return 0;
}
