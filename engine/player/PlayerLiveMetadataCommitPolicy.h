// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// N-MOD-17: compensation policy for durable Player metadata writes on the
// JUCE CONTROL thread. The existing LiveAudioEngine remains the sole deck
// owner. Native acceptance precedes disk I/O for responsive live KEY/TEMPO
// and Cue gestures; failed persistence attempts the exact previous values.
// An unrecoverable native rollback is handled by the caller's safe stop.
namespace armix::player {
struct PlayerLiveMetadataCommitPolicy final {
    enum class Outcome {
        accepted,
        nativeRejected,
        saveRejectedRecovered,
        saveRejectedRollbackFailed
    };
    template<typename ApplyNative,typename Save,typename RestoreNative>
    static Outcome run(bool requiresNative,ApplyNative&& apply,
                       Save&& save,RestoreNative&& restore) {
        // Editing NEXT is metadata only. No native/PCM DSP mutation happens.
        if(!requiresNative)
            return save()?Outcome::accepted:Outcome::saveRejectedRecovered;
        if(!apply())return Outcome::nativeRejected;
        if(save())return Outcome::accepted;
        return restore()?Outcome::saveRejectedRecovered:
            Outcome::saveRejectedRollbackFailed;
    }
};
} // namespace armix::player
