// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeMixerSessionStore.h"

using namespace armix::mixer;
using namespace armix::session;
using armix::shows::BinaryState;

static MixerSessionDocument makeSession() {
    MixerSessionDocument d;
    d.id="11111111-2222-4333-8444-555555555555";
    d.savedMs=1762520000000LL;
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,256};
    d.outputs={2,0,1,-1,-1,false};
    d.request.physicalInputs=2;d.request.maxFrames=256;
    d.request.maxScratchBytes=32u*1024u*1024u;
    d.masterGain=.7f;d.masterMute=true;d.masterMono=false;
    std::vector<ChannelStripDefinition> rows;
    for(int i=1;i<=10;++i) {
        ChannelStripDefinition row;
        row.id={static_cast<std::uint64_t>(i)};
        row.role=ChannelRole::input;
        row.name="CH "+std::to_string(i);
        row.format=i==1?ProcessingFormat::mono:ProcessingFormat::stereo;
        if(i==1){
            row.sends.push_back({{16},1.f,false,0.f,true,100});
            row.sends.push_back({{11},.3f,true,-.25f,false,101});
        } else if(i==2)row.sends.push_back({{15},1.f,false,0.f,true,102});
        rows.push_back(std::move(row));
        d.legacyIds.inputs[i-1]={static_cast<std::uint64_t>(i)};
    }
    for(int i=0;i<4;++i) {
        ChannelStripDefinition aux;
        aux.id={static_cast<std::uint64_t>(11+i)};
        aux.role=ChannelRole::fxReturn;
        aux.format=ProcessingFormat::stereo;
        aux.name="AUX "+std::to_string(i+1);
        aux.sends.push_back({{15},1.f,false,0.f,true,
                             static_cast<std::uint64_t>(200+i)});
        d.legacyIds.aux[i]=aux.id;
        rows.push_back(std::move(aux));
    }
    ChannelStripDefinition group;
    group.id={16};group.role=ChannelRole::group;
    group.name="Vocal Group";group.format=ProcessingFormat::stereo;
    group.sends.push_back({{15},1.f,false,0.f,true,105});
    rows.push_back(std::move(group));
    ChannelStripDefinition main;
    main.id={15};main.role=ChannelRole::main;
    main.name="MAIN";main.format=ProcessingFormat::stereo;
    rows.push_back(std::move(main));
    d.legacyIds.main={15};
    d.acceptedRevision=52;
    assert(d.roster.restoreExact(d.acceptedRevision,rows));
    d.request.sources.push_back({{1},0,-1,false});
    d.rackBypassed.push_back({1});
    return d;
}
int main(){
    auto d=makeSession();
    const auto before=Codec::encode(d);
    MixerSessionDocument restored;
    assert(Codec::decode(before,restored));
    assert(restored.roster.size()==16);
    assert(restored.roster.revision()==52);
    assert(Codec::encode(restored)==before);
    const auto* vocal=restored.roster.find({1});
    assert(vocal&&vocal->sends.size()==2);
    assert(vocal->sends[0].edgeId==100&&vocal->sends[0].destination.value==16);
    assert(vocal->sends[1].preFader&&vocal->sends[1].gain==.3f);
    assert(restored.request.sources.size()==1);
    assert(restored.request.sources[0].inputL==0);
    assert(restored.binding.backend=="ASIO");
    assert(restored.outputs.mainR==1);
    // Refuse unconfirmed or incompatible device remaps without mutating
    // the saved ChannelId/EdgeId graph; accept only a verified live pair.
    auto remap=d;
    HardwareBinding replacement={"WASAPI","New capture","New playback",
                                 "5","c",44100.,512};
    VerifiedNativeOutputs verifiedPair={2,1,0,-1,-1,false};
    const auto graphBefore=Codec::encode(remap);
    auto refusal=MixerDeviceBindingPolicy::apply(remap,replacement,
                                                 verifiedPair,2,false);
    assert(refusal.startsWith("NEEDS_REMAP"));
    assert(Codec::encode(remap)==graphBefore);
    refusal=MixerDeviceBindingPolicy::apply(remap,replacement,
                                            verifiedPair,1,true);
    assert(refusal.startsWith("NEEDS_REMAP"));
    assert(Codec::encode(remap)==graphBefore);
    assert(MixerDeviceBindingPolicy::apply(remap,replacement,
                                           verifiedPair,2,true).isEmpty());
    assert(remap.binding.backend=="WASAPI" && remap.outputs.mainL==1);
    assert(remap.roster.revision()==d.roster.revision());
    assert(remap.roster.find({1})->sends.size()==2 &&
           remap.roster.find({1})->sends[0].edgeId==100);
    auto exact=d;
    assert(MixerDeviceBindingPolicy::apply(exact,d.binding,d.outputs,2,false)
           .isEmpty());
    assert(Codec::encode(exact)==graphBefore);
    ChannelEdit newChannel;
    newChannel.kind=ChannelEdit::Kind::add;
    newChannel.position=15;newChannel.role=ChannelRole::input;
    newChannel.format=ProcessingFormat::stereo;
    newChannel.text="Added after restart";
    const std::array<ChannelEdit,1> edits{newChannel};
    auto result=restored.roster.edit(restored.roster.revision(),edits);
    assert(result.ok()&&result.createdIds.size()==1&&result.createdIds[0].value==17);
    // Bad IDs, duplicate EdgeIDs or missing physical binding cannot be
    // imported into an accepted PCM graph.
    std::vector<ChannelStripDefinition> duplicate=d.roster.channels();
    duplicate[1].id=duplicate[0].id;
    DynamicChannelRoster invalid;
    assert(!invalid.restoreExact(1,duplicate));
    duplicate=d.roster.channels();
    duplicate[1].sends[0].edgeId=duplicate[0].sends[0].edgeId;
    assert(!invalid.restoreExact(1,duplicate));
    auto bad=juce::JSON::parse(before);
    bad.getDynamicObject()->setProperty("schema","untrusted.version");
    assert(!Codec::decode(juce::JSON::toString(bad,false),restored));
    bad=juce::JSON::parse(before);
    auto binds=bad.getProperty("binding",{});
    binds.getDynamicObject()->setProperty("rate",0.0);
    assert(!Codec::decode(juce::JSON::toString(bad,false),restored));

    // Immutable generation chain with a REAL vendor state hash.
    const auto base=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixDynamicSession","",false);
    const auto studio=base.getChildFile("Studio");
    assert(studio.createDirectory().wasOk());
    NativeMixerSessionStore store(studio);
    BinaryState binary;
    const char bytes[]={'V','S','T','3','-','S','T','A','T','E',0,1,2};
    binary.bytes.append(bytes,sizeof(bytes));
    binary.sha256=juce::SHA256(binary.bytes).toHexString();
    d.inserts.push_back({{1},0,"TestVendor|9|0","C:/FX/Test.vst3",
                         binary.sha256,"EQ",9,false});
    {
        auto installed=d.roster.channels();
        installed[0].insertCatalogIds.push_back("TestVendor|9|0");
        assert(d.roster.restoreExact(d.acceptedRevision,installed));
    }
    // Semantic corruption must be rejected, even if the surrounding JSON
    // is otherwise well-formed or would have a valid SHA-256 on disk.
    const auto validWithFx=Codec::encode(d);
    MixerSessionDocument rejected;
    auto damaged=juce::JSON::parse(validWithFx);
    auto channels=damaged.getProperty("channels",{});
    auto* entries=channels.getArray();
    assert(entries && !entries->isEmpty());
    auto catalog=entries->getReference(0).getProperty("catalogs",{});
    catalog.getArray()->clear();
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    // A ghost slot may not point past the one accepted catalog entry.
    damaged=juce::JSON::parse(validWithFx);
    auto inserts=damaged.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()->setProperty("slot",1);
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    // Wrong plugin on the same slot is not a valid ChannelId FX recovery.
    damaged=juce::JSON::parse(validWithFx);
    inserts=damaged.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()
        ->setProperty("catalogId","OtherVendor|99|0");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    assert(store.commit(d,{binary}).isEmpty());
    MixerSessionDocument loaded;
    std::vector<BinaryState> readback;
    assert(store.load(loaded,readback).isEmpty());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    const auto firstId=studio.getChildFile("MixerSession").getChildFile("HEAD")
        .loadFileAsString().trim();
    assert(armix::shows::detail::uuid(firstId));
    d.masterGain=.35f;d.savedMs+=1000;
    assert(store.commit(d,{binary}).isEmpty());
    assert(store.load(loaded,readback).isEmpty());
    assert(loaded.masterGain==.35f);
    assert(store.recoverPrevious().isNotEmpty()); // NEVER roll back valid HEAD
    const auto head=studio.getChildFile("MixerSession").getChildFile("HEAD");
    const auto secondId=head.loadFileAsString().trim();
    const auto vendor=studio.getChildFile("MixerSession").getChildFile("versions")
        .getChildFile(secondId).getChildFile("blobs")
        .getChildFile(binary.sha256+".bin");
    assert(vendor.appendText("CORRUPTED"));
    assert(store.load(loaded,readback).isNotEmpty());
    assert(loaded.masterGain==.35f); // rejection did not replace output
    assert(store.recoverPrevious().isEmpty());
    assert(store.load(loaded,readback).isEmpty());
    assert(loaded.masterGain==.7f);
    assert(readback[0].bytes==binary.bytes);
    assert(!store.recoverPrevious().isEmpty());
    assert(base.deleteRecursively());
    std::cout<<"PASS: dynamic ChannelId/EdgeId graph, persistent vendor blobs and"
                " explicit verified HEAD recovery\n";
    return 0;
}
