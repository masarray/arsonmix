// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <optional>

// Pure Player metadata edit policy. CONTROL thread only; neither disk nor
// native deck/PCM is mutated until the live deck accepts the proposed range.
namespace armix::player {
struct PlayerCueEditPolicy final {
    enum class Command { clear, markIn, markOut };
    struct Range {
        double in=0., out=0.;
    };
    // Match the v3 archive's loadable range, rather than admitting a cue
    // that silently disappears when player-library.json is reopened.
    static bool validArchive(Range range) noexcept {
        return std::isfinite(range.in) && std::isfinite(range.out) &&
               range.in>=0. && range.out>=0. &&
               range.in<86400. && range.out<86400. &&
               (range.out==0. || range.out>range.in+0.05);
    }
    static std::optional<Range> plan(Range existing,Command action,
                                     double position,double duration) noexcept {
        if(action==Command::clear)return Range{};
        if(!validArchive(existing) ||
           !std::isfinite(position)||!std::isfinite(duration)||
           duration<=0.||position<0.||position>=duration)
            return std::nullopt;
        // Quantize BEFORE validating. A native time 1.059 must not create
        // an unsavable OUT=1.05 when IN=1.00.
        const double rounded=std::floor(position*100.)/100.;
        Range candidate=existing;
        if(action==Command::markIn) {
            candidate.in=rounded;
            if(candidate.out>0.&&candidate.out<=candidate.in+0.05)
                candidate.out=0.;
        } else {
            candidate.out=rounded;
        }
        if(!validArchive(candidate))return std::nullopt;
        return candidate;
    }
};
} // namespace armix::player
