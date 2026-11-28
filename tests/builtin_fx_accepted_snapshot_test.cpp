// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BuiltInFxAcceptedSnapshot.h"
#include <stdexcept>
#include <iostream>
#include <string>
#include <limits>

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
  DeEzzerProcessor dez;
  AntiNoizProcessor an;
  juce::AudioProcessor* processors[]={&eq,&geq,&comp,&harmony,&vthrow,&dez,&an};
  const char* catalogs[]={ChannelEqProcessor::catalogIdentity,
      GraphicEq31Processor::catalogIdentity,
      ChannelCompressorProcessor::catalogIdentity,
      ChannelHarmonyProcessor::catalogIdentity,
      VocalThrowProcessor::catalogIdentity,DeEzzerProcessor::catalogIdentity,AntiNoizProcessor::catalogIdentity};
  for(auto* p:processors) {
    REQUIRE(!BuiltInFxAcceptedSnapshot::acceptedSettings(p,
         "armix.builtin.arson-harmony.v1").isObject());
    p->prepareToPlay(48000.,128);
  }
  for(int i=0;i<7;++i) {
    auto* p=processors[i];
    const juce::String catalog(catalogs[i]);
    REQUIRE(BuiltInFxAcceptedSnapshot::matchesReady(p,catalog));
    for(int j=0;j<7;++j)
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
    for(int j=0;j<7;++j)if(i!=j) {
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
    // Final post-EQ Output boost must not reintroduce Inf after the core
    // sanitized PCM. Normal, valid PCM above unity is never limited.
    std::array<float,GraphicEq31Core::bandCount> flatBands{};
    REQUIRE(geq.setBandsFromPreset(flatBands,6.f));
    for(int block=0;block<10;++block) {
      fillTone();
      geq.processBlock(pcm,midi); // settle the 15 ms Output gain ramp
    }
    pcm.clear();
    pcm.setSample(0,0,std::numeric_limits<float>::max());
    pcm.setSample(1,0,-std::numeric_limits<float>::max());
    pcm.setSample(0,7,std::numeric_limits<float>::quiet_NaN());
    pcm.setSample(1,11,std::numeric_limits<float>::infinity());
    geq.processBlock(pcm,midi);
    for(int n=0;n<pcm.getNumSamples();++n) {
      REQUIRE(std::isfinite(pcm.getSample(0,n)));
      REQUIRE(std::isfinite(pcm.getSample(1,n)));
    }
  }
  // Atomic preset/factory/Flat contract: one valid 31+Output batch, never
  // a partially committed corrupt preset. Flat must retain Output Level.
  {
    std::array<float,GraphicEq31Core::bandCount> preset{};
    for(std::size_t i=0;i<preset.size();++i)
      preset[i]=i%2?6.f:-6.f;
    REQUIRE(geq.setBandsFromPreset(preset,3.f));
    REQUIRE(geq.stateSnapshotQuiescent());
    const auto bands=geq.acceptedBands();
    for(std::size_t i=0;i<bands.size();++i)
      REQUIRE(std::fabs(bands[i]-preset[i])<.06f);
    REQUIRE(std::fabs(geq.acceptedLevelDb()-3.f)<.06f);
    auto corrupt=preset;
    corrupt[20]=std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!geq.setBandsFromPreset(corrupt,-1.f));
    REQUIRE(geq.acceptedBands()==bands);
    REQUIRE(std::fabs(geq.acceptedLevelDb()-3.f)<.06f);
    geq.setFlatFromUi();
    for(const float gain:geq.acceptedBands())REQUIRE(std::fabs(gain)<.06f);
    REQUIRE(std::fabs(geq.acceptedLevelDb()-3.f)<.06f);
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
  {
    namespace dz=armix::builtin_fx::de_ezzer;
    const auto before=dez.readback();
    auto settings=before.settings;settings.amount=.42f;settings.enabled=false;
    REQUIRE(dez.queue({before.id,1,settings,dz::Audition::Normal}));
    REQUIRE(!BuiltInFxAcceptedSnapshot::acceptedSettings(&dez,catalogs[5]).isObject());
    juce::AudioBuffer<float> pcm(2,128);pcm.clear();juce::MidiBuffer midi;
    dez.processBlock(pcm,midi);
    REQUIRE(!BuiltInFxAcceptedSnapshot::acceptedSettings(&dez,catalogs[5]).isObject());
    dz::Ack ack;REQUIRE(dez.pollAck(ack)&&ack.code==dz::AckCode::Applied);
    const auto accepted=BuiltInFxAcceptedSnapshot::acceptedSettings(&dez,catalogs[5]);
    REQUIRE(BuiltInFxUserPresetStore::validateSettings(catalogs[5],accepted));
    REQUIRE(static_cast<float>(static_cast<double>(accepted.getProperty("amount",{})))==settings.amount);
    REQUIRE(!accepted.hasProperty("enabled")&&!accepted.hasProperty("audition"));
  }
  // Real accepted DSP -> portable preset, not a submitted GUI draft.
  auto settings=an.acceptedSettings();
  settings.mode=AntiNoizProcessor::Core::Mode::Singing;
  settings.cleanDb=9.5f;settings.silenceDb=21.f;settings.musicProtect=.75f;
  settings.spectralManual=true;settings.manual.enabled=true;settings.manual.smartVoice=true;settings.manual.rangeDb=96;settings.artifactControl=.72f;settings.manual.smartMarginDb=7;
  REQUIRE(an.queueSettings(settings,1));
  REQUIRE(!an.stateSnapshotQuiescent());
  const auto before=BuiltInFxAcceptedSnapshot::acceptedSettings(&an,catalogs[6]);
  REQUIRE(before.getProperty("mode",{}).toString()=="Auto");
  an.consumeRealtimeEdits(); // detached fixture has no callback publisher
  AntiNoizProcessor::Completion completed{};
  REQUIRE(an.pollCompletion(completed)&&completed.id==1&&completed.applied);
  REQUIRE(an.stateSnapshotQuiescent());
  const auto after=BuiltInFxAcceptedSnapshot::acceptedSettings(&an,catalogs[6]);
  REQUIRE(after.getProperty("mode",{}).toString()=="Singing");
  REQUIRE(static_cast<double>(after.getProperty("cleanDb",{}))==9.5);
  REQUIRE(!after.hasProperty("profile")&&!after.hasProperty("sourceRole")&&
          !after.hasProperty("deviceEpoch"));
  BuiltInFxUserPreset savedAn{{},catalogs[6],"Singing Live","My Presets",after};
  REQUIRE(store.saveNew(savedAn).isEmpty());
  BuiltInFxUserPreset coldAn;
  REQUIRE(BuiltInFxUserPresetStore(root).read(catalogs[6],savedAn.id,coldAn).isEmpty());
  AntiNoizProcessor::Core::Settings decoded{};
  REQUIRE(AntiNoizNativeBinding::parseSettings(coldAn.settings,decoded));
  REQUIRE(decoded.mode==settings.mode&&decoded.cleanDb==settings.cleanDb&&
          decoded.silenceDb==settings.silenceDb&&decoded.musicProtect==settings.musicProtect);
  using State=armix::builtin_fx::anti_noiz::NativeState;
  REQUIRE(State::encode(decoded)==State::encode(settings));
  // Saved mixer state is settings-only and is restored before publication.
  juce::MemoryBlock state;
  an.getStateInformation(state);
  AntiNoizProcessor restored;
  restored.prepareToPlay(96000.,128);
  REQUIRE(restored.restoreUnpublishedState(state.getData(),static_cast<int>(state.getSize())));
  REQUIRE(restored.getLatencySamples()==512&&restored.acceptedSettings().mode==settings.mode);
  REQUIRE(State::encode(restored.acceptedSettings())==State::encode(settings));
  REQUIRE(!restored.readObservation().profile&&!restored.readObservation().sidechainReference&&!restored.readObservation().audition);
  restored.releaseResources();
  for(auto* p:processors)p->releaseResources();
  for(int i=0;i<7;++i)
    REQUIRE(!BuiltInFxAcceptedSnapshot::matchesReady(processors[i],catalogs[i]));
  std::cout<<"Seven built-in FX: accepted DSP snapshot -> typed .armixfx -> "
           <<"cold reopen, wrong-catalog and source exclusion PASS\n";
  return 0;
 }catch(const std::exception& e) {
  std::cerr<<e.what()<<'\n';
  return 1;
 }
}
