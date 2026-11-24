// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BuiltInFxUserPresetStore.h"
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <iostream>
using armix::storage::BuiltInFxUserPreset;
using armix::storage::BuiltInFxUserPresetStore;

// Do not use <cassert>: Release/NDEBUG omits both assertions AND their
// mutating operations, producing a false-green Windows CTest.
#define REQUIRE(expr) do { if (!(expr)) throw std::runtime_error(\
    std::string("PRESET_TEST_FAILED: ") + #expr + " at line " +\
    std::to_string(__LINE__)); } while (false)


static juce::var object(std::initializer_list<std::pair<const char*,juce::var>> fields) {
    juce::var out(new juce::DynamicObject());
    for(const auto& item:fields)
        out.getDynamicObject()->setProperty(item.first,item.second);
    return out;
}
static BuiltInFxUserPreset comp() {
    return {{}, "armix.builtin.channel-compressor.v1", "Live Vocal", "Voice",
       object({{"thresholdDb",-18.0},{"ratio",3.0},{"attackMs",10.0},
          {"releaseMs",120.0},{"kneeDb",6.0},{"makeupDb",2.0},{"mix",1.0}})};
}
static BuiltInFxUserPreset harmony() {
    return {{}, "armix.builtin.arson-harmony.v1","My Trio","Harmony",
      object({{"key",0},{"scale",0},{"voices",3},{"voiceMode",0},
        {"correction",false},{"wet",0.45},{"space",0.6},
        {"color",0.5},{"gainDb",0.0}})};
}
static BuiltInFxUserPreset throwSettings() {
    return {{}, "armix.builtin.vocal-throw.v1","Last Word","Vocal",
      object({{"mode",2},{"bpm",120.0},{"division",0},{"repeats",3},
        {"captureMs",460.0},{"silenceMs",220.0},{"thresholdDb",-35.0},
        {"wet",0.42},{"decay",0.55},{"ducking",0.7}})};
}
static BuiltInFxUserPreset graphicEq31() {
    juce::var bands(juce::Array<juce::var>{});
    for(int i=0;i<31;++i)
        bands.append(i==0?12.0:i==30?-12.0:i==15?2.5:0.0);
    return {{}, "armix.builtin.graphic-eq31.v1","Voice Curve","My Presets",
        object({{"bands",bands},{"levelDb",-4.5}})};
}
static BuiltInFxUserPreset reverb() {
    juce::var values(juce::Array<juce::var>{});
    for(double v:{.137,32.,2.45,.89,.94,10400.,150.,15500.,.2,.28,1.15,.36,.18})
        values.append(v);
    return {{},"armix.builtin.vocal-reverb.v1","My Vocal Space","My Presets",
        object({{"algorithm",1},{"values",values}})};
}
static BuiltInFxUserPreset eq() {
    auto bands=juce::var(juce::Array<juce::var>{});
    for(int i=0;i<8;++i)
        bands.append(object({{"enabled",i==2},{"frequencyHz",i==2?2300.0:1000.0},
                  {"gainDb",i==2?2.5:0.0},{"q",1.0},{"slope",12}}));
    return {{}, "armix.builtin.channel-eq.v1","Vocal Presence","Voice",
        object({{"bands",bands}})};
}
int main() {
  try {
    const auto testRoot=juce::File::getSpecialLocation(
       juce::File::tempDirectory).getChildFile(
           "ArSonMix-Preset-CTest-"+juce::Uuid().toDashedString());
    REQUIRE(!testRoot.exists());
    struct Cleanup { juce::File folder;~Cleanup(){folder.deleteRecursively();} } cleanup{testRoot};
    BuiltInFxUserPresetStore store(testRoot);
    auto items=std::array{comp(),harmony(),throwSettings(),eq(),graphicEq31(),reverb()};
    for(auto& item:items) {
        REQUIRE(BuiltInFxUserPresetStore::validateSettings(item.catalogId,item.settings));
        REQUIRE(store.saveNew(item).isEmpty());
        REQUIRE(BuiltInFxUserPresetStore::safeId(item.id));
        BuiltInFxUserPreset readback;
        REQUIRE(store.read(item.catalogId,item.id,readback).isEmpty());
        REQUIRE(readback.name==item.name);
        REQUIRE(juce::JSON::toString(readback.settings,false)==
               juce::JSON::toString(item.settings,false));
        const BuiltInFxUserPresetStore reopened(testRoot);
        BuiltInFxUserPreset cold;
        REQUIRE(reopened.read(item.catalogId,item.id,cold).isEmpty());
        REQUIRE(cold.id==item.id);
        REQUIRE(reopened.enumerate(item.catalogId).size()==1);
        // Presets are effect-scoped rather than channel, slot or device-scoped.
        for(const auto& other:items)
          if(other.catalogId!=item.catalogId) {
             BuiltInFxUserPreset wrong;
             REQUIRE(reopened.read(other.catalogId,item.id,wrong).isNotEmpty());
          }
    }
    // Factory and user data must remain conceptually distinct; disk-only
    // user presets cannot silently be replaced by same category/name.
    auto duplicate=comp();
    REQUIRE(store.saveNew(duplicate).isNotEmpty());
    auto renamed=items[0];
    renamed.name="Natural Vocal";
    REQUIRE(store.overwrite(renamed).isEmpty());
    BuiltInFxUserPreset readback;
    REQUIRE(store.read(renamed.catalogId,renamed.id,readback).isEmpty());
    REQUIRE(readback.name=="Natural Vocal");
    REQUIRE(store.rename(renamed.catalogId,renamed.id,"Warm Vocal").isEmpty());
    REQUIRE(store.read(renamed.catalogId,renamed.id,readback).isEmpty());
    REQUIRE(readback.name=="Warm Vocal");
    REQUIRE(store.remove(renamed.catalogId,renamed.id).isEmpty());
    REQUIRE(store.read(renamed.catalogId,renamed.id,readback).isNotEmpty());
    // Exercise an actual overwrite of accepted settings; it must survive a
    // cold store re-open, independent of any mixer channel or session.
    auto tuned=items[2];
    tuned.settings.getDynamicObject()->setProperty("wet",0.83);
    REQUIRE(store.overwrite(tuned).isEmpty());
    BuiltInFxUserPreset coldTuned;
    REQUIRE(BuiltInFxUserPresetStore(testRoot).read(
        tuned.catalogId,tuned.id,coldTuned).isEmpty());
    REQUIRE(static_cast<double>(
        coldTuned.settings.getProperty("wet",{}))==0.83);
    // A new preset may not fake a factory or malformed unique identity.
    auto preassigned=comp();
    preassigned.id="../../unsafe";
    REQUIRE(store.saveNew(preassigned).isEmpty());
    REQUIRE(BuiltInFxUserPresetStore::safeId(preassigned.id));
    REQUIRE(store.remove(preassigned.catalogId,preassigned.id).isEmpty());
    // Nonportable channel routing, malformed/hostile values, unknown version
    // and cross-catalog data must all fail without damaging another preset.
    auto bad=harmony();
    bad.settings.getDynamicObject()->setProperty("guideChannelId","123456");
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(bad.catalogId,bad.settings));
    REQUIRE(store.saveNew(bad).isNotEmpty());
    bad=harmony();bad.settings.getDynamicObject()->setProperty("voices",7);
    REQUIRE(store.saveNew(bad).isNotEmpty());
    bad=throwSettings();bad.settings.getDynamicObject()->setProperty("bpm",-1);
    REQUIRE(store.saveNew(bad).isNotEmpty());
    bad=eq();bad.settings.getDynamicObject()->setProperty("liveInput",2);
    REQUIRE(store.saveNew(bad).isNotEmpty());
    auto album=reverb();album.settings.getDynamicObject()->setProperty("voicing",1);
    REQUIRE(BuiltInFxUserPresetStore::validateSettings(album.catalogId,album.settings));
    album.settings.getDynamicObject()->setProperty("voicing",2);
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(album.catalogId,album.settings));
    auto badReverb=reverb();
    badReverb.settings.getProperty("values",{}).getArray()->set(0,1.5);
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(badReverb.catalogId,badReverb.settings));
    badReverb=reverb();badReverb.settings.getProperty("values",{}).getArray()->removeLast();
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(badReverb.catalogId,badReverb.settings));
    badReverb=reverb();badReverb.settings.getDynamicObject()->setProperty("algorithm",4);
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(badReverb.catalogId,badReverb.settings));
    // 31-band portable schema: exact width, strict bounds, no hidden gain,
    // no NaN and no cross-effect injection. File remains persistent after
    // a cold library reopen and has the shared checksummed .armixfx schema.
    {
        auto good=graphicEq31();
        REQUIRE(BuiltInFxUserPresetStore::validateSettings(good.catalogId,good.settings));
        REQUIRE(static_cast<double>(
            good.settings.getProperty("levelDb",{}))==-4.5);
        // Old band-only presets remain valid; loaded at unity (0 dB).
        auto legacy=graphicEq31();
        legacy.settings.getDynamicObject()->removeProperty("levelDb");
        REQUIRE(BuiltInFxUserPresetStore::validateSettings(
            legacy.catalogId,legacy.settings));
        auto invalidLevel=graphicEq31();
        invalidLevel.settings.getDynamicObject()->setProperty("levelDb",6.1);
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(
            invalidLevel.catalogId,invalidLevel.settings));
        invalidLevel.settings.getDynamicObject()->setProperty(
            "levelDb",std::numeric_limits<double>::quiet_NaN());
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(
            invalidLevel.catalogId,invalidLevel.settings));
        auto wrongCount=graphicEq31();
        wrongCount.settings.getProperty("bands",{}).getArray()->removeLast();
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(
            wrongCount.catalogId,wrongCount.settings));
        auto over=graphicEq31();
        over.settings.getProperty("bands",{}).getArray()->set(0,12.1);
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(over.catalogId,over.settings));
        auto nan=graphicEq31();
        nan.settings.getProperty("bands",{}).getArray()->set(
            0,std::numeric_limits<double>::quiet_NaN());
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(nan.catalogId,nan.settings));
        auto injection=graphicEq31();
        injection.settings.getDynamicObject()->setProperty("outputDb",5.0);
        REQUIRE(!BuiltInFxUserPresetStore::validateSettings(
            injection.catalogId,injection.settings));
        auto presetOnDisk=items[4];
        REQUIRE(BuiltInFxUserPresetStore(testRoot).read(
            presetOnDisk.catalogId,presetOnDisk.id,readback).isEmpty());
        REQUIRE(readback.settings.getProperty("bands",{}).getArray()->size()==31);
        REQUIRE(static_cast<double>(
            readback.settings.getProperty("levelDb",{}))==-4.5);
    }
    REQUIRE(BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-ABCD-0123456789Ab"));
    REQUIRE(!BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-GBCD-0123456789Ab"));
    REQUIRE(!BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-ABCD-0123456789A/"));
    REQUIRE(!BuiltInFxUserPresetStore::safeId("../outside"));
    REQUIRE(!BuiltInFxUserPresetStore::safeText("../outside",80));
    REQUIRE(!BuiltInFxUserPresetStore::safeText("bad/name",80));
    REQUIRE(!BuiltInFxUserPresetStore::safeText("hi\nthere",80));
    REQUIRE(!BuiltInFxUserPresetStore::validateSettings(
            "vendor.external.plugin",comp().settings));
    // Checksum or schema corruption must not be advertised as a valid sound.
    const auto& saved=items[1];
    const auto path=testRoot.getChildFile("BuiltInFxPresets")
        .getChildFile("harmonice").getChildFile(saved.id+".armixfx");
    REQUIRE(path.existsAsFile());
    auto tampered=juce::JSON::parse(path.loadFileAsString());
    REQUIRE(tampered.isObject());
    tampered.getDynamicObject()->setProperty("sha256","deadbeef");
    REQUIRE(path.replaceWithText(juce::JSON::toString(tampered,false)));
    REQUIRE(store.read(saved.catalogId,saved.id,readback).isNotEmpty());
    REQUIRE(store.enumerate(saved.catalogId).empty());
    // Corruption must not bypass either quota or name collision checks.
    // This exercise is required in Release; no side-effecting assert().
    const auto compDir=testRoot.getChildFile("BuiltInFxPresets")
        .getChildFile("compressor");
    const auto corrupt=compDir.getChildFile(
        "22222222-2222-2222-2222-222222222222.armixfx");
    REQUIRE(corrupt.replaceWithText("{bad json"));
    auto otherwiseValid=comp();
    otherwiseValid.name="Safe New";
    REQUIRE(store.saveNew(otherwiseValid).isNotEmpty());
    REQUIRE(otherwiseValid.id.isEmpty());
    REQUIRE(corrupt.deleteFile());
    // Empty/stale data from another catalogue must not become a mixer preset.
    REQUIRE(store.enumerate(comp().catalogId).empty());
    std::cout<<"Shared per-built-in FX preset store: EQ/GEQ31/Comp/Harmony/Throw "
             <<"native typed disk roundtrip, corruption and rollback PASS\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr<<"PRESET_CTEST_REJECTED: "<<e.what()<<'\n';
    return 1;
  } catch (...) {
    std::cerr<<"PRESET_CTEST_REJECTED: unknown error"<<'\n';
    return 2;
  }
}
