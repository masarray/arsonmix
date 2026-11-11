// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BuiltInFxAcceptedSnapshot.h"
#include <stdexcept>
#include <iostream>
#include <string>

#define REQUIRE(x) do {if(!(x))throw std::runtime_error( \
  std::string("FX_ACCEPTED_SNAPSHOT_REJECTED: ")+ #x +" line "+ \
  std::to_string(__LINE__));}while(false)

int main(){
 try {
  using namespace armix::builtin_fx;
  using namespace armix::storage;
  const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("ArSonMix-Cross-Fx-Preset-"+juce::Uuid().toString());
  struct Cleanup {
    juce::File path;
    ~Cleanup(){path.deleteRecursively();}
  } cleanup{root};
  BuiltInFxUserPresetStore store(root);
  ChannelEqProcessor eq;
  ChannelCompressorProcessor comp;
  ChannelHarmonyProcessor harmony;
  VocalThrowProcessor vthrow;
  juce::AudioProcessor* processors[]={&eq,&comp,&harmony,&vthrow};
  const char* catalogs[]={ChannelEqProcessor::catalogIdentity,
      ChannelCompressorProcessor::catalogIdentity,
      ChannelHarmonyProcessor::catalogIdentity,
      VocalThrowProcessor::catalogIdentity};
  for(auto* p:processors) {
    REQUIRE(!BuiltInFxAcceptedSnapshot::acceptedSettings(p,
         "armix.builtin.arson-harmony.v1").isObject());
    p->prepareToPlay(48000.,128);
  }
  for(int i=0;i<4;++i) {
    auto* p=processors[i];
    const juce::String catalog(catalogs[i]);
    REQUIRE(BuiltInFxAcceptedSnapshot::matchesReady(p,catalog));
    for(int j=0;j<4;++j)
      if(i!=j)REQUIRE(!BuiltInFxAcceptedSnapshot::matchesReady(p,catalogs[j]));
    const auto captured=BuiltInFxAcceptedSnapshot::acceptedSettings(p,catalog);
    REQUIRE(BuiltInFxUserPresetStore::validateSettings(catalog,captured));
    BuiltInFxUserPreset first;
    first.name="Native Captured";
    first.category="My Presets";
    first.catalogId=catalog;
    first.settings=captured;
    REQUIRE(store.saveNew(first).isEmpty());
    REQUIRE(BuiltInFxUserPresetStore::safeId(first.id));
    BuiltInFxUserPreset second;
    REQUIRE(BuiltInFxUserPresetStore(root).read(catalog,first.id,second).isEmpty());
    REQUIRE(juce::JSON::toString(second.settings,false)==
            juce::JSON::toString(captured,false));
    for(int j=0;j<4;++j)if(i!=j) {
      BuiltInFxUserPreset wrong;
      REQUIRE(store.read(catalogs[j],first.id,wrong).isNotEmpty());
    }
  }
  const auto eqSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&eq,catalogs[0]);
  REQUIRE(eqSettings.getProperty("bands",{}).isArray());
  REQUIRE(eqSettings.getProperty("bands",{}).getArray()->size()==8);
  const auto compSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&comp,catalogs[1]);
  REQUIRE(compSettings.getProperty("makeupDb",{}).isDouble()||
          compSettings.getProperty("makeupDb",{}).isInt());
  const auto harmonySettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&harmony,catalogs[2]);
  REQUIRE(!harmonySettings.hasProperty("guideChannelId"));
  REQUIRE(!harmonySettings.hasProperty("guideMode"));
  REQUIRE(harmonySettings.hasProperty("voiceMode"));
  const auto throwSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&vthrow,catalogs[3]);
  REQUIRE(!throwSettings.hasProperty("musicSourceChannelId"));
  REQUIRE(!throwSettings.hasProperty("throwCount"));
  for(auto* p:processors)p->releaseResources();
  for(int i=0;i<4;++i)
    REQUIRE(!BuiltInFxAcceptedSnapshot::matchesReady(processors[i],catalogs[i]));
  std::cout<<"Four built-in FX: accepted DSP snapshot -> typed .armixfx -> "
           <<"cold reopen, wrong-catalog and source exclusion PASS\n";
  return 0;
 }catch(const std::exception& e) {
  std::cerr<<e.what()<<'\n';
  return 1;
 }
}
