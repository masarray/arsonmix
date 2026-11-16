// SPDX-License-Identifier: AGPL-3.0-only
// REAL native Windows JUCE v6 SonKu P8.4 headphone research:
// P8.2 -> native queued v6 -> fading -> AUDIO APPLIED ACK -> readback,
// v1..v6 .smix state, old v4 rejected physical rollback and no extra graph.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) || !ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
#error Native JUCE v6 test requires explicit private P8.4 research build
#endif
#include "../engine/builtin_fx/SonKuSpatialProcessor.h"
#include "../src/SonKuNativeBridgeContract.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
using armix::builtin_fx::SonKuSpatialProcessor;
using armix::sonku::Settings;
using namespace armix::builtin_fx::sonku_bridge;
static constexpr float pi=3.14159265358979323846f;
static double render(SonKuSpatialProcessor& fx,int sampleOffset) {
 juce::AudioBuffer<float> pcm(2,128);
 juce::MidiBuffer midi;
 for(int i=0;i<128;++i){
  const float t=static_cast<float>(sampleOffset+i)/48000.f;
  const float main=.14f*std::sin(2*pi*311*t)+.08f*std::sin(2*pi*719*t);
  const float side=.08f*std::sin(2*pi*1703*t);
  pcm.setSample(0,i,main+side);
  pcm.setSample(1,i,main-side);
 }
 fx.processBlock(pcm,midi);
 double e=0;
 for(int i=0;i<128;++i){
  const auto L=pcm.getSample(0,i),R=pcm.getSample(1,i);
  assert(std::isfinite(L)&&std::isfinite(R));
  assert(std::abs(L)<1.f&&std::abs(R)<1.f);
  e+=L*L+R*R;
 }
 return e;
}
static void complete(SonKuSpatialProcessor& fx,std::uint64_t id,int seed) {
 SonKuSpatialProcessor::Completion ack{};
 bool ok=false;
 for(int n=0;n<36;++n){
  render(fx,(seed+n)*128);
  if(fx.pollCompletion(ack)){
   assert(ack.requestId==id && ack.applied);
   ok=true;break;
  }
 }
 assert(ok && "P85 must ACK only after native PCM applies settings");
 for(int n=0;n<20;++n) render(fx,(seed+40+n)*128);
 assert(fx.stateSnapshotQuiescent());
}
static juce::MemoryBlock legacy(int version,int oldMode=2){
 juce::MemoryBlock bytes;
 {
  juce::MemoryOutputStream o(bytes,false);
  o.writeInt(0x534f4e4b);o.writeInt(version);
 o.writeFloat(0.f);o.writeFloat(12.f);o.writeFloat(60.f);
 o.writeFloat(1.f);o.writeFloat(.75f);
 o.writeByte(0);o.writeByte(1);
 if(version>=2){
  o.writeInt(1);o.writeFloat(1.6f);o.writeFloat(.55f);
  o.writeFloat(.30f);
 }
 if(version>=3)o.writeFloat(0.f);
  if(version>=4){
   o.writeInt(oldMode);o.writeByte(version==5?0:1);o.writeFloat(0.f);
  }
 } // MemoryOutputStream must flush byte count before asserting the block.
 assert(bytes.getSize()==static_cast<std::size_t>(
     version==1?30:version==2?46:version==3?50:59));
 return bytes;
}
int main(){
 static_assert(schemaVersion()==6);
 auto fx=std::make_unique<SonKuSpatialProcessor>();
 fx->prepareToPlay(48000.,128);
 assert(fx->isReady());
 assert(fx->preparedReflectionProfile()==
     armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch);
 fx->sealForRealtime();
 Settings s=fx->acceptedSettings();
 assert(s.renderMode==0);
 s.moving=false;s.startAzimuthDegrees=0.f;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.spacePreset=1;s.sourceDistanceMeters=1.6f;s.roomAmount=.55f;s.ambience=.3f;
 s.headYawDegrees=0.f;s.musicCompatibleLowFrequency=false;s.renderMode=4;
 auto json=acceptedSettings(s);
 Settings decoded{};
 assert(decodeSettings(json,decoded)&&decoded.renderMode==4);
 for(int rejected:{-1,1,2,5,63}){
  auto bad=acceptedSettings(s);
  bad.getDynamicObject()->setProperty("renderMode",rejected);
  assert(!decodeSettings(bad,decoded));
 }
 auto bad=acceptedSettings(s);
 bad.getDynamicObject()->setProperty("musicCompatibleLowFrequency",true);
 assert(!decodeSettings(bad,decoded));
 bad=acceptedSettings(s);
 bad.getDynamicObject()->setProperty("headYawDegrees","invalid");
 assert(!decodeSettings(bad,decoded));
 assert(fx->queueSettings(s,801));
 SonKuSpatialProcessor::Completion ack{};
 assert(!fx->pollCompletion(ack));
 render(*fx,0);
 assert(!fx->pollCompletion(ack));
 assert(!fx->stateSnapshotQuiescent());
 assert(fx->acceptedSettings().renderMode==0);
 complete(*fx,801,1);
 assert(fx->acceptedSettings().renderMode==4);
 juce::MemoryBlock snapshot;
 fx->getStateInformation(snapshot);
 assert(snapshot.getSize()==59);
 juce::MemoryInputStream input(snapshot.getData(),snapshot.getSize(),false);
 assert(input.readInt()==0x534f4e4b&&input.readInt()==6);
 auto restored=std::make_unique<SonKuSpatialProcessor>();
 restored->prepareToPlay(44100.,128);
 assert(restored->restoreUnpublishedState(
     snapshot.getData(),static_cast<int>(snapshot.getSize())));
 assert(restored->acceptedSettings().renderMode==4);
 restored->prepareToPlay(48000.,256);
 assert(restored->isReady() && restored->acceptedSettings().renderMode==4);
 // Actual direct FIR must distinguish P8.4 from P8.2 without double engine.
 auto golden=std::make_unique<SonKuSpatialProcessor>();
 golden->prepareToPlay(48000.,128);
 auto g=s;g.renderMode=0;
 assert(golden->queueSettings(g,811));
 golden->consumeRealtimeEdits();
 assert(golden->pollCompletion(ack)&&ack.applied&&ack.requestId==811);
 double reference=0,fused=0;
 for(int n=0;n<50;++n){
  reference+=render(*golden,(n+200)*128);
  fused+=render(*restored,(n+200)*128);
 }
 assert(reference>0.1 && fused>0.1);
 assert(std::abs(fused-reference)>0.0002);
 const double relativeDb=10.*std::log10(fused/reference);
 assert(std::abs(relativeDb)<1.5);
 // Audio-thread head-yaw change is a separate accepted v6 transaction.
 s.headYawDegrees=60.f;
 assert(fx->queueSettings(s,802));
 complete(*fx,802,400);
 assert(fx->acceptedSettings().renderMode==4);
 assert(fx->acceptedSettings().headYawDegrees==60.f);
 s.renderMode=0;s.headYawDegrees=0.f;
 assert(fx->queueSettings(s,803));
 complete(*fx,803,480);
 assert(fx->acceptedSettings().renderMode==0);
 // Unpublished workspace has no PCM clock: edits ACK without fade.
 auto offline=std::make_unique<SonKuSpatialProcessor>();
 offline->prepareToPlay(48000.,128);
 s.renderMode=4;
 assert(offline->queueSettings(s,901));
 offline->consumeRealtimeEdits();
 assert(offline->pollCompletion(ack)&&ack.applied&&ack.requestId==901);
 assert(offline->acceptedSettings().renderMode==4);
 assert(offline->stateSnapshotQuiescent());
 // Old v4 SpeakerPhysical and music-LF480 are REJECTED acoustically.
 // When migrated into a v6 research binary, they explicitly fall back P8.2.
 // Backward-compatible v5 P8.4 session must keep exact mode 3.
 {
  const auto v5=legacy(5,3);
  auto p84=std::make_unique<SonKuSpatialProcessor>();
  p84->prepareToPlay(48000.,128);
  assert(p84->restoreUnpublishedState(v5.getData(),static_cast<int>(v5.getSize())));
  assert(p84->acceptedSettings().renderMode==3);
 }
 for(int version:{1,2,3,4}){
  const auto old=legacy(version);
  auto dest=std::make_unique<SonKuSpatialProcessor>();
  dest->prepareToPlay(48000.,128);
  assert(dest->restoreUnpublishedState(old.getData(),static_cast<int>(old.getSize())));
  assert(dest->acceptedSettings().renderMode==0);
  if(version==4){
   assert(!dest->acceptedSettings().musicCompatibleLowFrequency);
   assert(dest->acceptedSettings().headYawDegrees==0.f);
  }
 }
 // Even legacy v4 MusicBed with a stale LF480 toggle must be sanitized
 // so the new v6 research UI always discovers truthful P8.4 capability.
 const auto v4GoldenWithLf=legacy(4,0);
 auto downgrade=std::make_unique<SonKuSpatialProcessor>();
 downgrade->prepareToPlay(48000.,128);
 assert(downgrade->restoreUnpublishedState(
    v4GoldenWithLf.getData(),static_cast<int>(v4GoldenWithLf.getSize())));
 assert(downgrade->acceptedSettings().renderMode==0);
 assert(!downgrade->acceptedSettings().musicCompatibleLowFrequency);
 auto broken=std::make_unique<SonKuSpatialProcessor>();
 broken->prepareToPlay(48000.,128);
 assert(!broken->restoreUnpublishedState(snapshot.getData(),58));
 juce::MemoryBlock corrupt(snapshot);
 auto* ptr=static_cast<std::uint8_t*>(corrupt.getData());
 ptr[54]=0xff; // signed JUCE bool, 0xff must never become true
 assert(!broken->restoreUnpublishedState(corrupt.getData(),59));
 corrupt=snapshot;ptr=static_cast<std::uint8_t*>(corrupt.getData());
 ptr[50]=2;ptr[51]=ptr[52]=ptr[53]=0; // old physical mode in v6 forbidden
 assert(!broken->restoreUnpublishedState(corrupt.getData(),59));
 restored->sealForRealtime();
 assert(!restored->restoreUnpublishedState(snapshot.getData(),59));
 std::printf("PASS P85 JUCE v6: audio ACK fade, single native graph A/B, "
   "safe v1-v4 migration, v6 59-byte state and device reopen; "
   "P85_vs_P82_RMS_dB=%.3f; ASIO/headphone NOT PROVEN\n",relativeDb);
}
