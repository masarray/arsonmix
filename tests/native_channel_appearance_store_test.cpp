// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>
#include "../src/NativeChannelAppearanceStore.h"

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
int main(){
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixChannelIdentity","",false);
    assert(dir.createDirectory().wasOk());
    const auto g=nativeGraph();
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
        auto snap=store.snapshot(g);
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
        assert(store.snapshot(g).getProperty("revision","").toString()=="2");
    }
    {
        Store reboot(dir); // No WebView/localStorage or device required.
        assert(reboot.error().isEmpty());
        const auto snap=reboot.snapshot(g);
        const auto entries=snap.getProperty("entries",{});
        assert(entries.isArray() && entries.getArray()->size()==2);
        auto req=edit(vocalId,"Studio Vox");
        set(req,"appearanceRevision","2");
        assert(reboot.apply(req,g).isEmpty()); // idempotent, no extra disk write
        assert(reboot.snapshot(g).getProperty("revision","").toString()=="2");
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
