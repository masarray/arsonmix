// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
#include "../VocalThrow.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
namespace armix::builtin_fx::vocal_delay {
// AVTH v1 is the exact shipping JUCE 48-byte little-endian payload.
// v2 keeps identity but deliberately has different semantics and version.
// No reinterpretation of old settings as continuous delay.
struct State final {
    enum class Kind { legacyThrow, continuous };
    Kind kind=Kind::continuous;
    VocalThrowSettings legacy{};
    Parameters continuous{};
};
class StateCodec final {
public:
    using Bytes=std::array<std::uint8_t,48>;
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
    static bool encode(const State& s,Bytes& out) noexcept {
        Bytes b{};std::size_t n=0;
        auto u=[&](std::uint32_t v){put(b,n,v);};
        auto f=[&](float v){std::uint32_t bits;std::memcpy(&bits,&v,4);u(bits);};
        u(0x41565448);
        if(s.kind==State::Kind::legacyThrow){
            const auto& p=s.legacy;if(!VocalThrow::validSettings(p))return false;
            u(1);u(std::uint32_t(p.mode));f(p.bpm);u(std::uint32_t(p.division));
            u(std::uint32_t(p.repeats));f(p.captureMs);f(p.silenceMs);f(p.thresholdDb);
            f(p.wet);f(p.decay);f(p.ducking);
        }else if(s.kind==State::Kind::continuous){
            const auto& p=s.continuous;if(!valid(p))return false;
            u(2);f(p.timeMs);f(p.feedback);f(p.echo);f(p.highPassHz);f(p.lowPassHz);
            f(p.duck);f(p.duckAttackMs);f(p.duckReleaseMs);
            u(std::uint32_t(p.mode));u(p.enabled?1u:0u);
        }else return false;
        if(n!=b.size())return false;
        out=b;return true;
    }
    static bool decode(const void* data,std::size_t size,State& out) noexcept {
        if(!data||size!=48)return false;
        const auto* b=static_cast<const std::uint8_t*>(data);std::size_t n=0;
        auto u=[&](){return get(b,n);};
        auto f=[&](){const auto bits=u();float v;std::memcpy(&v,&bits,4);return v;};
        if(u()!=0x41565448)return false;
        const auto version=u();State s{};
        if(version==1){
            s.kind=State::Kind::legacyThrow;
            auto& p=s.legacy;
            const auto mode=u();p.bpm=f();const auto div=u(),repeats=u();
            if(mode>2||div>3||repeats<1||repeats>6)return false;
            p.mode=int(mode);p.division=int(div);p.repeats=int(repeats);
            p.captureMs=f();p.silenceMs=f();p.thresholdDb=f();
            p.wet=f();p.decay=f();p.ducking=f();
            if(!VocalThrow::validSettings(p))return false;
        }else if(version==2){
            auto& p=s.continuous;
            p.timeMs=f();p.feedback=f();p.echo=f();p.highPassHz=f();p.lowPassHz=f();
            p.duck=f();p.duckAttackMs=f();p.duckReleaseMs=f();
            const auto mode=u(),enabled=u();if(mode>2||enabled>1)return false;
            p.mode=static_cast<StereoMode>(mode);p.enabled=enabled==1;
            if(!valid(p))return false;
        }else return false;
        if(n!=size)return false;
        out=s;return true;
    }
private:
    static void put(Bytes& b,std::size_t& n,std::uint32_t x) noexcept{
        for(unsigned i=0;i<4;++i)b[n++]=std::uint8_t(x>>(i*8));
    }
    static std::uint32_t get(const std::uint8_t* b,std::size_t& n) noexcept{
        std::uint32_t x=0;for(unsigned i=0;i<4;++i)x|=std::uint32_t(b[n++])<<(i*8);
        return x;
    }
};
} // namespace armix::builtin_fx::vocal_delay
