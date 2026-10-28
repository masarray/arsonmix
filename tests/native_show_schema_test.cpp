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
    assert(loaded.name=="Soundcheck 01");

    std::cout << "PASS: bounded ShowV2/SnapshotV2 JSON round-trips, 10CH references, safes, corrupted records, rollback\n";
    return 0;
}
