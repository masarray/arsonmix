// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include "../src/PlayerSetlistArchive.h"
#include "../src/PlayerFavoritesStore.h"
#include "../src/PlayerSetlistRepository.h"
#include "../src/PlayerSetlistEditTransaction.h"
#include "../src/PlayerLibraryEditPlan.h"
#include "../src/PlayerStateProjection.h"
#include "../engine/player/PlayerLiveMetadataCommitPolicy.h"

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
    // Restart-style reload of the actual favorite sidecar, plus corruption preservation.
    using Favorites=armix::player::PlayerFavoritesStore;
    const auto favoriteFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("arson-player-favorites",".json",false);
    assert(Favorites::load(favoriteFile)&&Favorites::load(favoriteFile)->empty());
    const Favorites::Songs favoriteSongs{juce::File(path),juce::File(other)};
    assert(Favorites::save(favoriteFile,favoriteSongs));
    assert(Favorites::load(favoriteFile)==favoriteSongs);
    assert(favoriteFile.replaceWithText("{invalid"));
    assert(!Favorites::load(favoriteFile));
    assert(favoriteFile.deleteFile());
    // Catalogue survives removal and remains independent of active set.
    const std::vector<SavedSet> one={{"Show",{juce::File(path)},{Track{}}}};
    auto withCatalogue=Archive::parse(Archive::serialize(one,0,false,180,{juce::File(other)}));
    assert(withCatalogue&&withCatalogue->catalogue.size()==2);
    assert(withCatalogue->catalogue[0]==juce::File(other));
    auto emptyCatalogueSet=one;emptyCatalogueSet[0].files.clear();emptyCatalogueSet[0].controls.clear();
    auto retained=Archive::parse(Archive::serialize(emptyCatalogueSet,0,false,180,withCatalogue->catalogue));
    assert(retained&&retained->catalogue.size()==2&&retained->sets[0].files.empty());
    auto badCatalogue=Archive::serialize(one,0,false,180);
    juce::Array<juce::var> relativeCatalogue;relativeCatalogue.add("relative.mp3");
    badCatalogue.getDynamicObject()->setProperty("catalogue",juce::var(relativeCatalogue));
    assert(!Archive::parse(badCatalogue));
    auto autoplaySet=one;autoplaySet[0].controls[0].autoAfter=1;
    auto autoplay=Archive::parse(Archive::serialize(autoplaySet,0,false,180));
    assert(autoplay&&autoplay->sets[0].controls[0].autoAfter==1);
    autoplaySet[0].controls[0].autoAfter=2;
    assert(!Archive::serialize(autoplaySet,0,false,180).isObject());
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

    // Native cue edit is a pure candidate: quantize THEN validate. At
    // IN=1.00, an OUT request at 1.059 becomes 1.05 (invalid) and must
    // not mutate metadata or poison a subsequent unrelated archive Save.
    using Cue=armix::player::PlayerCueEditPolicy;
    const Cue::Range originalCue{1.00,0.0};
    assert(!Cue::plan(originalCue,Cue::Command::markOut,1.059,20.));
    // Zero-quantized OUT must not silently clear an existing OUT marker.
    const Cue::Range existingOut{1.0,5.0};
    assert(!Cue::plan(existingOut,Cue::Command::markOut,0.009,20.));
    assert(!Cue::plan(Cue::Range{0.,5.},
                      Cue::Command::markOut,0.009,20.));
    assert(existingOut.in==1.0 && existingOut.out==5.0);
    assert(originalCue.in==1. && originalCue.out==0.);
    auto untouched=doc->sets;
    untouched[0].controls[0].cueIn=1.0;
    untouched[0].controls[0].cueOut=0.0;
    assert(Archive::serialize(untouched,doc->active,true,250).isObject());
    const auto nextOut=Cue::plan(
        originalCue,Cue::Command::markOut,1.09,20.);
    assert(nextOut && nextOut->out>nextOut->in+0.05);
    const auto nextIn=Cue::plan(
        Cue::Range{1.,5.},Cue::Command::markIn,5.0,20.);
    assert(nextIn && nextIn->in==5. && nextIn->out==0.);
    const auto cleared=Cue::plan(
        Cue::Range{1.,5.},Cue::Command::clear,
        std::numeric_limits<double>::quiet_NaN(),0.);
    assert(cleared && cleared->in==0. && cleared->out==0.);

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

    // Undo admission must reject full or malformed live library state.
    assert(Archive::canUndoRemove(0,0,1999,1999,1999));
    assert(!Archive::canUndoRemove(0,0,1999,2000,2000));
    assert(!Archive::canUndoRemove(0,0,1999,2001,2001));
    assert(!Archive::canUndoRemove(0,1,1999,1999,1999));
    assert(!Archive::canUndoRemove(0,0,-1,1999,1999));
    assert(!Archive::canUndoRemove(0,0,1999,1999,1998));

    // Real archive at capacity: Remove -> Import -> Undo must not turn
    // a valid 2,000-track library into an unpersistable 2,001-track document.
    std::vector<SavedSet> full{{"At capacity",{}, {}}};
    const auto tempRoot=juce::File::getSpecialLocation(
        juce::File::tempDirectory);
    for(int i=0;i<2000;++i) {
        full[0].files.push_back(tempRoot.getChildFile(
            "arsonmix-mv14-capacity-"+juce::String(i)+".wav"));
        full[0].controls.emplace_back();
    }
    const auto fullDocument=Archive::serialize(full,0,false,180);
    assert(fullDocument.isObject());
    const auto fullReload=Archive::parse(juce::JSON::parse(
        juce::JSON::toString(fullDocument,false)));
    assert(fullReload && fullReload->sets.size()==1);
    assert(fullReload->sets[0].files.size()==2000);
    assert(fullReload->sets[0].controls.size()==2000);
    assert(fullReload->sets[0].files[1000]==full[0].files[1000]);
    assert(fullReload->sets[0].files[1999]==full[0].files[1999]);
    const auto removedSong=full[0].files[1000];
    const auto removedPreset=full[0].controls[1000];
    full[0].files.erase(full[0].files.begin()+1000);
    full[0].controls.erase(full[0].controls.begin()+1000);
    assert(Archive::canUndoRemove(0,0,1000,1999,1999));
    full[0].files.push_back(tempRoot.getChildFile(
        "arsonmix-mv14-import-after-remove.wav"));
    full[0].controls.emplace_back();
    assert(Archive::serialize(full,0,false,180).isObject());
    assert(!Archive::canUndoRemove(0,0,1000,2000,2000));
    // Simulating the old unconditional Undo proves the serializer rejects
    // the oversized state; the native caller now refuses this insertion.
    full[0].files.insert(full[0].files.begin()+1000,removedSong);
    full[0].controls.insert(full[0].controls.begin()+1000,removedPreset);
    assert(!Archive::serialize(full,0,false,180).isObject());

    // N-MOD-15: REAL disk adapter regression. Use an isolated directory,
    // never the operator's settings or media library.
    using Store=armix::player::PlayerSetlistRepository;
    const auto sandbox=juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile(
            "arsonmix-nmod15-"+juce::Uuid().toString());
    assert(sandbox.createDirectory().wasOk());
    const auto target=sandbox.getChildFile("player-library.json");
    assert(Store::load(target).status==Store::LoadStatus::missing);
    assert(Store::save(target,doc->sets,doc->active,true,250)
           ==Store::SaveStatus::saved);
    const auto roundTrip=Store::load(target);
    assert(roundTrip.status==Store::LoadStatus::loaded);
    assert(roundTrip.document && roundTrip.document->sets.size()==2);
    assert(roundTrip.document->active==1 && roundTrip.document->autoNext);
    assert(roundTrip.document->fadeMs==250);
    assert(roundTrip.document->sets[0].controls[0].cueOut==9.0);
    const auto acceptedBytes=target.loadFileAsString();
    assert(Store::save(target,broken,0,false,180)
           ==Store::SaveStatus::invalidState);
    assert(target.loadFileAsString()==acceptedBytes);
    // An invalid parent (it is a regular file, NOT a folder) must fail
    // atomically without modifying the already accepted setlist.
    const auto notDirectory=sandbox.getChildFile("not-a-directory");
    assert(notDirectory.replaceWithText("must not be replaced"));
    const auto blocked=notDirectory.getChildFile("player-library.json");
    assert(Store::save(blocked,doc->sets,doc->active,true,250)
           ==Store::SaveStatus::ioError);
    assert(notDirectory.loadFileAsString()=="must not be replaced");
    assert(target.loadFileAsString()==acceptedBytes);
    assert(target.replaceWithText("{bad JSON"));
    assert(Store::load(target).status==Store::LoadStatus::invalid);
    assert(target.loadFileAsString()=="{bad JSON");
    // A valid operator save recovers a corrupt archive without touching
    // the media paths referenced by song records.
    assert(Store::save(target,fullReload->sets,0,false,180)
           ==Store::SaveStatus::saved);
    const auto largeReload=Store::load(target);
    assert(largeReload.document && largeReload.document->sets[0].files.size()==2000);
    assert(largeReload.document->sets[0].controls.size()==2000);
    // N-MOD-16: durable-first metadata edit. An accepted write changes
    // neither the caller's live model nor its deck until it explicitly adopts
    // the returned candidate. The disk is the acceptance boundary.
    using Edit=armix::player::PlayerSetlistEditTransaction;
    const auto transactionFile=sandbox.getChildFile("transactional-setlists.json");
    auto originalSets=doc->sets;
    const auto originalName=originalSets[0].name;
    auto liveFiles=originalSets[0].files;
    auto liveControls=originalSets[0].controls;
    liveFiles.push_back(juce::File(tempRoot.getChildFile("nmod16-new-track.wav")));
    liveControls.push_back({});
    const auto created=Edit::apply(transactionFile,originalSets,0,
        liveFiles,liveControls,true,250,Edit::Command::create);
    assert(created.committed() && created.active==2);
    assert(created.sets.size()==3 && created.sets[0].files.size()==3);
    assert(originalSets.size()==2 && originalSets[0].files.size()==2);
    const auto createdOnDisk=Store::load(transactionFile);
    assert(createdOnDisk.document && createdOnDisk.document->active==2);
    assert(createdOnDisk.document->sets[0].files.size()==3);
    auto selected=Edit::apply(transactionFile,created.sets,created.active,
        created.sets[2].files,created.sets[2].controls,true,250,
        Edit::Command::select,0);
    assert(selected.committed() && selected.active==0);
    assert(Store::load(transactionFile).document->active==0);
    const auto collisionBytes=transactionFile.loadFileAsString();
    const auto collision=Edit::apply(transactionFile,selected.sets,0,
        selected.sets[0].files,selected.sets[0].controls,true,250,
        Edit::Command::rename,-1,"live SHOW");
    assert(!collision.committed());
    assert(collision.error=="Setlist name already exists");
    assert(transactionFile.loadFileAsString()==collisionBytes);
    auto renamed=Edit::apply(transactionFile,selected.sets,0,
        selected.sets[0].files,selected.sets[0].controls,true,250,
        Edit::Command::rename,-1,"  Main Gig  ");
    assert(renamed.committed() && renamed.sets[0].name=="Main Gig");
    assert(selected.sets[0].name==originalName);
    assert(Store::load(transactionFile).document->sets[0].name=="Main Gig");
    const auto deleted=Edit::apply(transactionFile,renamed.sets,0,
        renamed.sets[0].files,renamed.sets[0].controls,true,250,
        Edit::Command::remove,0);
    assert(deleted.committed() && deleted.active==0);
    assert(deleted.sets.size()==2 && deleted.sets[0].name=="Live Show");
    assert(Store::load(transactionFile).document->sets.size()==2);
    const auto written=transactionFile.loadFileAsString();
    const auto failed=Edit::apply(blocked,renamed.sets,0,
        renamed.sets[0].files,renamed.sets[0].controls,true,250,
        Edit::Command::remove,0);
    assert(!failed.committed() && failed.status==Store::SaveStatus::ioError);
    assert(renamed.sets.size()==3 && renamed.sets[0].name=="Main Gig");
    assert(transactionFile.loadFileAsString()==written);
    assert(notDirectory.loadFileAsString()=="must not be replaced");
    const auto badCardinality=Edit::apply(transactionFile,renamed.sets,0,
        renamed.sets[0].files,{},true,250,Edit::Command::rename,-1,"Good");
    assert(!badCardinality.committed());
    assert(transactionFile.loadFileAsString()==written);
    const std::vector<SavedSet> lastOne{{"One",{}, {}}};
    const auto deleteLast=Edit::apply(transactionFile,lastOne,0,{}, {},
        false,180,Edit::Command::remove,0);
    assert(!deleteLast.committed());
    assert(transactionFile.loadFileAsString()==written);
    // N-MOD-16 active-track replacement is a transaction too. Invalid
    // media/control cardinality and occupied destinations never modify disk
    // or require the caller to advance playback/queue indices.
    auto revisedFiles=deleted.sets[0].files;
    auto revisedControls=deleted.sets[0].controls;
    revisedFiles.push_back(tempRoot.getChildFile("nmod16-import.mp3"));
    revisedControls.push_back({});
    const auto committedTracks=Edit::replaceActiveTracks(transactionFile,
        deleted.sets,deleted.active,revisedFiles,revisedControls,true,250);
    assert(committedTracks.committed());
    assert(committedTracks.sets[0].files.size()==1);
    assert(deleted.sets[0].files.empty());
    assert(Store::load(transactionFile).document->sets[0].files.size()==1);
    const auto tracksBytes=transactionFile.loadFileAsString();
    const auto rejectedTracks=Edit::replaceActiveTracks(transactionFile,
        committedTracks.sets,0,revisedFiles,{},true,250);
    assert(!rejectedTracks.committed());
    assert(transactionFile.loadFileAsString()==tracksBytes);
    const auto failedTracks=Edit::replaceActiveTracks(blocked,
        committedTracks.sets,0,revisedFiles,revisedControls,true,250);
    assert(!failedTracks.committed() &&
           failedTracks.status==Store::SaveStatus::ioError);
    assert(committedTracks.sets[0].files.size()==1);
    assert(transactionFile.loadFileAsString()==tracksBytes);
    // N-MOD-17: preferences are saved on the same durable-first boundary.
    // A rejected disk write cannot enable Auto Next or alter future fades.
    const auto prefs=Edit::replaceActiveTracks(
        transactionFile,committedTracks.sets,0,revisedFiles,revisedControls,
        false,500);
    assert(prefs.committed());
    const auto acceptedPrefs=Store::load(transactionFile);
    assert(acceptedPrefs.document && !acceptedPrefs.document->autoNext);
    assert(acceptedPrefs.document->fadeMs==500);
    const auto prefsBytes=transactionFile.loadFileAsString();
    const auto failedPrefs=Edit::replaceActiveTracks(
        blocked,prefs.sets,0,revisedFiles,revisedControls,true,1500);
    assert(!failedPrefs.committed() &&
           failedPrefs.status==Store::SaveStatus::ioError);
    assert(transactionFile.loadFileAsString()==prefsBytes);

    // Pure native+disk compensation sequencing. No JUCE audio device needed.
    using Meta=armix::player::PlayerLiveMetadataCommitPolicy;
    auto verifyMeta=[&](bool live,bool nativeOk,bool saveOk,bool rollbackOk,
                        Meta::Outcome expected,const char* order) {
        std::string sequence;
        const auto outcome=Meta::run(live,
            [&]{sequence+='A';return nativeOk;},
            [&]{sequence+='S';return saveOk;},
            [&]{sequence+='R';return rollbackOk;});
        assert(outcome==expected && sequence==order);
    };
    verifyMeta(false,false,true,false,Meta::Outcome::accepted,"S");
    verifyMeta(false,false,false,false,Meta::Outcome::saveRejectedRecovered,"S");
    verifyMeta(true,false,true,true,Meta::Outcome::nativeRejected,"A");
    verifyMeta(true,true,true,false,Meta::Outcome::accepted,"AS");
    verifyMeta(true,true,false,true,Meta::Outcome::saveRejectedRecovered,"ASR");
    verifyMeta(true,true,false,false,
               Meta::Outcome::saveRejectedRollbackFailed,"ASR");

    // N-MOD-18: real JUCE File plan with in-memory candidate immutability.
    using LibraryPlan=armix::player::PlayerLibraryEditPlan;
    const auto physical=sandbox.getChildFile("music");
    assert(physical.createDirectory().wasOk());
    const auto songA=physical.getChildFile("song-a.wav");
    const auto songB=physical.getChildFile("song-b.flac");
    const auto bad=physical.getChildFile("song-c.txt");
    assert(songA.replaceWithText("audio-fixture"));
    assert(songB.replaceWithText("audio-fixture"));
    assert(bad.replaceWithText("not music"));
    juce::Array<juce::File> selectedFiles;
    selectedFiles.add(songA);selectedFiles.add(songA);
    selectedFiles.add(bad);selectedFiles.add(songB);
    auto imported=LibraryPlan::append({}, {},selectedFiles);
    assert(imported && imported->added==2 && imported->first==0);
    assert(imported->files[0]==songA && imported->files[1]==songB);
    assert(imported->controls.size()==2);
    auto importedAgain=LibraryPlan::append(
        imported->files,imported->controls,selectedFiles);
    assert(importedAgain && importedAgain->added==0 && importedAgain->first==0);
    auto pairControls=imported->controls;
    pairControls[0].key=4;pairControls[1].tempo=115;
    const auto moved=LibraryPlan::move(imported->files,pairControls,0,1);
    assert(moved && moved->files[1]==songA);
    assert(moved->controls[1].key==4 && moved->controls[0].tempo==115);
    assert(imported->files[0]==songA && pairControls[0].key==4);
    const auto removed=LibraryPlan::remove(moved->files,moved->controls,1);
    assert(removed && removed->files.size()==1 && removed->files[0]==songB);
    const auto undone=LibraryPlan::undo(
        removed->files,removed->controls,1,songA,pairControls[0]);
    assert(undone && undone->first==1 && undone->controls[1].key==4);
    assert(!LibraryPlan::undo(
        undone->files,undone->controls,0,songA,pairControls[0]));
    assert(!LibraryPlan::move(imported->files,pairControls,-1,1));
    assert(!LibraryPlan::append(imported->files,{},selectedFiles));

    const auto missing=sandbox.getChildFile("lost/song-b.flac");
    const auto staleReplaced=LibraryPlan::manualRelink(
        {missing},{Track{}},0,missing,songB);
    assert(staleReplaced.candidate && staleReplaced.candidate->files[0]==songB);
    assert(LibraryPlan::manualRelink({songA},{Track{}},0,missing,songB).error==
           LibraryPlan::RelinkError::staleRow);
    assert(LibraryPlan::manualRelink({missing},{Track{}},0,missing,songA).error==
           LibraryPlan::RelinkError::wrongSong);
    assert(LibraryPlan::manualRelink(
        {missing,songB},{Track{},Track{}},0,missing,songB).error==
           LibraryPlan::RelinkError::alreadyPresent);
    armix::player::PlayerLibraryPolicy::Scan foundMusic;
    foundMusic.files={songA,songB};
    const auto folderResult=LibraryPlan::folder(
        {missing},{missing},{Track{}},foundMusic,
        {{0,songB}},-1);
    assert(folderResult && folderResult->relinked==1);
    assert(folderResult->added==1 && folderResult->files[0]==songB);
    assert(folderResult->files[1]==songA);
    // A concurrent UI replacement invalidates the scan's row identity.
    const auto staleFolder=LibraryPlan::folder(
        {missing},{songA},{Track{}},foundMusic,
        {{0,songB}},-1);
    assert(staleFolder && staleFolder->relinked==0);
    assert(staleFolder->files[0]==songA);
    const auto noLiveRelink=LibraryPlan::folder(
        {missing},{missing},{Track{}},foundMusic,
        {{0,songB}},0);
    assert(noLiveRelink && noLiveRelink->relinked==0);
    // One native Player v1 projection: queue revision, metadata, undo
    // admission and optional fields are inspectable without ConsoleWindow.
    using Project=armix::player::PlayerStateProjection;
    const auto projection=Project::build({
        imported->files,pairControls,doc->sets,imported->files,
        juce::var(new juce::DynamicObject()),"Choose a verified MAIN output",
        1,0,0,-1,23,500,0,0,true,false,false
    },true,true);
    assert(projection.isObject());
    assert(projection.getProperty("schema","")=="arsonmix.player.v1");
    assert(static_cast<int>(projection.getProperty("queueRevision",-1))==23);
    assert(static_cast<int>(projection.getProperty("crossfadeMs",-1))==500);
    assert(static_cast<bool>(projection.getProperty("chooseOutput",false)));
    assert(static_cast<bool>(projection.getProperty("canUndoRemove",false)));
    assert(projection.getProperty("setlists",juce::var{}).isArray());
    const auto displayed=projection.getProperty("tracks",juce::var{});
    assert(displayed.isArray() && displayed.getArray()->size()==2);
    assert(static_cast<int>((*displayed.getArray())[0].getProperty(
        "keySemitones",-1))==4);
    assert(static_cast<int>((*displayed.getArray())[1].getProperty(
        "tempoPercent",-1))==115);
    const auto lean=Project::build({
        imported->files,pairControls,doc->sets,imported->files,
        juce::var(new juce::DynamicObject()),"",1,0,0,-1,
        24,180,-1,-1,false,false,false
    },false,true);
    assert(lean.isObject());
    assert(lean.getProperty("setlists",juce::var{}).isVoid());
    assert(lean.getProperty("tracks",juce::var{}).isVoid());
    assert(lean.getProperty("queue",juce::var{}).isVoid());
    assert(!Project::build({
        imported->files,std::vector<Track>{},doc->sets,imported->files,
        juce::var(new juce::DynamicObject()),"",0,0,0,-1,
        24,180,-1,-1,false,false,false
    },true,true).isObject());
    assert(physical.deleteRecursively());
    assert(sandbox.deleteRecursively());

    // Legacy empty v3 content migrates to a non-destructive default setlist.
    const auto empty=Archive::parse(juce::JSON::parse("{\"version\":3}"));
    assert(empty && empty->sets.size()==1);
    assert(empty->sets[0].name=="My Setlist");
    assert(empty->sets[0].files.empty());

    std::cout<<"PASS: v1/v2/v3 migration, physical path identity, cue fence, "
                "safe archive round-trip and fail-closed persistence\n";
    return 0;
}
