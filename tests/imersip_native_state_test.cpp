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
#include <limits>

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

    ImerSipProcessor fx;
    assert(fx.getName()=="ImerSip");
    // A verified native insert without a real editor is not a usable product.
    assert(fx.hasEditor());
    {
        std::unique_ptr<juce::AudioProcessorEditor> ui(fx.createEditor());
        assert(ui && ui->getWidth()==840 && ui->getHeight()==474);
        // No fake browser PCM or secondary processing engine.
    }
    assert(fx.isBusesLayoutSupported(fx.getBusesLayout()));
    fx.prepareToPlay(48000.,128);
    assert(fx.isReady());
    assert(fx.measuredStereoStageEnabled()); // real two KEMAR sources
    auto settings=fx.acceptedSettings();
    assert(settings.bassImpact==.75f);
    juce::MemoryBlock before;
    fx.getStateInformation(before);
    assert(before.getSize()==36);
    assert(!fx.restoreUnpublishedState(before.getData(),35));
    std::array<std::uint8_t,36> corrupted{};
    std::memcpy(corrupted.data(),before.getData(),36);
    corrupted[0]=0;
    assert(!fx.restoreUnpublishedState(corrupted.data(),36));
    assert(!fx.queueSettings(settings,0));
    settings.venue=1;settings.immersion=.81f;
    settings.width=.9f;settings.bassImpact=.95f;settings.detail=.65f;
    assert(fx.queueSettings(settings,52));
    assert(!fx.stateSnapshotQuiescent());
    juce::AudioBuffer<float> pcm(2,128);
    juce::MidiBuffer midi;
    for(int i=0;i<128;++i) {
        const float s=.2f*std::sin(
            6.28318530718f*static_cast<float>(i)/128.f);
        pcm.setSample(0,i,s);
        pcm.setSample(1,i,-s);
    }
    fx.processBlock(pcm,midi);
    ImerSipProcessor::Completion ack{};
    assert(fx.pollCompletion(ack));
    assert(ack.requestId==52&&ack.applied);
    assert(ack.settings.venue==1);
    assert(fx.stateSnapshotQuiescent());
    assert(fx.acceptedSettings().width==.9f);
    // Reproduce Save immediately after an audio block: Save may consume
    // ACK before the editor's timer polls it. A latched record must retain
    // the applied identity and monotonic sequence across editor reopen.
    settings.detail=.72f;
    assert(fx.queueSettings(settings,53));
    fx.processBlock(pcm,midi);
    juce::MemoryBlock savedDuringEdit;
    fx.getStateInformation(savedDuringEdit);
    assert(savedDuringEdit.getSize()==36);
    assert(fx.lastCompletedRequestId()==53);
    assert(fx.lastCompletionApplied());
    assert(fx.acceptedSettings().detail==.72f);
    assert(fx.nextEditorRequestId()>53);
    for(int i=0;i<128;++i) {
        assert(std::isfinite(pcm.getSample(0,i)));
        assert(std::isfinite(pcm.getSample(1,i)));
    }
    juce::MemoryBlock snapshot;
    fx.getStateInformation(snapshot);
    assert(snapshot.getSize()==36);
    ImerSipProcessor restored;
    restored.prepareToPlay(48000.,128);
    assert(restored.isReady());
    assert(restored.restoreUnpublishedState(snapshot.getData(),
                                           static_cast<int>(snapshot.getSize())));
    auto selected=restored.acceptedSettings();
    assert(selected.venue==1&&selected.width==.9f);
    assert(selected.bassImpact==.95f&&selected.immersion==.81f);
    assert(selected.detail==.72f);
    restored.sealForRealtime();
    assert(!restored.restoreUnpublishedState(snapshot.getData(),36));
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
    ImerSipProcessor oldSession;
    oldSession.prepareToPlay(48000.,128);
    assert(oldSession.isReady());
    assert(oldSession.restoreUnpublishedState(legacy.getData(),32));
    assert(oldSession.acceptedSettings().detail==0.f);
    fx.releaseResources();
    assert(!fx.isReady());
    return 0;
}
