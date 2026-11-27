// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include "../engine/player/PlayerCueEditPolicy.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// MV-N14: control-thread-only schema v1/v2/v3 codec. One Player deck remains
// owned by ConsoleWindow/LiveAudioEngine; this codec does no file I/O,
// transport, device access, mutation of a live setlist or realtime work.
namespace armix::player {
struct PlayerTrackSettings {
    int key=0, tempo=100;
    double cueIn=0., cueOut=0.;
};
struct PlayerSavedSet {
    juce::String name;
    std::vector<juce::File> files;
    std::vector<PlayerTrackSettings> controls;
};
struct PlayerSetlistDocument {
    std::vector<PlayerSavedSet> sets;
    int active=0;
    bool autoNext=false;
    int fadeMs=180;
    std::vector<juce::File> catalogue;
};
class PlayerSetlistArchive final {
    static void parseSongs(const juce::var& items,int version,
                           PlayerSavedSet& destination) {
        const auto* entries=items.getArray();
        if(entries==nullptr)return;
        // Use the same JUCE File identity as serialize(). A 2,000-track
        // archive must not do quadratic duplicate scans on every startup.
        std::set<juce::File> knownPaths(
            destination.files.begin(),destination.files.end());
        for(const auto& item:*entries) {
            if(destination.files.size()>=2000)break;
            if((version==1&&!item.isString()) ||
               (version>=2&&!item.isObject()))continue;
            const auto path=version==1?item.toString():
                item.getProperty("path","").toString();
            if(path.isEmpty()||path.length()>4096||
               !juce::File::isAbsolutePath(path))continue;
            const juce::File source(path);
            if(!source.hasFileExtension("wav;mp3;flac;aif;aiff;ogg") ||
               !knownPaths.insert(source).second)continue;
            PlayerTrackSettings settings;
            if(version>=2) {
                settings.key=static_cast<int>(item.getProperty("key",0));
                settings.tempo=static_cast<int>(item.getProperty("tempo",100));
                if(settings.key< -6||settings.key>6||
                   settings.tempo<80||settings.tempo>120)settings={};
            }
            if(version>=3) {
                const double cueIn=static_cast<double>(
                    item.getProperty("cueIn",0.0));
                const double cueOut=static_cast<double>(
                    item.getProperty("cueOut",0.0));
                if(PlayerCueEditPolicy::validArchive({cueIn,cueOut})) {
                    settings.cueIn=cueIn;
                    settings.cueOut=cueOut;
                }
            }
            destination.files.push_back(source);
            destination.controls.push_back(settings);
        }
    }
public:
    static constexpr std::size_t maxCatalogue=64000;
    static std::optional<std::vector<juce::File>> mergeCatalogue(
        const std::vector<juce::File>& known,const std::vector<PlayerSavedSet>& sets) {
        std::vector<juce::File> result;std::set<juce::File> unique;
        auto add=[&](const juce::File& file){
            const auto path=file.getFullPathName();
            if(path.isEmpty()||path.length()>4096||!juce::File::isAbsolutePath(path)||!file.hasFileExtension("wav;mp3;flac;aif;aiff;ogg"))return false;
            if(unique.insert(file).second){if(result.size()>=maxCatalogue)return false;result.push_back(file);}return true;
        };
        for(const auto& file:known)if(!add(file))return std::nullopt;
        for(const auto& set:sets)for(const auto& file:set.files)if(!add(file))return std::nullopt;
        return result;
    }
    // Keep automatic creation consistent with the archive's case-insensitive
    // uniqueness rule, including a delete-then-create cycle. At <32 sets,
    // candidate numbers 2..33 always contain an unused name.
    // Undo is an insertion. Reject it before mutating live library/control
    // arrays if an intervening import refilled the 2,000-song capacity.
    // Keep this identical to serialize()/parseSongs() admission.
    static bool canUndoRemove(int activeSet,int undoSet,int undoIndex,
                              std::size_t songs,std::size_t controls) noexcept {
        return undoSet==activeSet && undoIndex>=0 &&
               songs==controls && songs<2000;
    }

    static juce::String nextGeneratedName(
        const std::vector<PlayerSavedSet>& sets) {
        for(int number=2;number<=33;++number) {
            const auto candidate="Setlist "+juce::String(number);
            const bool used=std::any_of(sets.begin(),sets.end(),
                [&](const PlayerSavedSet& s) {
                    return s.name.equalsIgnoreCase(candidate);
                });
            if(!used)return candidate;
        }
        return {};
    }

