// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string_view>

namespace armix::player {
// GUI-independent stage-playback decisions. No side effects, allocation,
// hardware ownership, graph mutation or background work.
struct SetlistPolicy final {
    struct PitchTempoTarget {
        int index=-1;
        bool updateLiveDeck=false;
    };
    static PitchTempoTarget pitchTempoTarget(
        int current,int selected,int trackCount) noexcept {
        // Selection stages NEXT without interrupting CURRENT. Edits to a
        // selected next song remain queued presets until it is played.
        const int index=(selected>=0 && selected<trackCount)?selected:
            ((current>=0 && current<trackCount)?current:-1);
        return {index,index>=0 && index==current};
    }
    static bool resumeCurrentOnPlay(std::string_view phase,int current) noexcept {
        return current>=0 &&
               (phase=="PAUSED" || phase=="PREPARING" || phase=="READY");
    }
    static int nextTrack(int current,int selected,int trackCount) noexcept {
        if(trackCount<=0)return -1;
        // Selection always queues NEXT, but never interrupts CURRENT until
        // a deliberate double-click or Next action.
        const int next=(selected>=0 && selected<trackCount && selected!=current)
            ?selected:(current<0?selected:current+1);
        return next>=0 && next<trackCount ? next:-1;
    }
};
} // namespace armix::player
