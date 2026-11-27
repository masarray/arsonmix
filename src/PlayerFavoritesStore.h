// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <set>
namespace armix::player {
// Explicit user gesture only. Independent atomic sidecar, never on PCM.
struct PlayerFavoritesStore final {
    using Songs=std::set<juce::File>;
    static bool valid(const juce::File& song){
        const auto path=song.getFullPathName();
        return path.length()<=4096&&juce::File::isAbsolutePath(path)&&song.hasFileExtension("wav;mp3;flac;aif;aiff;ogg");
    }
    static std::optional<Songs> load(const juce::File& file){
        if(!file.existsAsFile())return Songs{};
        if(file.getSize()>9*1024*1024)return std::nullopt;
        const auto doc=juce::JSON::parse(file.loadFileAsString());
        const auto values=doc.getProperty("files",{});
        const auto* rows=values.getArray();
        if(static_cast<int>(doc.getProperty("version",0))!=1||!rows||rows->size()>2000)return std::nullopt;
        Songs songs;
        for(const auto& row:*rows){
            const auto path=row.toString();
            if(!row.isString()||!juce::File::isAbsolutePath(path)||path.length()>4096)return std::nullopt;
            const juce::File song(path);if(!valid(song))return std::nullopt;
            songs.insert(song);
        }
        return songs;
    }
    static bool save(const juce::File& file,const Songs& songs){
        if(songs.size()>2000)return false;
        juce::Array<juce::var> rows;
        for(const auto& song:songs){if(!valid(song))return false;rows.add(song.getFullPathName());}
        auto doc=std::make_unique<juce::DynamicObject>();
        doc->setProperty("version",1);doc->setProperty("files",juce::var(rows));
        const auto json=juce::JSON::toString(juce::var(doc.release()),false);
        if(json.getNumBytesAsUTF8()>9*1024*1024)return false;
        juce::TemporaryFile staging(file);
        return staging.getFile().replaceWithText(json)&&staging.overwriteTargetFileWithTemporary();
    }
};
}
