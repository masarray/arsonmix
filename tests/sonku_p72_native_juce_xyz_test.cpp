// SPDX-License-Identifier: AGPL-3.0-only
// P7.2 exact real JUCE adapter: SPSC edit -> callback -> applied ACK,
// experimental v3 50-byte state, v1/v2 restore, corruption and restart.
// The normal product/catalog/installer stay on v2 and LIGHT.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) || !ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
#error P7.2 real JUCE test MUST enable native XYZ opt-in
#endif
#include "../engine/builtin_fx/SonKuSpatialProcessor.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
int main(){
 using armix::builtin_fx::SonKuSpatialProcessor;
 using armix::sonku::SonKuReflectionProfile;
 using armix::sonku::Settings;
 auto live=std::make_unique<SonKuSpatialProcessor>();
 auto flat=std::make_unique<SonKuSpatialProcessor>();
 live->prepareToPlay(48000.,128);
 flat->prepareToPlay(48000.,128);
 assert(live->isReady()&&flat->isReady());
 assert(live->preparedReflectionProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch);
 auto scene=live->acceptedSettings();
 assert(scene.spacePreset==0 && scene.directElevationDegrees==0.f);
 scene.spacePreset=1;scene.sourceDistanceMeters=1.6f;
 scene.roomAmount=.6f;scene.ambience=.15f;
 scene.stereoSpreadDegrees=60.f;scene.outputTrim=.55f;
 scene.moving=false;scene.startAzimuthDegrees=45.f;
 assert(armix::sonku::valid(scene));
 assert(live->queueSettings(scene,1001)&&flat->queueSettings(scene,1001));
 assert(live->acceptedSettings().spacePreset==0);
 juce::AudioBuffer<float> audio(2,128),original(2,128);
 juce::MidiBuffer midi;
 auto render=[&](int n){
   audio.clear();original.clear();
   for(int k=0;k<128;++k){
      const float a=.10f*std::sin((n*128+k)*.039f);
      const float b=.06f*std::sin((n*128+k)*.32f);
      audio.setSample(0,k,a+b);audio.setSample(1,k,a-b);
      original.setSample(0,k,a+b);original.setSample(1,k,a-b);
   }
   live->processBlock(audio,midi);
   flat->processBlock(original,midi);
   for(int c=0;c<2;++c)for(int k=0;k<128;++k){
     assert(std::isfinite(audio.getSample(c,k)));
     assert(std::abs(audio.getSample(c,k))<1.f);
   }
 };
 render(0);
 SonKuSpatialProcessor::Completion ack{};
 assert(live->pollCompletion(ack)&&ack.applied&&ack.requestId==1001);
 assert(flat->pollCompletion(ack)&&ack.applied);
 assert(live->acceptedSettings().spacePreset==1);
 for(int n=1;n<38;++n)render(n);
 auto bad=scene;bad.directElevationDegrees=70.f;
 assert(!live->queueSettings(bad,1002));
 assert(live->acceptedSettings().directElevationDegrees==0.f);
 bad=scene;bad.directElevationDegrees=25.f;bad.sourceDistanceMeters=10.f;
 assert(!live->queueSettings(bad,1002));
 assert(!live->pollCompletion(ack));

 scene.directElevationDegrees=25.f;
 assert(live->queueSettings(scene,1003));
 assert(live->acceptedSettings().directElevationDegrees==0.f);
 render(38);
 assert(live->pollCompletion(ack)&&ack.applied&&ack.requestId==1003);
 assert(ack.settings.directElevationDegrees==25.f);
 assert(live->acceptedSettings().directElevationDegrees==25.f);
 double changed=0.;
 for(int n=39;n<95;++n){
   render(n);
   for(int k=0;k<128;++k)
     changed+=std::abs(audio.getSample(0,k)-original.getSample(0,k))+
              std::abs(audio.getSample(1,k)-original.getSample(1,k));
 }
 assert(changed>.01);
 // State snapshot must serialize only AUDIO-ACKed geometry, not queued.
 juce::MemoryBlock bytes;
 live->getStateInformation(bytes);
 assert(bytes.getSize()==50);
 juce::MemoryInputStream check(bytes.getData(),50,false);
 assert(check.readInt()==0x534F4E4B&&check.readInt()==3);
 auto restore=std::make_unique<SonKuSpatialProcessor>();
 restore->prepareToPlay(44100.,128);
 assert(restore->restoreUnpublishedState(bytes.getData(),50));
 assert(restore->acceptedSettings().directElevationDegrees==25.f);
 assert(restore->acceptedSettings().spacePreset==1);
 restore->prepareToPlay(48000.,256); // ASIO format reprepare, no hardware
 assert(restore->isReady()&&restore->acceptedSettings().directElevationDegrees==25.f);
 juce::AudioBuffer<float> after(2,64);
 after.clear();after.setSample(0,0,.1f);
 restore->processBlock(after,midi);
 for(int c=0;c<2;++c)for(int k=0;k<64;++k)
    assert(std::isfinite(after.getSample(c,k)));

 // Copy a valid but incomplete v3 blob (49 bytes) and bad schema version.
 assert(!restore->restoreUnpublishedState(bytes.getData(),49));
 juce::MemoryBlock corrupt(bytes);
 auto* data=static_cast<std::uint8_t*>(corrupt.getData());
 for(int k=46;k<50;++k)data[k]=0xff; // NaN floats are invalid
 auto fails=std::make_unique<SonKuSpatialProcessor>();
 fails->prepareToPlay(48000.,128);
 assert(!fails->restoreUnpublishedState(corrupt.getData(),50));
 assert(fails->acceptedSettings().directElevationDegrees==0.f);

 // v2 old 46-byte scene and strict v1 30-byte ROOM OFF restore.
 juce::MemoryBlock v2;
 {juce::MemoryOutputStream out(v2,false);
  out.writeInt(0x534F4E4B);out.writeInt(2);
  out.writeFloat(35.f);out.writeFloat(3.7f);out.writeFloat(60.f);
  out.writeFloat(1.f);out.writeFloat(.6f);out.writeByte(1);out.writeByte(1);
  out.writeInt(1);out.writeFloat(1.6f);out.writeFloat(.58f);out.writeFloat(.2f);}
 assert(v2.getSize()==46);
 auto older=std::make_unique<SonKuSpatialProcessor>();
 older->prepareToPlay(48000.,128);
 assert(older->restoreUnpublishedState(v2.getData(),46));
 assert(older->acceptedSettings().directElevationDegrees==0.f);
 assert(older->acceptedSettings().spacePreset==1);
 juce::MemoryBlock v1;
 {juce::MemoryOutputStream out(v1,false);
  out.writeInt(0x534F4E4B);out.writeInt(1);
  out.writeFloat(0.f);out.writeFloat(12.f);out.writeFloat(60.f);
  out.writeFloat(1.f);out.writeFloat(.75f);out.writeByte(1);out.writeByte(1);}
 assert(v1.getSize()==30);
 auto legacy=std::make_unique<SonKuSpatialProcessor>();
 legacy->prepareToPlay(48000.,128);
 assert(legacy->restoreUnpublishedState(v1.getData(),30));
 assert(legacy->acceptedSettings().spacePreset==0);
 assert(legacy->acceptedSettings().directElevationDegrees==0.f);
 legacy->sealForRealtime();
 assert(!legacy->restoreUnpublishedState(v2.getData(),46));
 return 0;
}
