// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AntiNoizControl.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <limits>
namespace armix::builtin_fx::anti_noiz {
// Durable parameters only. The learned PSD is input/epoch specific and is
// deliberately not persisted until the host can prove physical source identity.
struct NativeState final {
    static constexpr std::size_t legacySize=28,v2Size=80,size=92;
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
        put(b,4,3);put(b,8,static_cast<std::uint32_t>(s.mode));
        put(b,12,std::bit_cast<std::uint32_t>(s.cleanDb));
        put(b,16,std::bit_cast<std::uint32_t>(s.silenceDb));
        put(b,20,std::bit_cast<std::uint32_t>(s.musicProtect));put(b,24,s.enabled?1u:0u);
        put(b,28,s.spectralManual?1u:0u);put(b,32,std::bit_cast<std::uint32_t>(s.noiseOffsetDb));
        put(b,36,s.manual.enabled?1u:0u);put(b,40,s.manual.gate?1u:0u);
        const float values[]={s.manual.thresholdDb,s.manual.ratio,s.manual.rangeDb,s.manual.attackMs,s.manual.holdMs,s.manual.releaseMs,s.manual.kneeDb,s.manual.hpHz,s.manual.lpHz};
        for(std::size_t i=0;i<9;++i)put(b,44+4*i,std::bit_cast<std::uint32_t>(values[i]));
        put(b,80,s.spectralEnabled?1u:0u);put(b,84,std::bit_cast<std::uint32_t>(s.artifactControl));put(b,88,std::bit_cast<std::uint32_t>(s.vocalProtection));
        return b;
    }
    static bool decode(std::span<const std::uint8_t> b,Core::Settings& out) noexcept {
        if((b.size()!=legacySize&&b.size()!=v2Size&&b.size()!=size)||get(b,0)!=0x5a4f4e41u||get(b,8)>3||get(b,24)>1)return false;
        const bool legacy=b.size()==legacySize;
        if(get(b,4)!=(legacy?1u:b.size()==v2Size?2u:3u))return false;
        Core::Settings s{static_cast<Core::Mode>(get(b,8)),std::bit_cast<float>(get(b,12)),
            std::bit_cast<float>(get(b,16)),std::bit_cast<float>(get(b,20)),get(b,24)!=0};
        if(legacy&&s.cleanDb>24)return false;
        if(!legacy){
            if(get(b,28)>1||get(b,36)>1||get(b,40)>1)return false;
            s.spectralManual=get(b,28)!=0;s.noiseOffsetDb=std::bit_cast<float>(get(b,32));
            s.manual.enabled=get(b,36)!=0;s.manual.gate=get(b,40)!=0;
            float* values[]={&s.manual.thresholdDb,&s.manual.ratio,&s.manual.rangeDb,&s.manual.attackMs,&s.manual.holdMs,&s.manual.releaseMs,&s.manual.kneeDb,&s.manual.hpHz,&s.manual.lpHz};
            for(std::size_t i=0;i<9;++i)*values[i]=std::bit_cast<float>(get(b,44+4*i));
        }
        if(b.size()==size){if(get(b,80)>1)return false;s.spectralEnabled=get(b,80)!=0;s.artifactControl=std::bit_cast<float>(get(b,84));s.vocalProtection=std::bit_cast<float>(get(b,88));}
        if(!Core::validSettings(s))return false;
        out=s;return true;
    }
};
}
