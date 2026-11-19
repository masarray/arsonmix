// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PlayerSetlistRepository.h"
#include <algorithm>
#include <utility>
#include <vector>

// N-MOD-16: durable-first setlist metadata use case, CONTROL thread only.
// Stage against the current live tracks, persist the complete validated v3
// archive, then return a candidate to the one ConsoleWindow owner. No deck,
// PCM callback, WebView, UI acknowledgement, or other state is changed here.
namespace armix::player {
class PlayerSetlistEditTransaction final {
public:
    enum class Command { create, select, remove, rename };
    using SaveStatus = PlayerSetlistRepository::SaveStatus;
    struct Result {
        SaveStatus status = SaveStatus::invalidState;
        juce::String error;
        std::vector<PlayerSavedSet> sets;
        int active = -1;
        bool committed() const noexcept {
            return status == SaveStatus::saved;
        }
    };

    static Result apply(const juce::File& archive,
                        const std::vector<PlayerSavedSet>& original,
                        int active,
                        const std::vector<juce::File>& liveFiles,
                        const std::vector<PlayerTrackSettings>& liveControls,
                        bool autoNext, int fadeMs, Command command,
                        int index = -1, const juce::String& proposedName = {}) {
        if(original.empty() || active < 0 ||
           active >= static_cast<int>(original.size()) ||
           liveFiles.size() != liveControls.size())
            return reject(SaveStatus::invalidState,"Unable to save setlists; invalid setlist state");

        auto candidate = original;
        candidate[static_cast<std::size_t>(active)].files = liveFiles;
        candidate[static_cast<std::size_t>(active)].controls = liveControls;
        int destination = active;
        switch(command) {
        case Command::create: {
            if(candidate.size() >= 32)
                return reject(SaveStatus::invalidState,"Maximum 32 setlists");
            auto name = PlayerSetlistArchive::nextGeneratedName(candidate);
            if(name.isEmpty())
                return reject(SaveStatus::invalidState,"No available setlist name");
            candidate.push_back({std::move(name),{},{}});
            destination = static_cast<int>(candidate.size()) - 1;
            break;
        }
        case Command::select:
            if(index < 0 || index >= static_cast<int>(candidate.size()))
                return reject(SaveStatus::invalidState,"Invalid setlist");
            destination = index;
            break;
        case Command::remove:
            if(candidate.size() < 2)
                return reject(SaveStatus::invalidState,"Keep at least one setlist");
            if(index < 0 || index >= static_cast<int>(candidate.size()))
                return reject(SaveStatus::invalidState,"Invalid setlist");
            if(index < active)
                --destination;
            else if(index == active)
                destination = std::min(index,static_cast<int>(candidate.size())-2);
            candidate.erase(candidate.begin()+index);
            break;
        case Command::rename: {
            const auto name = proposedName.trim();
            if(name.isEmpty() || name.length() > 64)
                return reject(SaveStatus::invalidState,"Invalid setlist name");
            for(int i=0;i<static_cast<int>(candidate.size());++i)
                if(i != active && candidate[static_cast<std::size_t>(i)].name.equalsIgnoreCase(name))
                    return reject(SaveStatus::invalidState,"Setlist name already exists");
            candidate[static_cast<std::size_t>(active)].name = name;
            break;
        }
        }

        return persist(archive,std::move(candidate),destination,autoNext,fadeMs);
    }

    // Any validated active-song edit uses the same durable-before-adopt gate.
    // Callers stage MOVE/REMOVE/UNDO/import/relink and adjust the deck/queue
    // indices only after this returns a successfully stored candidate.
    static Result replaceActiveTracks(
            const juce::File& archive,
            const std::vector<PlayerSavedSet>& original,int active,
            const std::vector<juce::File>& files,
            const std::vector<PlayerTrackSettings>& controls,
            bool autoNext,int fadeMs) {
        if(original.empty() || active < 0 ||
           active >= static_cast<int>(original.size()) ||
           files.size() != controls.size())
            return reject(SaveStatus::invalidState,
                          "Unable to save setlists; invalid setlist state");
        auto candidate=original;
        auto& set=candidate[static_cast<std::size_t>(active)];
        set.files=files;
        set.controls=controls;
        return persist(archive,std::move(candidate),active,autoNext,fadeMs);
    }
private:
    static Result persist(const juce::File& archive,
                          std::vector<PlayerSavedSet> candidate,int active,
                          bool autoNext,int fadeMs) {
        const auto stored=PlayerSetlistRepository::save(
            archive,candidate,active,autoNext,fadeMs);
        if(stored==SaveStatus::invalidState)
            return reject(stored,"Unable to save setlists; invalid setlist state");
        if(stored==SaveStatus::ioError)
            return reject(stored,"Unable to save setlists; check folder permissions");
        return {SaveStatus::saved,{},std::move(candidate),active};
    }
    static Result reject(SaveStatus status,juce::String error) {
        return {status,std::move(error),{},-1};
    }
};
} // namespace armix::player
