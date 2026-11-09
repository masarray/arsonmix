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
