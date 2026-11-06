// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeShowSchema.h"

int main() {
    using namespace armix::shows;
    const juce::String a="11111111-2222-4333-8444-555555555555";
    const juce::String b="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
    const juce::String c="12345678-1234-4234-9234-123456789abc";
    ShowV2 show;
    show.id=a; show.name="Live Set 1";
    show.createdMs=1761700000000LL;show.modifiedMs=show.createdMs+100;
    show.currentSnapshotId=b;show.snapshotOrder={b,c};
    show.channelSafes[3]=true;
    const auto encodedShow=Codec::encode(show);
    ShowV2 restoredShow;
    assert(Codec::decode(encodedShow,restoredShow));
    assert(restoredShow.snapshotOrder.size()==2);
    assert(restoredShow.currentSnapshotId==b);
    assert(restoredShow.channelSafes[3]&&!restoredShow.channelSafes[4]);
    assert(Codec::encode(restoredShow)==encodedShow);

    ShowV2 invalid=show;
    invalid.snapshotOrder={b,b};
    assert(!Codec::decode(Codec::encode(invalid),restoredShow));
    assert(restoredShow.snapshotOrder.size()==2); // failure atomicity
    invalid=show;invalid.currentSnapshotId=a;
    assert(!Codec::decode(Codec::encode(invalid),restoredShow));
    invalid=show;invalid.name="../unsafe";
    assert(!Codec::decode(Codec::encode(invalid),restoredShow));
    invalid=show;invalid.channelSafes[0]=true;
    auto mutated=juce::JSON::parse(Codec::encode(invalid));
    mutated.getDynamicObject()->setProperty("schemaVersion",3);
    assert(!Codec::decode(juce::JSON::toString(mutated),restoredShow));

    SnapshotV2 snap;
    snap.id=b;snap.name="Soundcheck 01";snap.sourceBuild="p1j";
    snap.createdMs=show.createdMs;snap.modifiedMs=show.modifiedMs;
    snap.strips[0].source=2;
    snap.strips[0].channelFormat=1; // Stereo physical source -> MONO channel
    snap.strips[1].source=1;
    snap.strips[1].channelFormat=2; // Mono physical source -> STEREO channel
    snap.strips[1].inputA=7;
    snap.strips[2].busReturn=true;
    snap.strips[2].channelFormat=1; // BUS receives MONO with no physical input
    snap.strips[0].inputA=3;snap.strips[0].inputB=5; // sparse physical pins
    snap.strips[0].pan=-0.32;snap.strips[0].fader=0.75;
    snap.strips[0].mute=true;
    snap.strips[0].eq.installed=true;
    snap.strips[0].eq.bands[2].enabled=true;
    snap.strips[0].eq.bands[2].frequencyHz=1050.f;
    snap.strips[0].eq.bands[2].gainDb=4.5f;
    snap.strips[0].eq.bands[2].q=1.1f;
    snap.strips[0].fx.push_back({
        "vault:test|987",987,juce::String::repeatedString("a",64),true
    });
    snap.strips[0].sends.push_back({5,true,0.4,-0.1});
    snap.strips[6].solo=true;snap.strips[6].toMaster=false;
    // Four AUX return lanes exist IN ADDITION to all ten input strips.
    snap.aux[0].enabled=true;snap.aux[0].gain=0.82;
    snap.aux[0].fx.push_back({"vault:aux-reverb",1234,juce::String::repeatedString("b",64),false});
    snap.aux[0].eq.installed=true;
    snap.aux[0].eq.bypassed=true; // bypass does NOT destroy EQ curves
    snap.aux[0].eq.bands[0].enabled=true;
    snap.aux[0].eq.bands[0].frequencyHz=110.f;
    snap.aux[0].eq.bands[0].slope=armix::builtin_fx::Slope::db24;
    snap.aux[3].enabled=true;snap.aux[3].mute=true;
    snap.auxSends.push_back({0,0,true,0.25,-0.2});
    snap.auxSends.push_back({6,3,false,0.75,0.3});
    snap.masterGain=0.65;snap.masterMono=true;
    const auto encoded=Codec::encode(snap);
    SnapshotV2 loaded;
    assert(Codec::decode(encoded,loaded));
    assert(Codec::encode(loaded)==encoded);
    assert(loaded.strips[0].inputA==3&&loaded.strips[0].inputB==5);
    assert(loaded.strips[0].channelFormat==1 && loaded.strips[1].channelFormat==2);
    assert(loaded.strips[2].busReturn && loaded.strips[2].channelFormat==1);
    assert(loaded.strips[0].fx[0].stateSha256==juce::String::repeatedString("a",64));
    // V5 EQ is mandatory and fixed-width: never accept a partial 7-band
    // preset that could silently change the realtime DSP routing.
    auto missingEq=juce::JSON::parse(encoded);
    auto* eqRows=missingEq.getProperty("strips",{}).getArray();
    eqRows->getReference(0).getDynamicObject()->removeProperty("builtinEq");
    SnapshotV2 invalidEq{};
    assert(!Codec::decode(juce::JSON::toString(missingEq),invalidEq));
    auto shortEq=juce::JSON::parse(encoded);
    auto* eqStrips=shortEq.getProperty("strips",{}).getArray();
    auto* eqBands=eqStrips->getReference(0).getProperty(
        "builtinEq",{}).getProperty("bands",{}).getArray();
    assert(eqBands&&eqBands->size()==8);
    eqBands->removeLast();
    assert(!Codec::decode(juce::JSON::toString(shortEq),invalidEq));
    auto missingAuxEq=juce::JSON::parse(encoded);
    auto* eqAux=missingAuxEq.getProperty("auxReturns",{}).getArray();
    eqAux->getReference(0).getDynamicObject()->removeProperty("builtinEq");
    assert(!Codec::decode(juce::JSON::toString(missingAuxEq),invalidEq));

    assert(loaded.strips[0].eq.installed&&!loaded.strips[0].eq.bypassed);
    assert(loaded.strips[0].eq.bands[2].enabled&&
           loaded.strips[0].eq.bands[2].gainDb==4.5f);
    assert(loaded.aux[0].eq.installed&&loaded.aux[0].eq.bypassed&&
           loaded.aux[0].eq.bands[0].slope==armix::builtin_fx::Slope::db24);
    assert(loaded.strips[0].sends[0].preFader);
    assert(loaded.strips[6].solo&&!loaded.strips[6].toMaster);
    assert(loaded.masterMono);
    assert(loaded.aux[0].enabled && loaded.aux[0].gain==0.82);
    assert(loaded.aux[0].fx.size()==1 && loaded.aux[0].fx[0].uid==1234);
    assert(loaded.aux[3].enabled && loaded.aux[3].mute);
    assert(loaded.auxSends.size()==2 && loaded.auxSends[0].preFader);
    assert(loaded.auxSends[1].source==6 && loaded.auxSends[1].destination==3);

    auto broken=snap;
    broken.strips[0].fx[0].stateSha256="not-a-digest";
    assert(!Codec::decode(Codec::encode(broken),loaded));
    assert(loaded.strips[0].inputB==5); // failure leaves last good record
    broken=snap;broken.strips[0].inputB=3;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].eq.bands[2].gainDb=21.f;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].eq.bands[2].q=0.f;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].eq.bands[0].slope=
        static_cast<armix::builtin_fx::Slope>(13);
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.aux[0].eq.installed=false;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].eq.installed=false;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].pan=1.01;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].sends[0].destination=0;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].fx.resize(9);
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.strips[0].source=0;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    assert(!Codec::decode(juce::String::repeatedString("x",maxJsonBytes+1),loaded));
    // AUX corruption/rejection must not replace the last accepted record.
    broken=snap;broken.aux[0].gain=5;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.aux[0].fx[0].stateSha256="not-hash";
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.auxSends[0].destination=4;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.aux[0].enabled=false;
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.auxSends.push_back(broken.auxSends[0]);
    assert(!Codec::decode(Codec::encode(broken),loaded));
    broken=snap;broken.auxSends.resize(maxAuxSendsPerSnapshot+1);
    assert(!Codec::decode(Codec::encode(broken),loaded));
    // V4 invalid width and fake downgraded typed data reject atomically.
    auto invalidFormat=juce::JSON::parse(encoded);
    auto invalidRows=invalidFormat.getProperty("strips",{}).getArray();
    invalidRows->getReference(0).getDynamicObject()->setProperty("channelFormat",3);
    assert(!Codec::decode(juce::JSON::toString(invalidFormat),loaded));
    assert(loaded.strips[0].channelFormat==1);

    // V4 had the typed processing width and AUX, but NO native EQ record.
    auto legacyV4=juce::JSON::parse(encoded);
    auto* v4Rows=legacyV4.getProperty("strips",{}).getArray();
    auto* v4Aux=legacyV4.getProperty("auxReturns",{}).getArray();
    for(auto& row:*v4Rows)row.getDynamicObject()->removeProperty("builtinEq");
    for(auto& row:*v4Aux)row.getDynamicObject()->removeProperty("builtinEq");
    legacyV4.getDynamicObject()->setProperty("schemaVersion",4);
    assert(Codec::decode(juce::JSON::toString(legacyV4),loaded));
    assert(!loaded.strips[0].eq.installed&&!loaded.aux[0].eq.installed);
    // A false v4 marker must NOT silently discard enabled V5 EQ.
    auto forgedV4=juce::JSON::parse(encoded);
    forgedV4.getDynamicObject()->setProperty("schemaVersion",4);
    assert(!Codec::decode(juce::JSON::toString(forgedV4),loaded));

    // Older V3 AUX-aware snapshots did not have processing format.
    auto legacyV3=juce::JSON::parse(encoded);
    auto* oldRows=legacyV3.getProperty("strips",{}).getArray();
    for(auto& row:*oldRows) {
        row.getDynamicObject()->removeProperty("channelFormat");
        row.getDynamicObject()->removeProperty("builtinEq");
    }
    auto* oldAuxV3=legacyV3.getProperty("auxReturns",{}).getArray();
    for(auto& row:*oldAuxV3)row.getDynamicObject()->removeProperty("builtinEq");
    legacyV3.getDynamicObject()->setProperty("schemaVersion",3);
    assert(Codec::decode(juce::JSON::toString(legacyV3),loaded));
    assert(loaded.strips[0].channelFormat==2); // native v1 physical stereo
    assert(loaded.strips[1].channelFormat==1); // native v1 physical mono
    assert(loaded.strips[2].channelFormat==2); // native v1 BUS stereo
    auto forgedV3=juce::JSON::parse(encoded);
    forgedV3.getDynamicObject()->setProperty("schemaVersion",3);
    assert(!Codec::decode(juce::JSON::toString(forgedV3),loaded));

    // Compatible older V2 snapshot without the AUX extension or format:
    // no implicit BUS mono processing or hidden loss of a real V4 field.
    auto legacy=juce::JSON::parse(encoded);
    auto* oldRowsV2=legacy.getProperty("strips",{}).getArray();
    for(auto& row:*oldRowsV2){
        row.getDynamicObject()->removeProperty("channelFormat");
        row.getDynamicObject()->removeProperty("builtinEq");
    }
    legacy.getDynamicObject()->removeProperty("auxReturns");
    legacy.getDynamicObject()->removeProperty("auxSends");
    legacy.getDynamicObject()->setProperty("schemaVersion",2);
    assert(Codec::decode(juce::JSON::toString(legacy),loaded));
    assert(!loaded.aux[0].enabled && loaded.auxSends.empty());
    assert(loaded.strips[0].channelFormat==2 && loaded.strips[1].channelFormat==1);
    // Never allow AUX-bearing data to masquerade as V2; older clients would
    // otherwise silently discard it when saving the Show.
    auto downgrade=juce::JSON::parse(encoded);
    downgrade.getDynamicObject()->setProperty("schemaVersion",2);
    assert(!Codec::decode(juce::JSON::toString(downgrade),loaded));
    auto partial=juce::JSON::parse(encoded);
    partial.getDynamicObject()->removeProperty("auxSends");
    assert(!Codec::decode(juce::JSON::toString(partial),loaded));
    assert(!loaded.aux[0].enabled && loaded.auxSends.empty());
    assert(loaded.name=="Soundcheck 01");

    std::cout << "PASS: Show snapshot V5 native EQ (10 CH + 4 AUX), legacy V2-V4 decode, VST3 hashes and rollback\n";
    return 0;
}
