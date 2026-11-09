// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace armix::player {
// FILE/WORKER only. No audio callback, network, plugin or device access.
// Folder import is a bounded, naturally ordered, user-directed action.
struct PlayerLibraryPolicy final {
    static constexpr std::size_t maxSongs=2000;
    static constexpr std::size_t maxEntriesInspected=20000;
    static bool supported(const juce::File& file) {
        return file.hasFileExtension("wav;mp3;flac;aif;aiff;ogg");
    }
    static bool sameSongName(const juce::File& saved,const juce::File& chosen) {
        return supported(chosen) && chosen.existsAsFile() &&
            saved.getFileNameWithoutExtension().isNotEmpty() &&
            saved.getFileNameWithoutExtension().equalsIgnoreCase(
                chosen.getFileNameWithoutExtension());
    }
    struct Scan {
        std::vector<juce::File> files;
        bool truncated=false;
    };
    struct SafeRelink {
        std::size_t index=0;
        juce::File replacement;
    };
    // CONTROL thread after scan worker completes. A chosen folder is explicit
    // permission to search, NOT permission to guess song identity. Relink only
    // an unambiguous full filename (including extension), never a stem match,
    // never a path already owned by another track, and never CURRENT live deck.
    // Preserve the original row, per-song KEY/TEMPO, and selection.
    static std::vector<SafeRelink> planMissingRelinks(
        const std::vector<juce::File>& library,
        const std::vector<juce::File>& scanned,
        int protectedCurrent=-1) {
        std::vector<SafeRelink> plan;
        for(std::size_t i=0;i<library.size();++i) {
            const auto& old=library[i];
            if(static_cast<int>(i)==protectedCurrent || old.existsAsFile())
                continue;
            const auto filename=old.getFileName();
            if(filename.isEmpty() || !supported(old))continue;
            const juce::File* match=nullptr;
            bool ambiguous=false;
            for(const auto& candidate:scanned) {
                if(!candidate.getFileName().equalsIgnoreCase(filename))
                    continue;
                if(!supported(candidate) || !candidate.existsAsFile())
                    continue;
                if(match) {ambiguous=true;break;}
                match=&candidate;
            }
            if(!match || ambiguous)continue;
            // An existing song or second missing row must not be silently
            // mapped to one physical file. Require one-to-one ownership.
            const auto alreadyOwned=std::any_of(library.begin(),library.end(),
                [&](const juce::File& entry) {return entry==*match;});
            if(alreadyOwned)continue;
            const auto duplicateMissing=std::count_if(library.begin(),library.end(),
                [&](const juce::File& entry) {
                    return !entry.existsAsFile() &&
                           entry.getFileName().equalsIgnoreCase(filename);
                });
            if(duplicateMissing!=1)continue;
            plan.push_back({i,*match});
        }
        return plan;
    }
    static Scan scanFolder(const juce::File& folder) {
        Scan result;
        if(!folder.isDirectory())return result;
        // Iterates lazily and stops at finite bounds, including deep folders.
        // Executed by the import worker, never by the WebView message loop.
        juce::DirectoryIterator files(folder,true,"*",juce::File::findFiles);
        std::size_t inspected=0;
        while(files.next()) {
            if(++inspected>maxEntriesInspected) {
                result.truncated=true;break;
            }
            const auto file=files.getFile();
            if(!supported(file))continue;
            if(result.files.size()>=maxSongs) {
                result.truncated=true;break;
            }
            result.files.push_back(file);
        }
        std::sort(result.files.begin(),result.files.end(),
            [](const juce::File& a,const juce::File& b) {
                const int c=a.getFileName().compareNatural(b.getFileName(),false);
                if(c!=0)return c<0;
                return a.getFullPathName().compareIgnoreCase(
                    b.getFullPathName())<0;
            });
        result.files.erase(std::unique(result.files.begin(),result.files.end()),
                           result.files.end());
        return result;
    }
};
} // namespace armix::player
