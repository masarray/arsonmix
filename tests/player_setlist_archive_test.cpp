// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/PlayerSetlistArchive.h"

using Archive=armix::player::PlayerSetlistArchive;
using SavedSet=armix::player::PlayerSavedSet;
using Track=armix::player::PlayerTrackSettings;

static juce::var object() {return juce::var(new juce::DynamicObject());}
static juce::var song(const juce::String& path,int key=0,int tempo=100,
                      double in=0.,double out=0.) {
    auto v=object();
    auto* d=v.getDynamicObject();
    d->setProperty("path",path);
    d->setProperty("key",key);
    d->setProperty("tempo",tempo);
    d->setProperty("cueIn",in);
    d->setProperty("cueOut",out);
    return v;
}
static juce::var set(const juce::String& name,
                     const juce::Array<juce::var>& songs) {
    auto v=object();
    v.getDynamicObject()->setProperty("name",name);
    v.getDynamicObject()->setProperty("files",juce::var(songs));
    return v;
}
int main() {
    const auto path=juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("arsonmix-mv14-setlist.wav")
            .getFullPathName();
    const auto other=juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("arsonmix-mv14-other.mp3")
            .getFullPathName();
    // Invalid document/version never causes disk or player changes.
    assert(!Archive::parse(juce::var{}));
    assert(!Archive::parse(juce::JSON::parse("{\"version\":8}")));
    auto v1=object();v1.getDynamicObject()->setProperty("version",1);
    juce::Array<juce::var> paths;
    paths.add(path);paths.add(path);paths.add("relative.mp3");
    paths.add(juce::File(path).withFileExtension(".txt").getFullPathName());
    v1.getDynamicObject()->setProperty("files",juce::var(paths));
    auto doc=Archive::parse(v1);
    assert(doc && doc->sets.size()==1 && doc->sets[0].files.size()==1);
    assert(doc->sets[0].name=="My Setlist");
    assert(doc->sets[0].controls[0].key==0);
    assert(doc->sets[0].controls[0].tempo==100);

    auto v2=object();v2.getDynamicObject()->setProperty("version",2);
    juce::Array<juce::var> oldSongs;
    oldSongs.add(song(path,4,105));oldSongs.add(song(other,7,99));
    v2.getDynamicObject()->setProperty("files",juce::var(oldSongs));
    doc=Archive::parse(v2);
    assert(doc && doc->sets[0].files.size()==2);
    assert(doc->sets[0].controls[0].key==4);
    assert(doc->sets[0].controls[0].tempo==105);
    assert(doc->sets[0].controls[1].key==0);
    assert(doc->sets[0].controls[1].tempo==100);

    auto v3=object();v3.getDynamicObject()->setProperty("version",3);
    juce::Array<juce::var> sets;
    juce::Array<juce::var> songs;
    songs.add(song(path,-5,115,2.0,9.0));
    songs.add(song(other,2,105,10.0,9.0)); // invalid OUT, cue stays zero
    sets.add(set("Soundcheck",songs));
    sets.add(set("soundCHECK",songs)); // duplicate case-insensitive name
    sets.add(set("Live Show",{}));
    v3.getDynamicObject()->setProperty("setlists",juce::var(sets));
    v3.getDynamicObject()->setProperty("activeSetlist",999);
    v3.getDynamicObject()->setProperty("autoNext",true);
    v3.getDynamicObject()->setProperty("crossfadeMs",3100);
    doc=Archive::parse(v3);
    assert(doc && doc->sets.size()==2 && doc->active==1);
    assert(doc->autoNext && doc->fadeMs==180);
    assert(doc->sets[0].files.size()==2);
    assert(doc->sets[0].controls[0].cueIn==2.0);
    assert(doc->sets[0].controls[0].cueOut==9.0);
    assert(doc->sets[0].controls[1].cueIn==0.0);
    assert(doc->sets[0].controls[1].cueOut==0.0);

    // Canonical v3 JSON is a complete round trip, no copying/deleting media.
    const auto encoded=Archive::serialize(doc->sets,doc->active,true,250);
    assert(encoded.isObject());
    assert(encoded.getProperty("version",0)==juce::var(3));
    auto decoded=Archive::parse(juce::JSON::parse(
        juce::JSON::toString(encoded,false)));
    assert(decoded && decoded->sets.size()==2);
    assert(decoded->active==1 && decoded->fadeMs==250);
    assert(decoded->sets[0].files[0].getFullPathName()==path);
    assert(decoded->sets[0].controls[0].cueIn==2.0);

    // Broken song/control cardinality must fail closed before staging JSON.
    std::vector<SavedSet> broken{{"Broken",{juce::File(path)}, {}}};
    assert(!Archive::serialize(broken,0,false,180).isObject());
    assert(!Archive::serialize(doc->sets,5,false,180).isObject());
    assert(!Archive::serialize(doc->sets,1,false,-1).isObject());
    assert(!Archive::serialize({},0,false,180).isObject());

    // Serializer mirrors the parser's admission rules. Otherwise a saved
    // show could silently lose songs or an entire set on the next launch.
    auto invalid=doc->sets;
    invalid[0].name="Live Show"; // collides case-insensitively with set 1
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    invalid=doc->sets;
    invalid[0].name=" ";
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    invalid=doc->sets;
    invalid[0].files[0]=juce::File(path).withFileExtension(".txt");
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    invalid=doc->sets;
    invalid[0].files[0]=invalid[0].files[1];
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    // Duplicate path admission MUST match the same JUCE File equality
    // used by parseSongs() on every supported platform.
    invalid=doc->sets;
    invalid[0].files[1]=juce::File(path.toUpperCase());
    const bool sameFile=invalid[0].files[0]==invalid[0].files[1];
    assert(Archive::serialize(invalid,0,false,180).isObject()!=sameFile);
    if(!sameFile) {
        // A case-sensitive filesystem treats these as separate file names.
        const auto archive=Archive::serialize(invalid,0,false,180);
        const auto reopened=Archive::parse(archive);
        assert(reopened && reopened->sets[0].files.size()==2);
    }
    invalid=doc->sets;
    invalid[0].controls[0].key=9;
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    invalid=doc->sets;
    invalid[0].controls[0].tempo=75;
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    invalid=doc->sets;
    invalid[0].controls[0].cueOut=1.0; // earlier than IN=2
    assert(!Archive::serialize(invalid,0,false,180).isObject());
    // A valid, canonical document still saves and round-trips unchanged.
    assert(Archive::serialize(doc->sets,doc->active,true,250).isObject());

    // Create -> delete -> create must never generate a colliding archive name.
    std::vector<SavedSet> names{
        {"My Setlist",{},{}},
        {"Setlist 2",{},{}},
        {"Setlist 3",{},{}}
    };
    assert(Archive::nextGeneratedName(names)=="Setlist 4");
    names.erase(names.begin()+1); // delete Setlist 2, Setlist 3 remains
    const auto newName=Archive::nextGeneratedName(names);
    assert(newName=="Setlist 2");
    names.push_back({newName,{},{}});
    assert(Archive::serialize(names,2,false,180).isObject());
    names[1].name="setLIST 3"; // case-folded name remains occupied
    assert(Archive::nextGeneratedName(names)=="Setlist 4");

    // Regression: full 2,000-song setlist -> Remove -> Import another
    // song -> Undo. Undo is rejected before mutation; after removing an
    // unrelated song the original undo remains available.
    assert(Archive::canUndoRemove(0,0,1999,1999,1999));
    assert(!Archive::canUndoRemove(0,0,1999,2000,2000));
    assert(!Archive::canUndoRemove(0,0,1999,2001,2001));
    assert(!Archive::canUndoRemove(0,1,1999,1999,1999));
    assert(!Archive::canUndoRemove(0,0,-1,1999,1999));
    assert(!Archive::canUndoRemove(0,0,1999,1999,1998));

    // Legacy empty v3 content migrates to a non-destructive default setlist.
    const auto empty=Archive::parse(juce::JSON::parse("{\"version\":3}"));
    assert(empty && empty->sets.size()==1);
    assert(empty->sets[0].name=="My Setlist");
    assert(empty->sets[0].files.empty());

    std::cout<<"PASS: v1/v2/v3 migration, physical path identity, cue fence, "
                "safe archive round-trip and fail-closed persistence\n";
    return 0;
}
