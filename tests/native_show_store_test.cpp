// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeShowStore.h"

int main() {
 using namespace armix::shows;
 const auto base=juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getNonexistentChildFile("ArSonMixShowGenerationUnit","",false);
 const auto workspace=base.getChildFile("Studio");
 assert(workspace.createDirectory().wasOk());
 NativeShowStore store(workspace);
 ShowV2 show;
 show.id="11111111-2222-4333-8444-555555555555";
 show.name="Stage Live 01";
 show.createdMs=1761700000000LL;
 show.modifiedMs=show.createdMs+1000;
 const juce::String snapshotId="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
 show.snapshotOrder={snapshotId};
 show.currentSnapshotId=snapshotId;
 show.channelSafes[3]=true;
 SnapshotV2 snap;
 snap.id=snapshotId;snap.name="Soundcheck";
 snap.createdMs=show.createdMs;snap.modifiedMs=show.modifiedMs;
 snap.strips[0].source=2;
 snap.strips[0].inputA=2;snap.strips[0].inputB=5;
 BinaryState blob;
 const char bytes[]={'a','r','m','i','x',0,1,2,'\0'};
 blob.bytes.append(bytes,sizeof(bytes));
 blob.sha256=juce::SHA256(blob.bytes).toHexString();
 snap.strips[0].fx.push_back({"TestVendor|195",195,blob.sha256,false});
 std::vector<SnapshotV2> snapshots={snap};
 std::vector<BinaryState> blobs={blob};
 assert(store.commit(show,snapshots,blobs).isEmpty());
 const auto first=store.currentGeneration(show.id);
 assert(first.isDirectory());
 ShowV2 loaded;
 std::vector<SnapshotV2> retrieved;
 assert(store.load(show.id,loaded,retrieved).isEmpty());
 assert(loaded.channelSafes[3]);
 assert(retrieved.size()==1 && retrieved[0].strips[0].inputB==5);
 assert(retrieved[0].strips[0].fx[0].stateSha256==blob.sha256);

 // Bad/missing binary fails entirely without touching HEAD.
 auto badBlob=blobs;
 badBlob[0].bytes.append("CORRUPT",7);
 assert(store.commit(show,snapshots,badBlob).isNotEmpty());
 assert(store.currentGeneration(show.id)==first);

 // Prepared show cannot reference snapshots not in the transaction.
 ShowV2 badShow=show;
 badShow.snapshotOrder.clear();
 assert(store.commit(badShow,snapshots,blobs).isNotEmpty());
 assert(store.currentGeneration(show.id)==first);

 // Valid newer generation commits exactly one pointer and retains old.
 snapshots[0].name="Performance Ready";
 show.modifiedMs+=1000;
 snapshots[0].modifiedMs+=1000;
 assert(store.commit(show,snapshots,blobs).isEmpty());
 const auto second=store.currentGeneration(show.id);
 assert(second!=first && second.isDirectory() && first.isDirectory());
 assert(store.load(show.id,loaded,retrieved).isEmpty());
 assert(retrieved[0].name=="Performance Ready");

 // Missing/corrupt payload never partially replaces the accepted outputs.
 const auto vendor=second.getChildFile("blobs").getChildFile(blob.sha256+".bin");
 assert(vendor.appendText("damaged"));
 assert(store.load(show.id,loaded,retrieved).isNotEmpty());
 assert(retrieved.size()==1&&retrieved[0].name=="Performance Ready");

 // Bad HEAD rejected before using unsafe path; backup remains intact.
 auto head=workspace.getChildFile("Shows").getChildFile(show.id).getChildFile("HEAD");
 assert(head.replaceWithText("../unsafe"));
 assert(store.load(show.id,loaded,retrieved).isNotEmpty());
 assert(store.currentGeneration(show.id)==juce::File{});
 assert(first.isDirectory());
 base.deleteRecursively();
 std::cout<<"PASS: immutable Shows generation, staged VST3 hash, atomic HEAD, no partial recovery\n";
 return 0;
}
