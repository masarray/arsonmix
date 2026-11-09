// SPDX-License-Identifier: AGPL-3.0-only
// Real ChannelId FX seam -> other-channel music PCM -> Harmony DSP guide.
// No fake browser audio, no second callback or audible music route.
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#if defined(NDEBUG)
#undef NDEBUG // Windows Release CTest must execute assertions, never remove them.
#endif
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
    assert(live->effectiveKeyCode()==14); // manual D minor before music
    // The MAIN/no-FX callback may use an empty identity. Never call the
    // absent legacy external renderer merely because its ID also equals 0.
    float emptyL[4]{.25f,.25f,.25f,.25f},emptyR[4]{.5f,.5f,.5f,.5f};
    bank(ChannelId{},emptyL,emptyR,4);
    assert(emptyL[0]==.25f&&emptyR[0]==.5f);
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
    assert(live->effectiveKeyCode()==0); // detected C major
    // Even a source with zero FX inserts guides Harmony. Source absence
    // ages out safely without touching physical routing/output authority.
    for(int i=0;i<950;++i) {
        float l[128]{},r[128]{};
        bank(vocal,l,r,128);
    }
    assert(live->observedMusicKey()==-1);
    assert(live->effectiveKeyCode()==14); // music absent: manual D minor
    juce::MemoryBlock saved;
    live->getStateInformation(saved);
    ChannelHarmonyProcessor restored;
    // Production NativeMixerSessionFxRestore prepares first, then restores.
    // Source must be armed at the actual DSP input, not just in accepted UI.
    restored.prepareToPlay(48000.,128);
    assert(restored.isReady());
    assert(restored.restoreUnpublishedState(saved.getData(),
                                            static_cast<int>(saved.getSize())));
    assert(restored.acceptedSettings().guideMode==1);
    assert(restored.acceptedSettings().guideChannelId==music.value);
    assert(restored.musicSourceChannel()==music.value);
    assert(restored.observedMusicKey()==-1); // no stale old-device estimate
    // Version-1 state still loads as Manual Key with no source binding.
    juce::MemoryBlock old;
    {
        // JUCE MemoryOutputStream commits its external MemoryBlock on
        // destruction. Do not decode a fixture before its writer closes:
        // that used to fail on Windows Release even though v1 parsing worked.
        juce::MemoryOutputStream writer(old,false);
        writer.writeInt(0x41484d31);writer.writeInt(1);
        writer.writeInt(5);writer.writeInt(0);writer.writeInt(3);
        writer.writeBool(false);writer.writeFloat(.5f);writer.writeFloat(.5f);
        writer.writeFloat(.5f);writer.writeFloat(0.f);
    }
    assert(old.getSize()>0);
    ChannelHarmonyProcessor migrated;
    migrated.prepareToPlay(48000.,128);
    assert(migrated.isReady());
    assert(migrated.restoreUnpublishedState(old.getData(),
                                           static_cast<int>(old.getSize())));
    assert(migrated.acceptedSettings().guideMode==0);
    assert(migrated.acceptedSettings().guideChannelId==0);
    assert(migrated.musicSourceChannel()==0);
    // Music-source retarget must not bleed the old channel's guide into the
    // new source. After switching to Manual the other-channel PCM must never
    // change the selected manual key. A third row owns no FX and no audible WIRE.
    constexpr ChannelId musicB{6003};
    std::array<DynamicChannelFxLeaseBank::Assignment,3> moreRows{};
    moreRows[0].id=music;moreRows[0].format=ProcessingFormat::stereo;
    moreRows[1].id=vocal;moreRows[1].format=ProcessingFormat::stereo;
    moreRows[1].inserts.push_back(fx);
    moreRows[2].id=musicB;moreRows[2].format=ProcessingFormat::stereo;
    DynamicChannelFxLeaseBank bank2;
    assert(DynamicChannelFxLeaseBank::prepare(1,2,moreRows,bank2)==
           DynamicChannelFxLeaseBank::Fault::none);
    auto changed=settings;changed.guideChannelId=musicB.value;
    assert(live->queueAccepted(changed,502));
    float targetL[128]{},targetR[128]{};
    bank2(vocal,targetL,targetR,128);
    assert(live->pollCompletion(applied)&&applied.applied&&applied.requestId==502);
    assert(live->musicSourceChannel()==musicB.value);
    assert(live->observedMusicKey()==-1); // invalidated before first new sample
    assert(live->effectiveKeyCode()==14); // retarget: manual fallback
    for(int block=0;block<1450;++block){
        float l[128]{},r[128]{};
        for(int j=0;j<128;++j){
            const double t=static_cast<double>(block*128+j)/48000.;
            l[j]=r[j]=static_cast<float>(.09*(
                std::sin(6.28318530718*261.625565*t)+
                std::sin(6.28318530718*329.627557*t)+
                std::sin(6.28318530718*391.995436*t)));
        }
        bank2(music,l,r,128); // old source: intentionally ignored
    }
    assert(live->observedMusicKey()==-1);
    for(int block=0;block<1450;++block){
        float l[128]{},r[128]{};
        for(int j=0;j<128;++j){
            const double t=static_cast<double>(block*128+j)/48000.;
            l[j]=r[j]=static_cast<float>(.09*(
                std::sin(6.28318530718*220.*t)+
                std::sin(6.28318530718*261.625565*t)+
                std::sin(6.28318530718*329.627557*t)));
        }
        // Real anti-phase other-channel PCM remains unmodified by the guide.
        for(int j=0;j<128;++j)r[j]=-l[j];
        const float originalL=l[17],originalR=r[17];
        bank2(musicB,l,r,128);
        assert(l[17]==originalL&&r[17]==originalR);
    }
    assert(live->observedMusicKey()==9&&live->observedMusicScale()==1);
    assert(live->effectiveKeyCode()==21); // detected A minor
    auto locked=changed;locked.guideMode=0;locked.guideChannelId=0;
    locked.key=7;locked.scale=0;
    assert(live->queueAccepted(locked,503));
    bank2(vocal,targetL,targetR,128);
    assert(live->pollCompletion(applied)&&applied.applied&&applied.requestId==503);
    assert(live->musicSourceChannel()==0);
    assert(live->observedMusicKey()==-1);
    for(int block=0;block<400;++block){
        float l[128]{},r[128]{};
        for(int j=0;j<128;++j){
            const double t=static_cast<double>(block*128+j)/48000.;
            l[j]=r[j]=static_cast<float>(.09*(
                std::sin(6.28318530718*220.*t)+
                std::sin(6.28318530718*261.625565*t)+
                std::sin(6.28318530718*329.627557*t)));
        }
        bank2(musicB,l,r,128);
    }
    assert(live->observedMusicKey()==-1);
    assert(live->acceptedSettings().key==7&&
           live->acceptedSettings().scale==0&&
           live->acceptedSettings().guideMode==0);
    assert(live->effectiveKeyCode()==7); // manual G major remains locked
    // NOTE: this test verifies real PCM to DSP, not real ASIO ear monitoring
    // or polyphonic pitch-transposition quality.
}
