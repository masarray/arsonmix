// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AntiNoizControl.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
namespace armix::builtin_fx::anti_noiz {
// Durable parameters only. The learned PSD is input/epoch specific and is
// deliberately not persisted until the host can prove physical source identity.
struct NativeState final {
    static constexpr std::size_t size=28;
    using Bytes=std::array<std::uint8_t,size>;
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    static void put(Bytes& b,std::size_t i,std::uint32_t v) noexcept {
        for(unsigned k=0;k<4;++k)b[i+k]=static_cast<std::uint8_t>(v>>(8*k));
    }
    static std::uint32_t get(std::span<const std::uint8_t> b,std::size_t i) noexcept {
        std::uint32_t v=0;
        for(unsigned k=0;k<4;++k)v|=std::uint32_t(b[i+k])<<(8*k);
        return v;
    }
    static Bytes encode(Core::Settings s) noexcept {
        Bytes b{};
        put(b,0,0x5a4f4e41u); // ANOZ little-endian
        put(b,4,1);put(b,8,static_cast<std::uint32_t>(s.mode));
        put(b,12,std::bit_cast<std::uint32_t>(s.cleanDb));
        put(b,16,std::bit_cast<std::uint32_t>(s.silenceDb));
        put(b,20,std::bit_cast<std::uint32_t>(s.musicProtect));put(b,24,s.enabled?1u:0u);
        return b;
    }
    static bool decode(std::span<const std::uint8_t> b,Core::Settings& out) noexcept {
        if(b.size()!=size||get(b,0)!=0x5a4f4e41u||get(b,4)!=1||get(b,8)>3||get(b,24)>1)return false;
        Core::Settings s{static_cast<Core::Mode>(get(b,8)),std::bit_cast<float>(get(b,12)),
            std::bit_cast<float>(get(b,16)),std::bit_cast<float>(get(b,20)),get(b,24)!=0};
        if(!std::isfinite(s.cleanDb)||s.cleanDb<0||s.cleanDb>24||
           !std::isfinite(s.silenceDb)||s.silenceDb<0||s.silenceDb>48||
           !std::isfinite(s.musicProtect)||s.musicProtect<0||s.musicProtect>1)return false;
        out=s;return true;
    }
};
}
