// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Stable-index changes for the operator's current song, selected NEXT,
// and pending async transition. No data allocations or UI/runtime dependencies.
namespace armix::player {
struct PlayerShowEditPolicy final {
    static int afterMove(int index,int from,int to,int size) noexcept {
        if(index<0)return -1;
        if(from<0 || to<0 || from>=size || to>=size || from==to)
            return index;
        if(index==from)return to;
        if(from<to && index>from && index<=to)return index-1;
        if(from>to && index>=to && index<from)return index+1;
        return index;
    }
    static int afterRemove(int index,int removed) noexcept {
        if(index<0 || removed<0)return index;
        return index==removed?-1:index>removed?index-1:index;
    }
    static int afterInsert(int index,int inserted) noexcept {
        if(index<0 || inserted<0)return index;
        return index>=inserted?index+1:index;
    }
};
} // namespace armix::player
