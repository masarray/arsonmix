// SPDX-License-Identifier: AGPL-3.0-only
// Real JUCE SonKu adapter: authoritative audio ACK, v1/v2 persistence,
// corrupt-state rejection and real 2-channel PCM after restore.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/SonKuSpatialProcessor.h"
#include "../src/SonKuNativeBridgeContract.h"
#include <cassert>
#include <cmath>
#include <cstdint>
int main(){
    using armix::builtin_fx::SonKuSpatialProcessor;
    SonKuSpatialProcessor live;
    live.prepareToPlay(48000.,128);
    assert(live.isReady());
#if defined(ARMIX_SONKU_EXPERIMENTAL_DIRECTIONAL_RT) && ARMIX_SONKU_EXPERIMENTAL_DIRECTIONAL_RT
    assert(live.preparedReflectionProfile()==
        armix::sonku::SonKuReflectionProfile::DirectionalKemar);
#else
    assert(live.preparedReflectionProfile()==
        armix::sonku::SonKuReflectionProfile::Lightweight);
#endif
    auto scene=live.acceptedSettings();
    assert(scene.spacePreset==0); // legacy samples always load with ROOM OFF
    scene.spacePreset=2;scene.sourceDistanceMeters=5.f;
    scene.roomAmount=.55f;scene.ambience=.30f;
    assert(armix::sonku::valid(scene));
    // Shipping editor/bridge must NEVER pretend P7.3 elevation is active.
    using namespace armix::builtin_fx::sonku_bridge;
    static_assert(schemaVersion()==2);
    armix::sonku::Settings decoded{};
    auto json=acceptedSettings(scene);
    assert(decodeSettings(json,decoded));
    assert(decoded.spacePreset==2&&decoded.sourceDistanceMeters==5.f);
    auto incompatible=acceptedSettings(scene);
    incompatible.getDynamicObject()->setProperty("directElevationDegrees",0.f);
    assert(!decodeSettings(incompatible,decoded));
    assert(decoded.spacePreset==2);
    assert(live.queueSettings(scene,42));
    // Queued does NOT mean accepted; only the actual PCM callback may apply.
    assert(live.acceptedSettings().spacePreset==0);
    juce::AudioBuffer<float> audio(2,128);
    juce::MidiBuffer midi;
    audio.clear();
    audio.getWritePointer(0)[0]=.1f;
    audio.getWritePointer(1)[0]=.1f;
    live.processBlock(audio,midi);
    SonKuSpatialProcessor::Completion ack{};
    assert(live.pollCompletion(ack) && ack.applied && ack.requestId==42);
    assert(live.acceptedSettings().spacePreset==2);
    assert(live.acceptedSettings().sourceDistanceMeters==5.f);
    for(int c=0;c<2;++c)
        for(int i=0;i<128;++i)assert(std::isfinite(audio.getSample(c,i)));
    juce::MemoryBlock bytes;
    live.getStateInformation(bytes);
    assert(bytes.getSize()==46);
    SonKuSpatialProcessor restored;
    restored.prepareToPlay(48000.,128);
    assert(restored.preparedReflectionProfile()==live.preparedReflectionProfile());
    assert(restored.restoreUnpublishedState(bytes.getData(),
                                           static_cast<int>(bytes.getSize())));
    const auto got=restored.acceptedSettings();
    assert(got.spacePreset==2&&got.sourceDistanceMeters==5.f);
    assert(got.roomAmount==.55f&&got.ambience==.30f);
    assert(restored.getTailLengthSeconds()>=2.);
    audio.clear();audio.getWritePointer(0)[0]=.1f;
    restored.processBlock(audio,midi);
    for(int c=0;c<2;++c)
        for(int i=0;i<128;++i)assert(std::isfinite(audio.getSample(c,i)));
    // Strict v1 binary payload: SONK, v1, five floats and two bool bytes.
    juce::MemoryBlock legacy;
    {juce::MemoryOutputStream out(legacy,false);
     out.writeInt(0x534F4E4B);out.writeInt(1);
     out.writeFloat(0.f);out.writeFloat(12.f);out.writeFloat(60.f);
     out.writeFloat(1.f);out.writeFloat(.75f);
     out.writeByte(1);out.writeByte(1);}
    assert(legacy.getSize()==30);
    SonKuSpatialProcessor older;
    older.prepareToPlay(48000.,128);
    assert(older.restoreUnpublishedState(legacy.getData(),30));
    assert(older.acceptedSettings().spacePreset==0);
    assert(!older.restoreUnpublishedState(legacy.getData(),29));
    juce::MemoryBlock invalid=bytes;
    auto* data=static_cast<std::uint8_t*>(invalid.getData());
    // Space preset starts at byte offset 30: reject out-of-range v2 schema.
    data[30]=99;data[31]=data[32]=data[33]=0;
    SonKuSpatialProcessor bad;
    bad.prepareToPlay(48000.,128);
    assert(!bad.restoreUnpublishedState(invalid.getData(),46));
    assert(bad.acceptedSettings().spacePreset==0);
    // Stale/completed active buffers may not be used as a published restore.
    restored.sealForRealtime();
    assert(!restored.restoreUnpublishedState(bytes.getData(),46));
    return 0;
}
