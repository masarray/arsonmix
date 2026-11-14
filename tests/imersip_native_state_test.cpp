// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipProcessor.h"
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
    ImerSipProcessor fx;
    assert(fx.getName()=="ImerSip");
    assert(fx.isBusesLayoutSupported(fx.getBusesLayout()));
    fx.prepareToPlay(48000.,128);
    assert(fx.isReady());
    auto settings=fx.acceptedSettings();
    assert(settings.bassImpact==.75f);
    juce::MemoryBlock before;
    fx.getStateInformation(before);
    assert(before.getSize()==32);
    assert(!fx.restoreUnpublishedState(before.getData(),31));
    std::array<std::uint8_t,32> corrupted{};
    std::memcpy(corrupted.data(),before.getData(),32);
    corrupted[0]=0;
    assert(!fx.restoreUnpublishedState(corrupted.data(),32));
    assert(!fx.queueSettings(settings,0));
    settings.venue=1;settings.immersion=.81f;
    settings.width=.9f;settings.bassImpact=.95f;
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
    for(int i=0;i<128;++i) {
        assert(std::isfinite(pcm.getSample(0,i)));
        assert(std::isfinite(pcm.getSample(1,i)));
    }
    juce::MemoryBlock snapshot;
    fx.getStateInformation(snapshot);
    assert(snapshot.getSize()==32);
    ImerSipProcessor restored;
    restored.prepareToPlay(48000.,128);
    assert(restored.isReady());
    assert(restored.restoreUnpublishedState(snapshot.getData(),
                                           static_cast<int>(snapshot.getSize())));
    auto selected=restored.acceptedSettings();
    assert(selected.venue==1&&selected.width==.9f);
    assert(selected.bassImpact==.95f&&selected.immersion==.81f);
    restored.sealForRealtime();
    assert(!restored.restoreUnpublishedState(snapshot.getData(),32));
    fx.releaseResources();
    assert(!fx.isReady());
    return 0;
}
