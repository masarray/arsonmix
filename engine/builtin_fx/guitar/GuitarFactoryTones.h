// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GuitarProgramModel.h"
#include <array>
#include <cstddef>

// Factory tone identity and DSP values share ONE implementation across
// native editor, processor and restore. No duplicated UI-only preset guesses.
// These are original algorithmic start tones, not commercial amp captures.
namespace armix::guitarfx {
inline constexpr std::array<const char*,4> factoryToneNames{
    "Clean Studio", "Warm Blues", "Vintage Crunch", "Modern Lead"
};
constexpr Program factoryTone(std::size_t index) noexcept {
    Program p{};
    p.count=4;
    p.blocks[0]={1,Module::gate,true,{}};
    p.blocks[1]={2,Module::overdrive,false,{}};
    p.blocks[2]={3,Module::amplifier,true,{}};
    p.blocks[3]={4,Module::cabinet,true,{}};
    p.blocks[0].values[0]=0.22f;
    p.blocks[1].values[0]=0.45f;
    p.blocks[2].values[0]=0.20f;
    p.blocks[2].values[1]=0.57f;
    p.blocks[2].values[2]=0.66f;
    switch(index) {
        case 1:
            p.blocks[1].enabled=true;
            p.blocks[1].values[0]=0.22f;
            p.blocks[2].values[0]=0.43f;
            break;
        case 2:
            p.blocks[1].enabled=true;
            p.blocks[1].values[0]=0.45f;
            p.blocks[2].values[0]=0.62f;
            break;
        case 3:
            p.blocks[1].enabled=true;
            p.blocks[1].values[0]=0.73f;
            p.blocks[2].values[0]=0.91f;
            break;
        default: break;
    }
    return p;
}
constexpr bool sameTone(const Program& a,const Program& b) noexcept {
    if(a.version!=b.version || a.count!=b.count)return false;
    for(std::size_t i=0;i<a.count;++i) {
        const auto& x=a.blocks[i]; const auto& y=b.blocks[i];
        if(x.id!=y.id || x.module!=y.module || x.enabled!=y.enabled)
            return false;
        for(std::size_t p=0;p<valuesPerBlock;++p)
            if(x.values[p]!=y.values[p])return false;
    }
    return true;
}
// 1..4 for an EXACT known factory program, 0 for Custom.
// A manual gate/amp bypass or any other changed parameter must be Custom.
constexpr int identifyFactoryTone(const Program& p) noexcept {
    if(validate(p)!=ProgramError::none)return 0;
    for(std::size_t i=0;i<factoryToneNames.size();++i)
        if(sameTone(p,factoryTone(i)))return static_cast<int>(i)+1;
    return 0;
}
static_assert(validate(factoryTone(0))==ProgramError::none);
static_assert(validate(factoryTone(3))==ProgramError::none);
static_assert(identifyFactoryTone(factoryTone(1))==2);
} // namespace armix::guitarfx
