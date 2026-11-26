// SPDX-License-Identifier: AGPL-3.0-only
// REAL native Windows JUCE v10 SonKu P9.0 true external speaker headphone research:
// P8.2 -> native queued v10 -> fading -> AUDIO APPLIED ACK -> readback,
// v1..v10 .smix state, old v4 rejected physical rollback, v9 HRTF trim intact and no extra graph.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT) || !ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT
#error Native JUCE v11 test requires explicit private P9 research build
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
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
// Real JUCE class-substitution contract: the other product can inherit
// the *same* native renderer while rejecting SonKu-only motion/voicing
// commands. It must not inherit SonKu's screenshot factory default.
class RestrictedBrandedInsert final : public SonKuSpatialProcessor {
public:
 RestrictedBrandedInsert() : SonKuSpatialProcessor(initial()) {}
 static Settings initial() {
  auto s=armix::sonku::p99::DefaultOrbitStage::settings();
  s.moving=false; // Branded studio/venue editor has static source
  return s;
 }
 bool adopt(const Settings& next) noexcept {
  return adoptUnpublishedScene(next);
 }
protected:
 bool admitQueuedScene(const Settings& next) const noexcept override {
  return next.renderMode==9 && !next.moving;
 }
};
#endif
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
 assert(ok && "P90 must ACK only after native PCM applies settings");
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
   o.writeInt(oldMode);o.writeByte(version>=5?0:1);o.writeFloat(0.f);
  }
  if(version>=9)o.writeFloat(-12.f);
 } // MemoryOutputStream must flush byte count before asserting the block.
 assert(bytes.getSize()==static_cast<std::size_t>(
     version==1?30:version==2?46:version==3?50:version>=9?63:59));
 return bytes;
}
int main(){
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
 static_assert(schemaVersion()==12);
 constexpr int activeVersion=12,activeBytes=67;
#else
 static_assert(schemaVersion()==11);
 constexpr int activeVersion=11,activeBytes=63;
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
 // The companion product's own state and DSP identity survive co-build.
 // This compiles exactly the protected extension contract used by ImmerSIB
 // without importing or overwriting any file from its independent PR.
 {
  auto product=std::make_unique<RestrictedBrandedInsert>();
  const auto initial=RestrictedBrandedInsert::initial();
  assert(!initial.moving && initial.renderMode==9 && initial.sceneVoicing==1);
  assert(product->acceptedSettings().moving==false);
  assert(product->acceptedSettings().sourceDistanceMeters==2.7f);
  product->prepareToPlay(48000.,128);
  assert(product->isReady());
  Settings forbidden=initial;forbidden.moving=true;
  assert(!product->queueSettings(forbidden,1801));
  Settings legal=initial;legal.sourceDistanceMeters=3.0f;legal.spatialMix=.42f;
  assert(product->queueSettings(legal,1802));
  SonKuSpatialProcessor::Completion ack{};
  assert(!product->pollCompletion(ack));
  product->consumeRealtimeEdits();
  assert(product->pollCompletion(ack) && ack.applied && ack.requestId==1802);
  assert(product->acceptedSettings().sourceDistanceMeters==3.0f);
  assert(product->acceptedSettings().spatialMix==.42f);
  assert(product->stateSnapshotQuiescent());
  legal.sourceDistanceMeters=2.4f;
  assert(product->adopt(legal));
  assert(product->acceptedSettings().sourceDistanceMeters==2.4f);
  product->sealForRealtime();
  assert(!product->adopt(initial)); // no published state overwrite
 }
#endif
 auto fx=std::make_unique<SonKuSpatialProcessor>();
 fx->prepareToPlay(48000.,128);
 assert(fx->isReady());
 assert(fx->preparedReflectionProfile()==
     armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch);
 fx->sealForRealtime();
 Settings s=fx->acceptedSettings();
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
 // Operator screenshot is the default for NEW inserts only. Source of
 // truth: DefaultOrbitStage::settings(); no hidden migration of old .smix.
 assert(s.renderMode==9 && s.sceneVoicing==1 && s.moving && s.clockwise);
 assert(s.startAzimuthDegrees==0.f && s.revolutionSeconds==12.f);
 assert(s.stereoSpreadDegrees==127.f && s.spatialMix==1.f);
 assert(s.outputTrim==1.8f && s.spacePreset==3);
 assert(s.sourceDistanceMeters==2.7f && s.roomAmount==1.f);
 assert(s.ambience==.77f);
 assert(s.headYawDegrees==0.f && s.hrtfElevationTrimDegrees==0.f);
 // Default v12 memory snapshot must round-trip without selecting a
 // preset manually and must not alter operator sessions restored below.
 juce::MemoryBlock brandNewState;
 fx->getStateInformation(brandNewState);
 assert(brandNewState.getSize()==67);
 auto brandNewRestored=std::make_unique<SonKuSpatialProcessor>();
 brandNewRestored->prepareToPlay(48000.,128);
 assert(brandNewRestored->restoreUnpublishedState(
     brandNewState.getData(),static_cast<int>(brandNewState.getSize())));
 const auto fresh=brandNewRestored->acceptedSettings();
 assert(fresh.renderMode==s.renderMode && fresh.sceneVoicing==s.sceneVoicing);
 assert(fresh.moving==s.moving && fresh.clockwise==s.clockwise);
 assert(fresh.revolutionSeconds==s.revolutionSeconds &&
        fresh.stereoSpreadDegrees==s.stereoSpreadDegrees);
 assert(fresh.outputTrim==s.outputTrim && fresh.spacePreset==s.spacePreset);
 assert(fresh.sourceDistanceMeters==s.sourceDistanceMeters &&
        fresh.roomAmount==s.roomAmount && fresh.ambience==s.ambience);
#else
 assert(s.renderMode==0);
#endif
 s.moving=false;s.startAzimuthDegrees=0.f;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.spacePreset=1;s.sourceDistanceMeters=1.6f;s.roomAmount=.55f;s.ambience=.3f;
 s.headYawDegrees=0.f;s.musicCompatibleLowFrequency=false;s.renderMode=9;s.hrtfElevationTrimDegrees=-12.f;
 auto json=acceptedSettings(s);
 Settings decoded{};
 assert(decodeSettings(json,decoded)&&decoded.renderMode==9);
 for(int rejected:{-1,1,2,10,63}){
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
 SonKuSpatialProcessor::Completion ack{};
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
 // A brand-new P9.9 insert intentionally STARTS in mode 9 (Orbit).
 // Parameter-only mode 9 -> mode 9 edits must ACK after the first
 // render callback, WITHOUT an unnecessary mode-fade stall.
 assert(fx->acceptedSettings().renderMode==9);
 assert(fx->queueSettings(s,800));
 assert(!fx->pollCompletion(ack));
 render(*fx,0);
 assert(fx->pollCompletion(ack) && ack.requestId==800 && ack.applied);
 assert(fx->acceptedSettings().renderMode==9);
 assert(fx->acceptedSettings().sourceDistanceMeters==1.6f);
 assert(fx->stateSnapshotQuiescent());
 // The old 801 assertion checks a DIFFERENT contract: switching from
 // an existing legacy engine mode into 9 MUST fade and ACK only when
 // PCM has applied the new mode. Explicitly establish that premise.
 auto oldMode=s;
 oldMode.renderMode=0;
 oldMode.sceneVoicing=0;
 assert(fx->queueSettings(oldMode,799));
 complete(*fx,799,15);
 assert(fx->acceptedSettings().renderMode==0);
 assert(fx->acceptedSettings().sceneVoicing==0);
 assert(fx->stateSnapshotQuiescent());
#endif
 assert(fx->queueSettings(s,801));
 assert(!fx->pollCompletion(ack));
 render(*fx,0);
 assert(!fx->pollCompletion(ack));
 assert(!fx->stateSnapshotQuiescent());
 // No prematurely published settings during a mode-switch fade. This
 // is mode-independent; P9.9 now explicitly exercises both ACK paths.
 assert(fx->acceptedSettings().renderMode==0);
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
 assert(fx->acceptedSettings().sceneVoicing==0);
 assert(fx->acceptedSettings().sourceDistanceMeters==1.6f);
#endif
 complete(*fx,801,1);
 assert(fx->acceptedSettings().renderMode==9);
 juce::MemoryBlock snapshot;
 fx->getStateInformation(snapshot);
 assert(snapshot.getSize()==static_cast<std::size_t>(activeBytes));
 juce::MemoryInputStream input(snapshot.getData(),snapshot.getSize(),false);
 assert(input.readInt()==0x534f4e4b&&input.readInt()==activeVersion);
 auto restored=std::make_unique<SonKuSpatialProcessor>();
 restored->prepareToPlay(44100.,128);
 assert(restored->restoreUnpublishedState(
     snapshot.getData(),static_cast<int>(snapshot.getSize())));
 assert(restored->acceptedSettings().renderMode==9);
  assert(restored->acceptedSettings().hrtfElevationTrimDegrees==-12.f);
 restored->prepareToPlay(48000.,256);
 assert(restored->isReady() && restored->acceptedSettings().renderMode==9);
 // Actual direct FIR must distinguish P8.4 from P8.2 without double engine.
 auto golden=std::make_unique<SonKuSpatialProcessor>();
 golden->prepareToPlay(48000.,128);
 auto g=s;g.renderMode=0;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
 g.sceneVoicing=0; // v12 never permits voiced legacy mode 0.
#endif
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
 assert(std::abs(relativeDb)<8.0);
 // Audio-thread head-yaw change is a separate accepted v10 transaction.
 s.headYawDegrees=60.f;
 s.spatialMix=.3f;
 assert(fx->queueSettings(s,802));
 assert(!fx->pollCompletion(ack));
 assert(render(*fx,400*128)>0.);
 assert(fx->pollCompletion(ack)&&ack.applied&&ack.requestId==802);
 assert(ack.settings.spatialMix==1.f&&fx->acceptedSettings().spatialMix==1.f);
 assert(fx->stateSnapshotQuiescent());
 s.spatialMix=1.f;
 s.headYawDegrees=-45.f;
 assert(fx->queueSettings(s,804));
 fx->consumeRealtimeEdits();
 assert(fx->pollCompletion(ack)&&ack.applied&&ack.requestId==804);
 assert(ack.settings.headYawDegrees==-45.f);
 s.headYawDegrees=60.f;
 assert(fx->queueSettings(s,805));
 complete(*fx,805,420);
 assert(fx->acceptedSettings().renderMode==9);
 assert(fx->acceptedSettings().headYawDegrees==60.f);
 s.renderMode=0;s.headYawDegrees=0.f;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
 s.sceneVoicing=0; // A disabled world speaker cannot retain named voicing.
#endif
 assert(fx->queueSettings(s,803));
 complete(*fx,803,480);
 assert(fx->acceptedSettings().renderMode==0);
 // Unpublished workspace has no PCM clock: edits ACK without fade.
 auto offline=std::make_unique<SonKuSpatialProcessor>();
 offline->prepareToPlay(48000.,128);
 s.renderMode=9;
 assert(offline->queueSettings(s,901));
 offline->consumeRealtimeEdits();
 assert(offline->pollCompletion(ack)&&ack.applied&&ack.requestId==901);
 assert(offline->acceptedSettings().renderMode==9);
 assert(offline->stateSnapshotQuiescent());
 // Old v4 SpeakerPhysical and music-LF480 are REJECTED acoustically.
 // When migrated into a v10 research binary, they explicitly fall back P8.2.
 // Backward-compatible v5 P8.4 session must keep exact mode 3.
 {
  const auto v5=legacy(5,3);
  auto p84=std::make_unique<SonKuSpatialProcessor>();
  p84->prepareToPlay(48000.,128);
  assert(p84->restoreUnpublishedState(v5.getData(),static_cast<int>(v5.getSize())));
  assert(p84->acceptedSettings().renderMode==3);
 }
 // v6 P8.5 world-coherence sessions restore unchanged; never rewritten as focus.
 {
  const auto v6=legacy(6,4);
  auto p85=std::make_unique<SonKuSpatialProcessor>();
  p85->prepareToPlay(48000.,128);
  assert(p85->restoreUnpublishedState(v6.getData(),static_cast<int>(v6.getSize())));
  assert(p85->acceptedSettings().renderMode==4);
 }
 // v7 P8.6 focused music session must retain render mode 5, not 6.
 {
  const auto v7=legacy(7,5);
  auto p86=std::make_unique<SonKuSpatialProcessor>();
  p86->prepareToPlay(48000.,128);
  assert(p86->restoreUnpublishedState(v7.getData(),static_cast<int>(v7.getSize())));
  assert(p86->acceptedSettings().renderMode==5);
 }
 // v8 P8.7 session must preserve mode 6, and no HRTF trim is invented.
 {
  const auto v8=legacy(8,6);
  auto p87=std::make_unique<SonKuSpatialProcessor>();
  p87->prepareToPlay(48000.,128);
  assert(p87->restoreUnpublishedState(v8.getData(),static_cast<int>(v8.getSize())));
  assert(p87->acceptedSettings().renderMode==6);
  assert(p87->acceptedSettings().hrtfElevationTrimDegrees==0.f);
 }
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
 // Old operator-approved v11 Venue has no voicing: restore exactly neutral.
 {
  const auto oldVenue=legacy(11,9);
  auto v11=std::make_unique<SonKuSpatialProcessor>();
  v11->prepareToPlay(48000.,128);
  assert(v11->restoreUnpublishedState(oldVenue.getData(),63));
  assert(v11->acceptedSettings().renderMode==9);
  assert(v11->acceptedSettings().sceneVoicing==0);
 }
 // P9.8 round-trip and negative validation of native scene identity.
 {
  Settings voiced=s;
  voiced.renderMode=9;voiced.sceneVoicing=1;
  auto accepted=acceptedSettings(voiced);
  Settings decodedScene{};
  assert(decodeSettings(accepted,decodedScene));
  assert(decodedScene.sceneVoicing==1);
  for(const int invalid : {-1,4,127}){
   accepted.getDynamicObject()->setProperty("sceneVoicing",invalid);
   assert(!decodeSettings(accepted,decodedScene));
  }
  accepted.getDynamicObject()->setProperty("sceneVoicing",juce::String("1"));
  assert(!decodeSettings(accepted,decodedScene));
  accepted=acceptedSettings(voiced);
  auto v12=std::make_unique<SonKuSpatialProcessor>();
  v12->prepareToPlay(48000.,128);
  assert(v12->queueSettings(voiced,977));
  v12->consumeRealtimeEdits();
  SonKuSpatialProcessor::Completion done{};
  assert(v12->pollCompletion(done)&&done.applied&&done.requestId==977);
  assert(done.settings.sceneVoicing==1);
  juce::MemoryBlock saved;
  v12->getStateInformation(saved);
  assert(saved.getSize()==67);
  auto reopened=std::make_unique<SonKuSpatialProcessor>();
  reopened->prepareToPlay(44100.,128);
  assert(reopened->restoreUnpublishedState(saved.getData(),67));
  assert(reopened->acceptedSettings().sceneVoicing==1);
  assert(reopened->acceptedSettings().renderMode==9);
  // All native admission routes must reject the old P8.3 physical
  // modes, even if a v12 blob declares neutral sceneVoicing=0.
  for(int legacyMode : {1,2}){
   auto rejected=voiced;
   rejected.sceneVoicing=0;
   rejected.renderMode=legacyMode;
   assert(!armix::sonku::valid(rejected));
   assert(!v12->queueSettings(rejected,979));
   auto data=acceptedSettings(rejected);
   assert(!decodeSettings(data,decodedScene));
   // v12 binary is append-only: the render-mode field retains v11
   // offset 50; force that field without touching sceneVoicing=0.
   auto corrupted=saved;
   auto* b=static_cast<std::uint8_t*>(corrupted.getData());
   b[50]=static_cast<std::uint8_t>(legacyMode);
   b[51]=b[52]=b[53]=0;
   b[63]=b[64]=b[65]=b[66]=0;
   assert(!reopened->restoreUnpublishedState(corrupted.getData(),67));
  }
  // v12 corrupt ID cannot bypass valid() or restore a partial blob.
  auto invalidData=saved;
  auto* bytes=static_cast<std::uint8_t*>(invalidData.getData());
  bytes[63]=0xff;bytes[64]=bytes[65]=bytes[66]=0;
  assert(!reopened->restoreUnpublishedState(invalidData.getData(),67));
  assert(!reopened->restoreUnpublishedState(saved.getData(),66));
 }
#endif
 // Existing v10 P8.9 session must remain byte-compatible and mode 8.
 {
  const auto v10=legacy(10,8);
  auto p89=std::make_unique<SonKuSpatialProcessor>();
  p89->prepareToPlay(48000.,128);
  assert(p89->restoreUnpublishedState(v10.getData(),static_cast<int>(v10.getSize())));
  assert(p89->acceptedSettings().renderMode==8);
  assert(p89->acceptedSettings().hrtfElevationTrimDegrees==-12.f);
 }
 // Exact v9 P8.8 session must retain mode 7 and HRTF trim -12°.
 {
  const auto v9=legacy(9,7);
  auto p88=std::make_unique<SonKuSpatialProcessor>();
  p88->prepareToPlay(48000.,128);
  assert(p88->restoreUnpublishedState(v9.getData(),static_cast<int>(v9.getSize())));
  assert(p88->acceptedSettings().renderMode==7);
  assert(p88->acceptedSettings().hrtfElevationTrimDegrees==-12.f);
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
 // so the new v10 research UI always discovers truthful P8.4 capability.
 const auto v4GoldenWithLf=legacy(4,0);
 auto downgrade=std::make_unique<SonKuSpatialProcessor>();
 downgrade->prepareToPlay(48000.,128);
 assert(downgrade->restoreUnpublishedState(
    v4GoldenWithLf.getData(),static_cast<int>(v4GoldenWithLf.getSize())));
 assert(downgrade->acceptedSettings().renderMode==0);
 assert(!downgrade->acceptedSettings().musicCompatibleLowFrequency);
 auto broken=std::make_unique<SonKuSpatialProcessor>();
 broken->prepareToPlay(48000.,128);
 assert(!broken->restoreUnpublishedState(snapshot.getData(),activeBytes-1));
 juce::MemoryBlock corrupt(snapshot);
 auto* ptr=static_cast<std::uint8_t*>(corrupt.getData());
 ptr[54]=0xff; // signed JUCE bool, 0xff must never become true
 assert(!broken->restoreUnpublishedState(corrupt.getData(),activeBytes));
 corrupt=snapshot;ptr=static_cast<std::uint8_t*>(corrupt.getData());
 ptr[50]=2;ptr[51]=ptr[52]=ptr[53]=0; // old physical mode in v10 forbidden
 assert(!broken->restoreUnpublishedState(corrupt.getData(),activeBytes));
 restored->sealForRealtime();
 assert(!restored->restoreUnpublishedState(snapshot.getData(),activeBytes));
 std::printf("PASS P90 JUCE v11: audio ACK fade, single native graph A/B, "
   "safe v1-v11 migration, opt-in v12 state and device reopen; "
   "P90_vs_P82_RMS_dB=%.3f; ASIO/headphone NOT PROVEN\n",relativeDb);
}
