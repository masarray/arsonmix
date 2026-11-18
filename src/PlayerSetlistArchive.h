// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <optional>
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
};
class PlayerSetlistArchive final {
    static void parseSongs(const juce::var& items,int version,
                           PlayerSavedSet& destination) {
        const auto* entries=items.getArray();
        if(entries==nullptr)return;
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
               std::find(destination.files.begin(),destination.files.end(),source)
                   !=destination.files.end())continue;
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
                if(std::isfinite(cueIn)&&std::isfinite(cueOut)&&
                   cueIn>=0.&&cueOut>=0.&&cueIn<86400.&&cueOut<86400.&&
                   (cueOut==0.||cueOut>cueIn+0.05)) {
                    settings.cueIn=cueIn;
                    settings.cueOut=cueOut;
                }
            }
            destination.files.push_back(source);
            destination.controls.push_back(settings);
        }
    }
public:
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
        return document;
    }

    static juce::var serialize(const std::vector<PlayerSavedSet>& sets,
                               int active,bool autoNext,int fadeMs) {
        // Reject inconsistent in-memory state BEFORE writing a checkpoint.
        // Never truncate song/control pairs or silently persist a broken show.
        if(sets.empty()||sets.size()>32||active<0||
           active>=static_cast<int>(sets.size())||
           fadeMs<0||fadeMs>3000)return {};
        for(const auto& s:sets)
            if(s.files.size()!=s.controls.size()||s.files.size()>2000)
                return {};
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
        return document;
    }
};
} // namespace armix::player