    static std::optional<PlayerSetlistDocument> parse(const juce::var& json) {
        if(!json.isObject())return std::nullopt;
        const int version=static_cast<int>(json.getProperty("version",0));
        if(version!=1&&version!=2&&version!=3)return std::nullopt;
        PlayerSetlistDocument document;
        // Retain the variant while walking arrays; a pointer into a
        // temporary juce::var would dangle.
        const auto setlists=json.getProperty("setlists",{});
        if(version<=2) {
            PlayerSavedSet migrated{"My Setlist",{},{}};
            parseSongs(json.getProperty("files",{}),version,migrated);
            document.sets.push_back(std::move(migrated));
        } else if(const auto* saved=setlists.getArray()) {
            for(const auto& item:*saved) {
                if(document.sets.size()>=32)break;
                if(!item.isObject())continue;
                auto name=item.getProperty("name","").toString().trim();
                if(name.isEmpty()||name.length()>64)continue;
                const bool duplicate=std::any_of(document.sets.begin(),
                    document.sets.end(),[&](const PlayerSavedSet& s) {
                        return s.name.equalsIgnoreCase(name);
                    });
                if(duplicate)continue;
                PlayerSavedSet next{name,{},{}};
                parseSongs(item.getProperty("files",{}),3,next);
                document.sets.push_back(std::move(next));
            }
        }
        if(document.sets.empty())
            document.sets.push_back({"My Setlist",{},{}});
        const int active=version==3?
            static_cast<int>(json.getProperty("activeSetlist",0)):0;
        document.active=std::clamp(active,0,
            static_cast<int>(document.sets.size())-1);
        const auto autoNext=json.getProperty("autoNext",false);
        document.autoNext=autoNext.isBool()&&static_cast<bool>(autoNext);
        const int fade=static_cast<int>(json.getProperty("crossfadeMs",180));
        document.fadeMs=(fade>=0&&fade<=3000)?fade:180;
        const auto savedCatalogue=json.getProperty("catalogue",{});
        if(const auto* items=savedCatalogue.getArray()){
            if(static_cast<std::size_t>(items->size())>maxCatalogue)return std::nullopt;
            for(const auto& item:*items){
                const auto path=item.toString();
                if(!item.isString()||path.length()>4096||!juce::File::isAbsolutePath(path))return std::nullopt;
                document.catalogue.emplace_back(path);
            }
        }else if(!savedCatalogue.isVoid())return std::nullopt;
        auto catalogue=mergeCatalogue(document.catalogue,document.sets);
        if(!catalogue)return std::nullopt;
        document.catalogue=std::move(*catalogue);
        return document;
    }

    static juce::var serialize(const std::vector<PlayerSavedSet>& sets,
                               int active,bool autoNext,int fadeMs,const std::vector<juce::File>& catalogue={}) {
        // Reject inconsistent in-memory state BEFORE writing a checkpoint.
        // Never truncate song/control pairs or silently persist a broken show.
        if(sets.empty()||sets.size()>32||active<0||
           active>=static_cast<int>(sets.size())||
           fadeMs<0||fadeMs>3000)return {};
        for(std::size_t index=0;index<sets.size();++index) {
            const auto& s=sets[index];
            const auto name=s.name.trim();
            if(name.isEmpty()||name.length()>64||name!=s.name||
               s.files.size()!=s.controls.size()||s.files.size()>2000)
                return {};
            // A saved setlist must be loadable without silently dropping a
            // set, song or cue on the next startup. Validate before touching
            // the staging file; active Player/PCM ownership is unchanged.
            for(std::size_t previous=0;previous<index;++previous)
                if(sets[previous].name.equalsIgnoreCase(name))
                    return {};
            // O(n log n), rather than O(n^2) file comparisons for large
            // setlists (up to 2,000 tracks in each of 32 saved sets).
            // JUCE File::operator< matches File::operator== platform rules,
            // including case-insensitive path identity on Windows/macOS.
            std::set<juce::File> uniquePaths;
            for(std::size_t i=0;i<s.files.size();++i) {
                const auto path=s.files[i].getFullPathName();
                if(path.isEmpty()||path.length()>4096||
                   !juce::File::isAbsolutePath(path)||
                   !s.files[i].hasFileExtension("wav;mp3;flac;aif;aiff;ogg"))
                    return {};
                if(!uniquePaths.insert(s.files[i]).second)
                    return {};
                const auto& settings=s.controls[i];
                if(settings.key< -6||settings.key>6||
                   settings.tempo<80||settings.tempo>120||
                   !PlayerCueEditPolicy::validArchive(
                       {settings.cueIn,settings.cueOut}))
                    return {};
            }
        }
        auto document=juce::var(new juce::DynamicObject());
        auto* out=document.getDynamicObject();
        out->setProperty("version",3);
        out->setProperty("activeSetlist",active);
        out->setProperty("autoNext",autoNext);
        out->setProperty("crossfadeMs",fadeMs);
        juce::Array<juce::var> savedSets;
        for(const auto& set:sets) {
            auto row=juce::var(new juce::DynamicObject());
            row.getDynamicObject()->setProperty("name",set.name);
            juce::Array<juce::var> songs;
            for(std::size_t i=0;i<set.files.size();++i) {
                auto song=juce::var(new juce::DynamicObject());
                song.getDynamicObject()->setProperty(
                    "path",set.files[i].getFullPathName());
                song.getDynamicObject()->setProperty("key",set.controls[i].key);
                song.getDynamicObject()->setProperty(
                    "tempo",set.controls[i].tempo);
                song.getDynamicObject()->setProperty(
                    "cueIn",set.controls[i].cueIn);
                song.getDynamicObject()->setProperty(
                    "cueOut",set.controls[i].cueOut);
                songs.add(std::move(song));
            }
            row.getDynamicObject()->setProperty("files",juce::var(songs));
            savedSets.add(std::move(row));
        }
        out->setProperty("setlists",juce::var(savedSets));
        const auto merged=mergeCatalogue(catalogue,sets);if(!merged)return {};
        juce::Array<juce::var> paths;for(const auto& file:*merged)paths.add(file.getFullPathName());
        out->setProperty("catalogue",juce::var(paths));
        return document;
    }
};
} // namespace armix::player
