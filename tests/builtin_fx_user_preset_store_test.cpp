// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BuiltInFxUserPresetStore.h"
#include <array>
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
static BuiltInFxUserPreset eq() {
    auto bands=juce::var(juce::Array<juce::var>{});
    for(int i=0;i<8;++i)
        bands.append(object({{"enabled",i==2},{"frequencyHz",i==2?2300.0:1000.0},
                  {"gainDb",i==2?2.5:0.0},{"q",1.0},{"slope",12}}));
    return {{}, "armix.builtin.channel-eq.v1","Vocal Presence","Voice",
        object({{"bands",bands}})};
}
int main() {
    const auto testRoot=juce::File::getSpecialLocation(
       juce::File::tempDirectory).getChildFile(
           "ArSonMix-Preset-CTest-"+juce::Uuid().toDashedString());
    REQUIRE(!testRoot.exists());
    struct Cleanup { juce::File folder;~Cleanup(){folder.deleteRecursively();} } cleanup{testRoot};
    BuiltInFxUserPresetStore store(testRoot);
    auto items=std::array{comp(),harmony(),throwSettings(),eq()};
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
    auto json=path.loadFileAsString();
    REQUIRE(path.replaceWithText(json.replace("\"sha256\":\"",
                                            "\"sha256\":\"deadbeef")));
    REQUIRE(store.read(saved.catalogId,saved.id,readback).isNotEmpty());
    REQUIRE(store.enumerate(saved.catalogId).empty());
    std::cout<<"Shared per-built-in FX preset store: EQ/Comp/Harmony/Throw "
             <<"native typed disk roundtrip, corruption and rollback PASS\n";
}
