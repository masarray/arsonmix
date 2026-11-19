// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PlayerSetlistArchive.h"
#include "../engine/player/PlayerLibraryPolicy.h"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// N-MOD-18: immutable candidate editing on JUCE CONTROL. No disk writes,
// UI, transport, or PCM changes. ConsoleWindow commits a candidate only
// after the ONE PlayerSetlistEditTransaction repository save succeeds.
namespace armix::player {
class PlayerLibraryEditPlan final {
public:
    struct Candidate {
        std::vector<juce::File> files;
        std::vector<PlayerTrackSettings> controls;
        int first = -1; // first newly inserted OR already-selected file
        int added = 0;
        int relinked = 0;
    };
    enum class RelinkError { none, staleRow, wrongSong, alreadyPresent };
    struct RelinkResult {
        RelinkError error=RelinkError::none;
        std::optional<Candidate> candidate;
    };
    static std::optional<Candidate> base(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls) {
        if(live.size()!=controls.size() ||
           live.size()>PlayerLibraryPolicy::maxSongs)return std::nullopt;
        return Candidate{live,controls};
    }
    static std::optional<Candidate> append(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,
            const juce::Array<juce::File>& selected) {
        auto result=base(live,controls);
        if(!result)return std::nullopt;
        std::map<juce::File,int> positions;
        for(int i=0;i<static_cast<int>(result->files.size());++i)
            positions.emplace(result->files[static_cast<std::size_t>(i)],i);
        for(const auto& file:selected) {
            if(!file.existsAsFile() || !PlayerLibraryPolicy::supported(file))
                continue;
            const auto it=positions.find(file);
            if(it!=positions.end()) {
                if(result->first<0)result->first=it->second;
                continue;
            }
            if(result->files.size()>=PlayerLibraryPolicy::maxSongs)break;
            const auto index=static_cast<int>(result->files.size());
            if(result->first<0)result->first=index;
            positions.emplace(file,index);
            result->files.push_back(file);
            result->controls.emplace_back();
            ++result->added;
        }
        return result;
    }
    // Apply worker findings only to exact rows seen when that scan began.
    // Later performer edits are preserved; no worker may accept a native
    // model, stop a deck, or publish a WebView snapshot.
    static std::optional<Candidate> folder(
            const std::vector<juce::File>& snapshot,
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,
            const PlayerLibraryPolicy::Scan& scan,
            const std::vector<PlayerLibraryPolicy::SafeRelink>& relinks,
            int protectedCurrent) {
        auto result=base(live,controls);
        if(!result)return std::nullopt;
        std::set<juce::File> seen(result->files.begin(),result->files.end());
        for(const auto& change:relinks) {
            const auto index=change.index;
            if(index>=snapshot.size() || index>=result->files.size() ||
               static_cast<int>(index)==protectedCurrent ||
               result->files[index]!=snapshot[index] ||
               seen.contains(change.replacement))continue;
            seen.erase(result->files[index]);
            seen.insert(change.replacement);
            result->files[index]=change.replacement;
            ++result->relinked;
        }
        for(const auto& file:scan.files) {
            if(seen.contains(file))continue;
            if(result->files.size()>=PlayerLibraryPolicy::maxSongs)break;
            if(result->first<0)
                result->first=static_cast<int>(result->files.size());
            seen.insert(file);
            result->files.push_back(file);
            result->controls.emplace_back();
            ++result->added;
        }
        return result;
    }
    static RelinkResult manualRelink(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,
            int index,const juce::File& expected,const juce::File& chosen) {
        auto result=base(live,controls);
        if(!result||index<0||index>=static_cast<int>(live.size())||
           live[static_cast<std::size_t>(index)]!=expected)
            return {RelinkError::staleRow,{}};
        if(!PlayerLibraryPolicy::sameSongName(expected,chosen))
            return {RelinkError::wrongSong,{}};
        if(std::find(live.begin(),live.end(),chosen)!=live.end())
            return {RelinkError::alreadyPresent,{}};
        result->files[static_cast<std::size_t>(index)]=chosen;
        return {RelinkError::none,std::move(result)};
    }
    static std::optional<Candidate> move(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,
            int from,int to) {
        auto result=base(live,controls);
        if(!result||from<0||to<0||
           from>=static_cast<int>(live.size())||
           to>=static_cast<int>(live.size())||from==to)return std::nullopt;
        auto reorder=[&](auto& values) {
            auto entry=std::move(values[static_cast<std::size_t>(from)]);
            values.erase(values.begin()+from);
            values.insert(values.begin()+to,std::move(entry));
        };
        reorder(result->files);
        reorder(result->controls);
        return result;
    }
    static std::optional<Candidate> remove(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,int index) {
        auto result=base(live,controls);
        if(!result||index<0||index>=static_cast<int>(live.size()))
            return std::nullopt;
        result->files.erase(result->files.begin()+index);
        result->controls.erase(result->controls.begin()+index);
        return result;
    }
    static std::optional<Candidate> undo(
            const std::vector<juce::File>& live,
            const std::vector<PlayerTrackSettings>& controls,
            int index,const juce::File& song,PlayerTrackSettings settings) {
        auto result=base(live,controls);
        if(!result||live.size()>=PlayerLibraryPolicy::maxSongs||
           index<0||std::find(live.begin(),live.end(),song)!=live.end())
            return std::nullopt;
        const auto at=std::min(index,static_cast<int>(live.size()));
        result->files.insert(result->files.begin()+at,song);
        result->controls.insert(result->controls.begin()+at,settings);
        result->first=at;
        return result;
    }
};
} // namespace armix::player
