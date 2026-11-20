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
  GraphicEq31Processor geq;
  ChannelCompressorProcessor comp;
  ChannelHarmonyProcessor harmony;
  VocalThrowProcessor vthrow;
  juce::AudioProcessor* processors[]={&eq,&geq,&comp,&harmony,&vthrow};
  const char* catalogs[]={ChannelEqProcessor::catalogIdentity,
      GraphicEq31Processor::catalogIdentity,
      ChannelCompressorProcessor::catalogIdentity,
      ChannelHarmonyProcessor::catalogIdentity,
      VocalThrowProcessor::catalogIdentity};
  for(auto* p:processors) {
    REQUIRE(!BuiltInFxAcceptedSnapshot::acceptedSettings(p,
         "armix.builtin.arson-harmony.v1").isObject());
    p->prepareToPlay(48000.,128);
  }
  for(int i=0;i<5;++i) {
    auto* p=processors[i];
    const juce::String catalog(catalogs[i]);
    REQUIRE(BuiltInFxAcceptedSnapshot::matchesReady(p,catalog));
    for(int j=0;j<5;++j)
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
    for(int j=0;j<5;++j)if(i!=j) {
      BuiltInFxUserPreset wrong;
      REQUIRE(store.read(catalogs[j],first.id,wrong).isNotEmpty());
    }
  }
  // Actual JUCE APVTS -> processBlock test while the gesture is STILL OPEN.
  // A wheel tick or a drag update both call setValueNotifyingHost before
  // endChangeGesture; neither path is allowed to defer sound until mouseUp.
  {
    juce::AudioBuffer<float> pcm(2,128);
    juce::MidiBuffer midi;
    const auto fillTone=[&] {
      for(int n=0;n<128;++n) {
        const auto sample=.04f*static_cast<float>(
            std::sin(6.2831853071795864769*1000.*n/48000.));
        pcm.setSample(0,n,sample);
        pcm.setSample(1,n,-sample);
      }
    };
    fillTone();
    geq.processBlock(pcm,midi); // flat/unity reference
    const auto dry=pcm.getSample(0,47);
    auto* band=dynamic_cast<juce::RangedAudioParameter*>(
        geq.parameters().getParameter("band17"));
    REQUIRE(band!=nullptr);
    band->beginChangeGesture();
    band->setValueNotifyingHost(band->convertTo0to1(12.f));
    REQUIRE(geq.acceptedBands()[17]>11.9f);
    // No endChangeGesture/mouseUp yet: sound MUST respond in this block.
    fillTone();
    geq.processBlock(pcm,midi);
    REQUIRE(std::fabs(pcm.getSample(0,47)-dry)>0.00001f);
    REQUIRE(std::fabs(pcm.getSample(0,47)+pcm.getSample(1,47))<1.e-6f);
    band->endChangeGesture();

    auto* output=dynamic_cast<juce::RangedAudioParameter*>(
        geq.parameters().getParameter("levelDb"));
    REQUIRE(output!=nullptr);
    output->beginChangeGesture();
    output->setValueNotifyingHost(output->convertTo0to1(-24.f));
    REQUIRE(geq.acceptedLevelDb()<-23.9f);
    float peakBefore=0.f,peakAfter=0.f;
    fillTone();
    geq.processBlock(pcm,midi);
    for(int n=0;n<128;++n)peakBefore=std::max(peakBefore,std::fabs(pcm.getSample(0,n)));
    for(int block=0;block<8;++block) {
      fillTone();
      geq.processBlock(pcm,midi);
    }
    for(int n=0;n<128;++n)peakAfter=std::max(peakAfter,std::fabs(pcm.getSample(0,n)));
    REQUIRE(peakAfter<peakBefore*.5f);
    output->endChangeGesture();
    REQUIRE(geq.getLatencySamples()==0);
  }
  const auto eqSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&eq,catalogs[0]);
  REQUIRE(eqSettings.getProperty("bands",{}).isArray());
  REQUIRE(eqSettings.getProperty("bands",{}).getArray()->size()==8);
  const auto geqSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&geq,catalogs[1]);
  REQUIRE(geqSettings.isObject());
  REQUIRE(geqSettings.getProperty("bands",{}).isArray());
  REQUIRE(geqSettings.getProperty("bands",{}).getArray()->size()==31);
  REQUIRE(!geqSettings.hasProperty("inputDb"));
  REQUIRE(!geqSettings.hasProperty("outputDb"));
  const auto compSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&comp,catalogs[2]);
  REQUIRE(compSettings.getProperty("makeupDb",{}).isDouble()||
          compSettings.getProperty("makeupDb",{}).isInt());
  const auto harmonySettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&harmony,catalogs[3]);
  REQUIRE(!harmonySettings.hasProperty("guideChannelId"));
  REQUIRE(!harmonySettings.hasProperty("guideMode"));
  REQUIRE(harmonySettings.hasProperty("voiceMode"));
  const auto throwSettings=BuiltInFxAcceptedSnapshot::acceptedSettings(&vthrow,catalogs[4]);
  REQUIRE(!throwSettings.hasProperty("musicSourceChannelId"));
  REQUIRE(!throwSettings.hasProperty("throwCount"));
  for(auto* p:processors)p->releaseResources();
  for(int i=0;i<5;++i)
    REQUIRE(!BuiltInFxAcceptedSnapshot::matchesReady(processors[i],catalogs[i]));
  std::cout<<"Five built-in FX: accepted DSP snapshot -> typed .armixfx -> "
           <<"cold reopen, wrong-catalog and source exclusion PASS\n";
  return 0;
 }catch(const std::exception& e) {
  std::cerr<<e.what()<<'\n';
  return 1;
 }
}
