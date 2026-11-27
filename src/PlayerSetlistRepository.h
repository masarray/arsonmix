// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PlayerSetlistArchive.h"
#include <optional>
#include <utility>

// N-MOD-15: one control-thread disk boundary around the pure Player archive.
// Callers still own the active deck, song selection, engine and status ACK.
// This class never manipulates media paths, PCM, WebView or session routing.
namespace armix::player {
class PlayerSetlistRepository final {
public:
    enum class LoadStatus { missing, loaded, invalid };
    enum class SaveStatus { saved, invalidState, ioError };
    struct LoadResult {
        LoadStatus status;
        std::optional<PlayerSetlistDocument> document;
    };
    static LoadResult load(const juce::File& target) {
        if (!target.existsAsFile())
            return {LoadStatus::missing,std::nullopt};
        auto parsed=PlayerSetlistArchive::parse(
            juce::JSON::parse(target.loadFileAsString()));
        if (!parsed)return {LoadStatus::invalid,std::nullopt};
        return {LoadStatus::loaded,std::move(parsed)};
    }
    static SaveStatus save(const juce::File& target,
                           const std::vector<PlayerSavedSet>& sets,
                           int active,bool autoNext,int fadeMs,const std::vector<juce::File>& catalogue={}) {
        // Do not begin writing until *every* set is loadable by the codec.
        const auto document=PlayerSetlistArchive::serialize(
            sets,active,autoNext,fadeMs,catalogue);
        if (!document.isObject())return SaveStatus::invalidState;
        // JUCE creates a sibling staging file and replaces only after the
        // complete payload was written. Never mutate live media files.
        juce::TemporaryFile staging(target);
        if (!staging.getFile().replaceWithText(
                juce::JSON::toString(document,false)) ||
            !staging.overwriteTargetFileWithTemporary())
            return SaveStatus::ioError;
        return SaveStatus::saved;
    }
};
} // namespace armix::player
