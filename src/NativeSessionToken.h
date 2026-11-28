// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <limits>

namespace armix::console {
// ChannelId producers/commands use canonical positive decimal uint64 strings.
// Legacy M4 WebView JSON may encode an integral Number as JUCE double.
// Never reinterpret a fractional/NaN/oversized JavaScript value as a
// different device epoch or graph revision. This is CONTROL thread only.
inline std::uint64_t nativeSessionTokenValue(const juce::var& value) noexcept {
    if(value.isString()) {
        const auto text=value.toString();
        auto p=text.getCharPointer();
        if(p.isEmpty() || *p<'1' || *p>'9')return 0;
        std::uint64_t n=0;
        constexpr auto max=std::numeric_limits<std::uint64_t>::max();
        while(!p.isEmpty()) {
            const auto ch=p.getAndAdvance();
            if(ch<'0'||ch>'9')return 0;
            const auto digit=static_cast<std::uint64_t>(ch-'0');
            if(n>(max-digit)/10)return 0;
            n=n*10+digit;
        }
        return n;
    }
    if(value.isInt() || value.isInt64()) {
        const auto n=static_cast<juce::int64>(value);
        return n>0 ? static_cast<std::uint64_t>(n) : 0;
    }
    if(value.isDouble()) {
        const auto n=static_cast<double>(value);
        constexpr double maxExact=9007199254740991.0; // 2^53 - 1
        if(std::isfinite(n) && n>0. && n<=maxExact &&
           std::floor(n)==n)
            return static_cast<std::uint64_t>(n);
    }
    return 0;
}
inline bool nativeSessionTokenValid(const juce::var& supplied) noexcept {
    return nativeSessionTokenValue(supplied)!=0;
}
inline bool nativeSessionTokenMatches(const juce::var& supplied,
                                      const juce::var& producer) noexcept {
    const auto accepted=nativeSessionTokenValue(producer);
    return accepted!=0 && nativeSessionTokenValue(supplied)==accepted;
}
} // namespace armix::console
