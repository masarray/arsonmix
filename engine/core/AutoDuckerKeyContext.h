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
};
inline thread_local AutoDuckerKeyFrame currentAutoDuckerKeyFrame{};
class AutoDuckerKeyScope final {
public:
    AutoDuckerKeyScope(std::span<const AutoDuckerKeyEntry> entries,
                       std::uint64_t target,int frames) noexcept
        : previous_(currentAutoDuckerKeyFrame) {
        currentAutoDuckerKeyFrame={entries,target,frames};
    }
    ~AutoDuckerKeyScope() noexcept {currentAutoDuckerKeyFrame=previous_;}
    AutoDuckerKeyScope(const AutoDuckerKeyScope&)=delete;
    AutoDuckerKeyScope& operator=(const AutoDuckerKeyScope&)=delete;
    static armix::builtin_fx::AutoDuckerKey find(std::uint64_t source,int frames) noexcept {
        const auto& f=currentAutoDuckerKeyFrame;
        if(!source||!f.target||source==f.target||frames<=0||frames!=f.frames||f.entries.empty())return {};
        const auto it=std::lower_bound(f.entries.begin(),f.entries.end(),source,
            [](const AutoDuckerKeyEntry& e,std::uint64_t id){return e.id<id;});
        return it!=f.entries.end()&&it->id==source?it->key:armix::builtin_fx::AutoDuckerKey{};
    }
private:
    AutoDuckerKeyFrame previous_{};
};
} // namespace armix::mixer
