// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../builtin_fx/AutoDuckerCore.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace armix::mixer {
// Borrowed raw capture pointers, valid ONLY inside the current FX invocation.
// Sorted by stable ChannelId on CONTROL; views refreshed once per PCM block.
struct AutoDuckerKeyEntry final {
    std::uint64_t id=0;
    std::size_t laneIndex=0;
    armix::builtin_fx::AutoDuckerKey key{};
};
struct AutoDuckerKeyFrame final {
    std::span<const AutoDuckerKeyEntry> entries{};
    std::uint64_t target=0;
    int frames=0;
    std::span<const AutoDuckerKeyEntry> postFader{};
};
inline thread_local AutoDuckerKeyFrame currentAutoDuckerKeyFrame{};
class AutoDuckerKeyScope final {
public:
    AutoDuckerKeyScope(std::span<const AutoDuckerKeyEntry> entries,
                       std::uint64_t target,int frames,
                       std::span<const AutoDuckerKeyEntry> postFader={}) noexcept
        : previous_(currentAutoDuckerKeyFrame) {
        currentAutoDuckerKeyFrame={entries,target,frames,postFader};
    }
    ~AutoDuckerKeyScope() noexcept {currentAutoDuckerKeyFrame=previous_;}
    AutoDuckerKeyScope(const AutoDuckerKeyScope&)=delete;
    AutoDuckerKeyScope& operator=(const AutoDuckerKeyScope&)=delete;
    static armix::builtin_fx::AutoDuckerKey find(std::uint64_t source,int frames) noexcept {
        return lookup(currentAutoDuckerKeyFrame.entries,source,frames);
    }
    // Only a CONTROL-admitted dependency may expose post-fader PCM.
    static armix::builtin_fx::AutoDuckerKey findPostFader(std::uint64_t source,int frames) noexcept {
        return lookup(currentAutoDuckerKeyFrame.postFader,source,frames);
    }
private:
    static armix::builtin_fx::AutoDuckerKey lookup(std::span<const AutoDuckerKeyEntry> entries,
                                                  std::uint64_t source,int frames) noexcept {
        const auto& f=currentAutoDuckerKeyFrame;
        if(!source||!f.target||source==f.target||frames<=0||frames!=f.frames||entries.empty())return {};
        const auto it=std::lower_bound(entries.begin(),entries.end(),source,
            [](const AutoDuckerKeyEntry& e,std::uint64_t id){return e.id<id;});
        return it!=entries.end()&&it->id==source?it->key:armix::builtin_fx::AutoDuckerKey{};
    }
    AutoDuckerKeyFrame previous_{};
};
} // namespace armix::mixer
