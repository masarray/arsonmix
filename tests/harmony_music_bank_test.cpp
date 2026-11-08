// SPDX-License-Identifier: AGPL-3.0-only
// Real ChannelId FX seam -> other-channel music PCM -> Harmony DSP guide.
// No fake browser audio, no second callback or audible music route.
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#include <cassert>
#include <cmath>
#include <memory>
#include <array>
using namespace armix::mixer;
using armix::builtin_fx::ChannelHarmonyProcessor;

int main(){
    constexpr ChannelId music{6001},vocal{6002};
    auto processor=std::make_unique<ChannelHarmonyProcessor>();
    processor->prepareToPlay(48000.,128);
    assert(processor->isReady());
    auto* live=processor.get();
    auto fx=std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(processor),ChannelHarmonyProcessor::catalogIdentity,
        "ArSon Harmony",512);
    std::array<DynamicChannelFxLeaseBank::Assignment,2> rows{};
    rows[0].id=music;rows[0].format=ProcessingFormat::stereo;
    rows[1].id=vocal;rows[1].format=ProcessingFormat::stereo;
    rows[1].inserts.push_back(fx);
    DynamicChannelFxLeaseBank bank;
    assert(DynamicChannelFxLeaseBank::prepare(1,1,rows,bank)==
           DynamicChannelFxLeaseBank::Fault::none);
    armix::harmony::Settings settings;
    settings.guideMode=1;settings.guideChannelId=music.value;
    settings.key=2;settings.scale=1;settings.wet=0.f;
    assert(live->queueAccepted(settings,501));
    float dryL[128]{},dryR[128]{};
    bank(vocal,dryL,dryR,128); // audio callback applies staged settings
    ChannelHarmonyProcessor::Completion applied{};
    assert(live->pollCompletion(applied)&&applied.applied&&applied.requestId==501);
    assert(live->acceptedSettings().guideChannelId==music.value);
    assert(live->musicSourceChannel()==music.value);
    for(int block=0;block<1400;++block){
        float l[128]{},r[128]{};
        for(int j=0;j<128;++j) {
            const int index=block*128+j;
            const double t=static_cast<double>(index)/48000.;
            const float sample=static_cast<float>(.09*(
                std::sin(6.28318530718*261.625565*t)+
                std::sin(6.28318530718*329.627557*t)+
                std::sin(6.28318530718*391.995436*t)));
            l[j]=r[j]=sample;
        }
        const float before=l[17];
        bank(music,l,r,128); // actual accepted source audio is READ only
        assert(std::abs(l[17]-before)<1.e-7f);
    }
    assert(live->observedMusicKey()==0 && live->observedMusicScale()==0);
    assert(live->observedMusicConfidence()>=.045f);
    // Even a source with zero FX inserts guides Harmony. Source absence
    // ages out safely without touching physical routing/output authority.
    for(int i=0;i<950;++i) {
        float l[128]{},r[128]{};
        bank(vocal,l,r,128);
    }
    assert(live->observedMusicKey()==-1);
    juce::MemoryBlock saved;
    live->getStateInformation(saved);
    ChannelHarmonyProcessor restored;
    assert(restored.restoreUnpublishedState(saved.getData(),
                                            static_cast<int>(saved.getSize())));
    assert(restored.acceptedSettings().guideMode==1);
    assert(restored.acceptedSettings().guideChannelId==music.value);
    // Version-1 state still loads as Manual Key with no source binding.
    juce::MemoryBlock old;
    juce::MemoryOutputStream writer(old,false);
    writer.writeInt(0x41484d31);writer.writeInt(1);
    writer.writeInt(5);writer.writeInt(0);writer.writeInt(3);
    writer.writeBool(false);writer.writeFloat(.5f);writer.writeFloat(.5f);
    writer.writeFloat(.5f);writer.writeFloat(0.f);
    ChannelHarmonyProcessor migrated;
    assert(migrated.restoreUnpublishedState(old.getData(),
                                           static_cast<int>(old.getSize())));
    assert(migrated.acceptedSettings().guideMode==0);
    assert(migrated.acceptedSettings().guideChannelId==0);
    // NOTE: this test verifies real PCM to DSP, not real ASIO ear monitoring
    // or polyphonic pitch-transposition quality.
}
