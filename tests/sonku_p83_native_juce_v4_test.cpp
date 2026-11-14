// SPDX-License-Identifier: AGPL-3.0-only
// REAL JUCE native processor P8.3 v4: SPSC -> PCM -> fade transaction ->
// APPLIED ACK/readback -> state v4, backward v1/v2/v3, device reprepare.
// No WebAudio or ASIO hardware claim. This is NOT an ear-localization FAT.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) || !ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
#error P8.3 real native adapter gate must enable the experimental build
#endif
#include "../engine/builtin_fx/SonKuSpatialProcessor.h"
#include "../src/SonKuNativeBridgeContract.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
using armix::builtin_fx::SonKuSpatialProcessor;
using armix::sonku::Settings;
static void render(SonKuSpatialProcessor& dsp,int iteration){
    juce::AudioBuffer<float> pcm(2,128);
    juce::MidiBuffer midi;
    for(int i=0;i<128;++i){
        const float t=static_cast<float>(iteration*128+i)/48000.f;
        const float x=.10f*std::sin(2.f*3.14159265f*197.f*t);
        const float y=.06f*std::sin(2.f*3.14159265f*523.f*t);
        pcm.setSample(0,i,x+y);
        pcm.setSample(1,i,x-y);
    }
    dsp.processBlock(pcm,midi);
    for(int c=0;c<2;++c)
      for(int i=0;i<128;++i) {
        assert(std::isfinite(pcm.getSample(c,i)));
        assert(std::abs(pcm.getSample(c,i))<1.f);
      }
}
static bool complete(SonKuSpatialProcessor& dsp,std::uint64_t request,
                     int start){
    SonKuSpatialProcessor::Completion ack{};
    for(int n=start;n<start+24;++n) {
        render(dsp,n);
        if(dsp.pollCompletion(ack)) {
            assert(ack.requestId==request && ack.applied);
            return true;
        }
    }
    return false;
}
int main(){
    using namespace armix::builtin_fx::sonku_bridge;
    static_assert(schemaVersion()==4);
    auto p=std::make_unique<SonKuSpatialProcessor>();
    p->prepareToPlay(48000.,128);
    assert(p->isReady());
    p->sealForRealtime(); // explicitly published graph: require audio ACK
    assert(p->preparedReflectionProfile()==
        armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch);
    Settings s=p->acceptedSettings();
    assert(s.renderMode==0 && !s.musicCompatibleLowFrequency);
    s.moving=false;s.startAzimuthDegrees=0.f;
    s.spacePreset=1;s.sourceDistanceMeters=1.6f;
    s.roomAmount=.55f;s.ambience=.30f;
    s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.35f;
    s.directElevationDegrees=0.f;s.renderMode=2;
    s.musicCompatibleLowFrequency=true;s.headYawDegrees=0.f;
    auto json=acceptedSettings(s);
    Settings parsed{};
    assert(decodeSettings(json,parsed));
    assert(parsed.renderMode==2&&parsed.musicCompatibleLowFrequency);
    for(int bad:{-1,3,29}){
        auto broken=acceptedSettings(s);
        broken.getDynamicObject()->setProperty("renderMode",bad);
        assert(!decodeSettings(broken,parsed));
    }
    auto wrong=acceptedSettings(s);
    wrong.getDynamicObject()->setProperty("headYawDegrees","not a number");
    assert(!decodeSettings(wrong,parsed));
    wrong=acceptedSettings(s);
    wrong.getDynamicObject()->setProperty("musicCompatibleLowFrequency",1);
    assert(!decodeSettings(wrong,parsed));
    wrong=acceptedSettings(s);
    wrong.getDynamicObject()->setProperty("headYawDegrees",400.);
    assert(!decodeSettings(wrong,parsed));
    SonKuSpatialProcessor::Completion done{};
    assert(p->queueSettings(s,101));
    assert(!p->pollCompletion(done)); // queued is NEVER applied
    render(*p,0);
    assert(!p->pollCompletion(done)); // fade-out not yet switched
    assert(!p->stateSnapshotQuiescent()); // staged edit not silently savable
    assert(p->acceptedSettings().renderMode==0);
    assert(complete(*p,101,1));
    assert(p->acceptedSettings().renderMode==2);
    assert(p->acceptedSettings().musicCompatibleLowFrequency);
    juce::MemoryBlock bytes;
    p->getStateInformation(bytes);
    assert(bytes.getSize()==59);
    juce::MemoryInputStream check(bytes.getData(),bytes.getSize(),false);
    assert(check.readInt()==0x534F4E4B && check.readInt()==4);
    auto clone=std::make_unique<SonKuSpatialProcessor>();
    clone->prepareToPlay(44100.,128);
    assert(clone->restoreUnpublishedState(bytes.getData(),
                                          static_cast<int>(bytes.getSize())));
    assert(clone->acceptedSettings().renderMode==2);
    assert(clone->acceptedSettings().musicCompatibleLowFrequency);
    clone->prepareToPlay(48000.,256);
    assert(clone->isReady()&&clone->acceptedSettings().renderMode==2);
    render(*clone,100);

    s.headYawDegrees=90.f;
    assert(p->queueSettings(s,102));
    assert(complete(*p,102,40));
    assert(p->acceptedSettings().headYawDegrees==90.f);
    s.musicCompatibleLowFrequency=false;
    assert(p->queueSettings(s,103));
    assert(complete(*p,103,70));
    assert(!p->acceptedSettings().musicCompatibleLowFrequency);
    s.renderMode=1;s.stereoSpreadDegrees=0.f;
    assert(p->queueSettings(s,104));
    assert(complete(*p,104,100));
    assert(p->acceptedSettings().renderMode==1);
    s.renderMode=0;s.stereoSpreadDegrees=60.f;
    assert(p->queueSettings(s,105));
    assert(complete(*p,105,130));
    assert(p->acceptedSettings().renderMode==0);
    auto invalid=s;invalid.renderMode=2;invalid.stereoSpreadDegrees=0.f;
    assert(!p->queueSettings(invalid,106));
    invalid=s;invalid.headYawDegrees=400.f;
    assert(!p->queueSettings(invalid,106));
    assert(!p->pollCompletion(done));

    auto broken=std::make_unique<SonKuSpatialProcessor>();
    broken->prepareToPlay(48000.,128);
    assert(!broken->restoreUnpublishedState(bytes.getData(),58));
    juce::MemoryBlock corrupt(bytes);
    auto* data=static_cast<std::uint8_t*>(corrupt.getData());
    data[54]=0xff;data[55]=0xff;data[56]=0xff;
    assert(!broken->restoreUnpublishedState(corrupt.getData(),59));
    // The unpublished/logical FX control path has no audio callback.
    // Mode transition MUST complete immediately there, not await a fade.
    auto offline=std::make_unique<SonKuSpatialProcessor>();
    offline->prepareToPlay(48000.,128);
    assert(offline->queueSettings(s,901));
    offline->consumeRealtimeEdits();
    SonKuSpatialProcessor::Completion saved{};
    assert(offline->pollCompletion(saved)&&saved.applied&&saved.requestId==901);
    assert(offline->stateSnapshotQuiescent());
    assert(offline->acceptedSettings().renderMode==0);
    auto offlineSpeaker=s;offlineSpeaker.renderMode=2;
    offlineSpeaker.musicCompatibleLowFrequency=true;
    assert(offline->queueSettings(offlineSpeaker,902));
    offline->consumeRealtimeEdits(); // no native audio clock available
    assert(offline->pollCompletion(saved)&&saved.applied&&saved.requestId==902);
    assert(offline->acceptedSettings().renderMode==2);
    // Exact legacy state shapes are still accepted and upgrade to v4 defaults.
    juce::MemoryBlock v3;
    {juce::MemoryOutputStream out(v3,false);
      out.writeInt(0x534F4E4B);out.writeInt(3);
      out.writeFloat(0.f);out.writeFloat(12.f);out.writeFloat(60.f);
      out.writeFloat(1.f);out.writeFloat(.75f);
      out.writeByte(0);out.writeByte(1);
      out.writeInt(1);out.writeFloat(1.6f);out.writeFloat(.55f);
      out.writeFloat(.3f);out.writeFloat(0.f);}
    assert(v3.getSize()==50);
    assert(broken->restoreUnpublishedState(v3.getData(),50));
    assert(broken->acceptedSettings().renderMode==0);
    assert(broken->acceptedSettings().directElevationDegrees==0.f);
    juce::MemoryBlock v2;
    {juce::MemoryOutputStream out(v2,false);
      out.writeInt(0x534F4E4B);out.writeInt(2);
      out.writeFloat(0.f);out.writeFloat(12.f);out.writeFloat(60.f);
      out.writeFloat(1.f);out.writeFloat(.75f);
      out.writeByte(0);out.writeByte(1);
      out.writeInt(1);out.writeFloat(1.6f);out.writeFloat(.55f);
      out.writeFloat(.3f);}
    assert(v2.getSize()==46);
    assert(broken->restoreUnpublishedState(v2.getData(),46));
    assert(broken->acceptedSettings().renderMode==0);
    juce::MemoryBlock v1;
    {juce::MemoryOutputStream out(v1,false);
      out.writeInt(0x534F4E4B);out.writeInt(1);
      out.writeFloat(0.f);out.writeFloat(12.f);out.writeFloat(60.f);
      out.writeFloat(1.f);out.writeFloat(.75f);
      out.writeByte(0);out.writeByte(1);}
    assert(v1.getSize()==30);
    assert(broken->restoreUnpublishedState(v1.getData(),30));
    broken->sealForRealtime();
    assert(!broken->restoreUnpublishedState(bytes.getData(),59));
    return 0;
}
