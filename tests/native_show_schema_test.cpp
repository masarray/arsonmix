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
    snap.strips[0].inputA=3;snap.strips[0].inputB=5; // sparse physical pins
    snap.strips[0].pan=-0.32;snap.strips[0].fader=0.75;
    snap.strips[0].mute=true;
    snap.strips[0].fx.push_back({
        "vault:test|987",987,juce::String::repeatedString("a",64),true
    });
    snap.strips[0].sends.push_back({5,true,0.4,-0.1});
    snap.strips[6].solo=true;snap.strips[6].toMaster=false;
    // Four AUX return lanes exist IN ADDITION to all ten input strips.
    snap.aux[0].enabled=true;snap.aux[0].gain=0.82;
    snap.aux[0].fx.push_back({"vault:aux-reverb",1234,juce::String::repeatedString("b",64),false});
    snap.aux[3].enabled=true;snap.aux[3].mute=true;
    snap.auxSends.push_back({0,0,true,0.25,-0.2});
    snap.auxSends.push_back({6,3,false,0.75,0.3});
    snap.masterGain=0.65;snap.masterMono=true;
    const auto encoded=Codec::encode(snap);
    SnapshotV2 loaded;
    assert(Codec::decode(encoded,loaded));
    assert(Codec::encode(loaded)==encoded);
    assert(loaded.strips[0].inputA==3&&loaded.strips[0].inputB==5);
    assert(loaded.strips[0].fx[0].stateSha256==juce::String::repeatedString("a",64));
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
    // Compatible older V2 snapshot without the AUX extension: all AUX off.
    auto legacy=juce::JSON::parse(encoded);
    legacy.getDynamicObject()->removeProperty("auxReturns");
    legacy.getDynamicObject()->removeProperty("auxSends");
    legacy.getDynamicObject()->setProperty("schemaVersion",2);
    assert(Codec::decode(juce::JSON::toString(legacy),loaded));
    assert(!loaded.aux[0].enabled && loaded.auxSends.empty());
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

    std::cout << "PASS: bounded ShowV2/SnapshotV2 JSON round-trips, 10 INPUT + 4 AUX, PRE/POST, FX hashes, legacy V2 decode, rollback\n";
    return 0;
}
