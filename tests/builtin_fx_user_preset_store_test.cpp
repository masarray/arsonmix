// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/BuiltInFxUserPresetStore.h"
#include <cassert>
#include <iostream>
using armix::storage::BuiltInFxUserPreset;
using armix::storage::BuiltInFxUserPresetStore;

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
    assert(!testRoot.exists());
    struct Cleanup { juce::File folder;~Cleanup(){folder.deleteRecursively();} } cleanup{testRoot};
    BuiltInFxUserPresetStore store(testRoot);
    auto items=std::array{comp(),harmony(),throwSettings(),eq()};
    for(auto& item:items) {
        assert(BuiltInFxUserPresetStore::validateSettings(item.catalogId,item.settings));
        assert(store.saveNew(item).isEmpty());
        assert(BuiltInFxUserPresetStore::safeId(item.id));
        BuiltInFxUserPreset readback;
        assert(store.read(item.catalogId,item.id,readback).isEmpty());
        assert(readback.name==item.name);
        assert(juce::JSON::toString(readback.settings,false)==
               juce::JSON::toString(item.settings,false));
        const BuiltInFxUserPresetStore reopened(testRoot);
        BuiltInFxUserPreset cold;
        assert(reopened.read(item.catalogId,item.id,cold).isEmpty());
        assert(cold.id==item.id);
        assert(reopened.enumerate(item.catalogId).size()==1);
        // Presets are effect-scoped rather than channel, slot or device-scoped.
        for(const auto& other:items)
          if(other.catalogId!=item.catalogId) {
             BuiltInFxUserPreset wrong;
             assert(reopened.read(other.catalogId,item.id,wrong).isNotEmpty());
          }
    }
    // Factory and user data must remain conceptually distinct; disk-only
    // user presets cannot silently be replaced by same category/name.
    auto duplicate=comp();
    assert(store.saveNew(duplicate).isNotEmpty());
    auto renamed=items[0];
    renamed.name="Natural Vocal";
    assert(store.overwrite(renamed).isEmpty());
    BuiltInFxUserPreset readback;
    assert(store.read(renamed.catalogId,renamed.id,readback).isEmpty());
    assert(readback.name=="Natural Vocal");
    assert(store.rename(renamed.catalogId,renamed.id,"Warm Vocal").isEmpty());
    assert(store.read(renamed.catalogId,renamed.id,readback).isEmpty());
    assert(readback.name=="Warm Vocal");
    assert(store.remove(renamed.catalogId,renamed.id).isEmpty());
    assert(store.read(renamed.catalogId,renamed.id,readback).isNotEmpty());
    // Nonportable channel routing, malformed/hostile values, unknown version
    // and cross-catalog data must all fail without damaging another preset.
    auto bad=harmony();
    bad.settings.getDynamicObject()->setProperty("guideChannelId","123456");
    assert(!BuiltInFxUserPresetStore::validateSettings(bad.catalogId,bad.settings));
    assert(store.saveNew(bad).isNotEmpty());
    bad=harmony();bad.settings.getDynamicObject()->setProperty("voices",7);
    assert(store.saveNew(bad).isNotEmpty());
    bad=throwSettings();bad.settings.getDynamicObject()->setProperty("bpm",-1);
    assert(store.saveNew(bad).isNotEmpty());
    bad=eq();bad.settings.getDynamicObject()->setProperty("liveInput",2);
    assert(store.saveNew(bad).isNotEmpty());
    assert(BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-ABCD-0123456789Ab"));
    assert(!BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-GBCD-0123456789Ab"));
    assert(!BuiltInFxUserPresetStore::safeId(
        "12345678-1234-AbCd-ABCD-0123456789A/"));
    assert(!BuiltInFxUserPresetStore::safeId("../outside"));
    assert(!BuiltInFxUserPresetStore::safeText("../outside",80));
    assert(!BuiltInFxUserPresetStore::safeText("bad/name",80));
    assert(!BuiltInFxUserPresetStore::safeText("hi\nthere",80));
    assert(!BuiltInFxUserPresetStore::validateSettings(
            "vendor.external.plugin",comp().settings));
    // Checksum or schema corruption must not be advertised as a valid sound.
    const auto& saved=items[1];
    const auto path=testRoot.getChildFile("BuiltInFxPresets")
        .getChildFile("harmonice").getChildFile(saved.id+".armixfx");
    assert(path.existsAsFile());
    auto json=path.loadFileAsString();
    assert(path.replaceWithText(json.replace("\"sha256\":\"",
                                            "\"sha256\":\"deadbeef")));
    assert(store.read(saved.catalogId,saved.id,readback).isNotEmpty());
    assert(store.enumerate(saved.catalogId).empty());
    std::cout<<"Shared per-built-in FX preset store: EQ/Comp/Harmony/Throw "
             <<"native typed disk roundtrip, corruption and rollback PASS\n";
}
