// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
#include "../src/NativeChannelAppearanceStore.h"
#include "../src/NativeNamedPatchStore.h"
#include "../src/NativeOutputBusPatch.h"

using armix::appearance::Store;
static juce::var object(){return juce::var(new juce::DynamicObject());}
static void set(juce::var& value,const char* k,const juce::var& v){
    value.getDynamicObject()->setProperty(k,v);
}
static juce::var nativeGraph(){
    auto root=object(),device=object(),graph=object(),caps=object(),session=object();
    set(device,"epoch","7");
    set(graph,"revision","9");set(graph,"rosterRevision","11");
    set(caps,"callbackOwnsChannelIds",true);set(graph,"capabilities",caps);
    juce::Array<juce::var> channels;
    for(const auto& [id,name]:std::vector<std::pair<juce::String,juce::String>>{
        {"9007199254740993","Vocal"},{"9007199254740995","Guitar"},
        {"18446744073709551615","MAIN"}}){
        auto row=object();set(row,"id",id);set(row,"name",name);channels.add(row);
    }
    set(graph,"channels",juce::var(channels));set(root,"graph",graph);
    set(root,"device",device);set(session,"authority","CHANNEL_ID");
    set(root,"mixerSession",session);
    return root;
}
static juce::var edit(const juce::String& id,const juce::String& name,
                      const juce::String& color="#c32974",
                      const juce::String& icon=juce::String::fromUTF8("\xF0\x9F\x8E\xB9")){
    auto v=object();
    set(v,"channelId",id);set(v,"name",name);
    set(v,"color",color);set(v,"icon",icon);
    set(v,"deviceEpoch","7");set(v,"graphRevision","9");
    set(v,"rosterRevision","11");set(v,"appearanceRevision","0");
    return v;
}
static void namedPatchRegression(){
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixNamedIO","",false);
    assert(dir.createDirectory().wasOk());
    auto active=object();
    set(active,"backend","ASIO");set(active,"captureDevice","Test ASIO");set(active,"playbackDevice","Test ASIO");
    set(active,"open",true);set(active,"authorized",true);set(active,"deviceGeneration",7);
    juce::Array<juce::var> names;names.add("Mic 1");names.add("Mic 2");names.add("VC 1");
    juce::Array<juce::var> enabled;enabled.add(0);enabled.add(2);
    set(active,"inputNames",juce::var(names));set(active,"enabledInputs",juce::var(enabled));
    auto req=object();
    for(const char* k:{"backend","captureDevice","playbackDevice","deviceGeneration"})
        set(req,k,active.getProperty(k,{}));
    set(req,"revision",0);set(req,"id","");set(req,"operation","SAVE");
    set(req,"side","input");set(req,"name","Vocal Stereo");set(req,"mode","STEREO");
    set(req,"left",2);set(req,"right",0);set(req,"leftName","VC 1");set(req,"rightName","Mic 1");
    juce::String id;
    {
        armix::io::NamedPatchStore store(dir);
        set(req,"right",1);set(req,"rightName","Mic 2");
        assert(store.apply(req,active).isNotEmpty());
        set(req,"right",0);set(req,"rightName","Mic 1");
        assert(store.apply(req,active).isEmpty());
        const auto rows=store.snapshot(active).getProperty("entries",{});
        assert(rows.isArray()&&rows.getArray()->size()==1);
        id=rows[0].getProperty("id","").toString();
        assert(rows[0].getProperty("available",{})==juce::var(true));
        assert(store.apply(req,active).isNotEmpty());
    }
    {
        armix::io::NamedPatchStore store(dir);
        auto rows=store.snapshot(active).getProperty("entries",{});
        assert(rows.isArray()&&rows.getArray()->size()==1);
        assert(rows[0].getProperty("id","").toString()==id);
        set(req,"id",id);set(req,"revision",1);set(req,"name","Vocal LR");
        assert(store.apply(req,active).isEmpty());
        set(active,"inputNames",juce::var(juce::Array<juce::var>{"Other","Mic 2","VC 1"}));
        rows=store.snapshot(active).getProperty("entries",{});
        assert(rows[0].getProperty("available",{})==juce::var(false));
        set(req,"revision",2);assert(store.apply(req,active).isNotEmpty());
        set(req,"operation","REMOVE");assert(store.apply(req,active).isEmpty());
        assert(store.snapshot(active).getProperty("entries",{}).getArray()->isEmpty());
        // Output preset persists a selection only; even/odd ASIO policy,
        // device isolation and side identity are independently validated.
        juce::Array<juce::var> outputNames,outputPins;
        for(int i=0;i<6;++i){outputNames.add("Out "+juce::String(i+1));outputPins.add(i);}
        set(active,"outputNames",juce::var(outputNames));set(active,"enabledOutputs",juce::var(outputPins));
        set(active,"mainOutputs",juce::var(juce::Array<juce::var>{0,1}));
        set(active,"monitorOutputs",juce::var(juce::Array<juce::var>{2,3}));
        set(req,"revision",3);set(req,"id","");set(req,"operation","SAVE");set(req,"side","output");
        set(req,"name","Streaming");set(req,"mode","STEREO");set(req,"left",3);set(req,"right",4);
        set(req,"leftName","Out 4");set(req,"rightName","Out 5");
        assert(store.apply(req,active).isNotEmpty());
        set(req,"left",0);set(req,"right",1);set(req,"leftName","Out 1");set(req,"rightName","Out 2");
        assert(store.apply(req,active).isNotEmpty()); // reserved MAIN
        set(req,"left",2);set(req,"right",3);set(req,"leftName","Out 3");set(req,"rightName","Out 4");
        assert(store.apply(req,active).isNotEmpty()); // reserved MONITOR
        set(req,"left",4);set(req,"right",5);set(req,"leftName","Out 5");set(req,"rightName","Out 6");
        set(active,"monitorOutputs",juce::var());
        assert(store.apply(req,active).isNotEmpty()); // unknown reservation
        set(active,"monitorOutputs",juce::var(juce::Array<juce::var>{2,3}));
        assert(store.apply(req,active).isEmpty());
        rows=store.snapshot(active).getProperty("entries",{});
        assert(rows.getArray()->size()==1);
        const auto outputId=rows[0].getProperty("id","").toString();
        set(active,"monitorOutputs",juce::var(juce::Array<juce::var>{4,5}));
        assert(store.snapshot(active).getProperty("entries",{} )[0].getProperty("available",{})==juce::var(false));
        set(active,"monitorOutputs",juce::var(juce::Array<juce::var>{2,3}));
        assert(store.snapshot(active).getProperty("entries",{} )[0].getProperty("available",{})==juce::var(true));
        set(req,"revision",4);set(req,"name","streaming");
        assert(store.apply(req,active).isNotEmpty());
        set(active,"playbackDevice","Other ASIO");
        assert(store.snapshot(active).getProperty("entries",{}).getArray()->isEmpty());
        assert(store.apply(req,active).isNotEmpty());
        set(active,"playbackDevice","Test ASIO");
        set(req,"id",outputId);set(req,"operation","REMOVE");set(req,"side","input");
        assert(store.apply(req,active).isNotEmpty());
        set(req,"side","output");assert(store.apply(req,active).isEmpty());
    }
    const auto file=dir.getChildFile("named-io-patches.v1.json");
    assert(file.replaceWithText("broken"));
    armix::io::NamedPatchStore damaged(dir);
    assert(damaged.snapshot(active).getProperty("readOnly",{})==juce::var(true));
    assert(damaged.apply(req,active).isNotEmpty());
    assert(file.loadFileAsString()=="broken");
    assert(dir.deleteRecursively());
}
static void busOutputPatchRegression(){
    using P=armix::io::OutputBusPatch;
    const std::array<int,8> pins{0,1,4,5,8,9,12,13};
    const std::array<int,2> external{12,13};
    const auto changed=P::plan(pins,{0,1},{2,3},false,{0,1},{8,9},external);
    assert(changed&&changed->main==(P::Pair{4,5})&&changed->monitor==(P::Pair{2,3}));
    const auto monitor=P::plan(pins,{0,1},{2,3},true,{4,5},{8,9},external);
    assert(monitor&&monitor->main==(P::Pair{0,1})&&monitor->monitor==(P::Pair{4,5}));
    assert(!P::plan(pins,{0,1},{2,3},false,{0,1},{4,5},external));
    assert(!P::plan(pins,{0,1},{2,3},false,{0,1},{12,13},external));
    assert(!P::plan(pins,{0,1},{2,3},false,{0,1},{2,3},external));
    assert(!P::plan(pins,{0,1},{2,3},false,{1,0},{8,9},external));
    assert(!P::plan(pins,{0,1},{-1,-1},true,{-1,-1},{8,9},external));
}
int main(){
    busOutputPatchRegression();
    namedPatchRegression();
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixChannelIdentity","",false);
    assert(dir.createDirectory().wasOk());
    const auto g=nativeGraph();
    // Match the production transport: apply takes the accepted envelope,
    // while snapshot projects appearances against its nested channel graph.
    const auto graph=g.getProperty("graph",{});
    // Raw UTF-8 bytes are a compiler-charset-independent ground truth.
    const auto keyboard=juce::String::fromUTF8("\xF0\x9F\x8E\xB9");
    assert(Store::validIcon(keyboard));
    assert(edit("9007199254740993","Studio Vox").getProperty("icon",{}).toString()==keyboard);
    const juce::String vocalId="9007199254740993";
    const juce::String guitarId="9007199254740995";
    {
        Store store(dir);
        assert(store.error().isEmpty());
        auto req=edit(vocalId,"Studio Vox");
        // Fail closed on non-graph identity or stale lease.
        set(req,"channelId","9007199254740994");
        const auto missing=store.apply(req,g);
        if(missing!="CHANNEL_NOT_IN_NATIVE_ROSTER") {
            std::cerr<<"Non-roster guard returned: "<<missing.toStdString()<<"\n"
                     <<"request="<<juce::JSON::toString(req,false).toStdString()<<"\n"
                     <<"accepted="<<juce::JSON::toString(g,false).toStdString()<<"\n";
        }
        assert(missing=="CHANNEL_NOT_IN_NATIVE_ROSTER");
        set(req,"channelId",vocalId);
        set(req,"rosterRevision","10");
        assert(store.apply(req,g)=="STALE_CHANNEL_IDENTITY_OR_GRAPH");
        set(req,"rosterRevision","11");
        set(req,"icon","<script>");
        assert(store.apply(req,g)=="INVALID_CHANNEL_NAME_COLOR_OR_ICON");
        set(req,"icon",keyboard);
        assert(store.apply(req,g).isEmpty());
        auto snap=store.snapshot(graph);
        assert(snap.getProperty("revision","").toString()=="1");
        const auto stored=snap.getProperty("entries",{});
        const auto* rows=stored.getArray();
        assert(rows && rows->size()==1);
        assert((*rows)[0].getProperty("id","").toString()==vocalId);
        assert((*rows)[0].getProperty("color","").toString()=="#c32974");
        auto stale=edit(guitarId,"Studio Guitar");
        assert(store.apply(stale,g)=="STALE_CHANNEL_APPEARANCE_REVISION");
        set(stale,"appearanceRevision","1");
        set(stale,"name","Studio Vox");
        assert(store.apply(stale,g)=="CHANNEL_NAME_ALREADY_IN_USE");
        set(stale,"name","Guitar New");
        assert(store.apply(stale,g).isEmpty());
        assert(store.snapshot(graph).getProperty("revision","").toString()=="2");
    }
    {
        Store reboot(dir); // No WebView/localStorage or device required.
        assert(reboot.error().isEmpty());
        const auto snap=reboot.snapshot(graph);
        const auto entries=snap.getProperty("entries",{});
        assert(entries.isArray() && entries.getArray()->size()==2);
        auto req=edit(vocalId,"Studio Vox");
        set(req,"appearanceRevision","2");
        assert(reboot.apply(req,g).isEmpty()); // idempotent, no extra disk write
        assert(reboot.snapshot(graph).getProperty("revision","").toString()=="2");
        set(req,"name","Studio Vox 2");
        set(req,"deviceEpoch","8");
        assert(reboot.apply(req,g)=="STALE_CHANNEL_IDENTITY_OR_GRAPH");
    }
    // A corrupted existing document is read-only, not silently erased.
    auto path=dir.getChildFile("channel-appearance.v1.json");
    assert(path.replaceWithText("{corrupt"));
    Store damaged(dir);
    assert(damaged.error().isNotEmpty());
    auto req=edit(vocalId,"Try overwrite");
    assert(damaged.apply(req,g)==damaged.error());
    assert(path.loadFileAsString()=="{corrupt");
    assert(dir.deleteRecursively());
    std::cout << "PASS native appearance disk + exact uint64 IDs + atomic revision + readback\n";
}
