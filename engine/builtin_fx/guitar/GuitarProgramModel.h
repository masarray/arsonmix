// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Guitar FX *program model*, not a second processor/graph/callback.
// This is independent of JUCE and hardware; mutations and validation happen
// on CONTROL. Future prepared DSP snapshots consume a validated COPY only.
namespace armix::guitarfx {

inline constexpr std::size_t maxBlocks=16;
inline constexpr std::size_t valuesPerBlock=8;
inline constexpr std::uint32_t schemaVersion=1;

enum class Module : std::uint8_t {
    gate=1, compressor, overdrive, amplifier, cabinet, chorus, delay, reverb
};
enum class ProgramError : std::uint8_t {
    none, unsupportedSchema, invalidCount, invalidId, duplicateId,
    invalidModule, invalidParameter, missingAmplifier, missingCabinet,
    duplicateAmplifier, duplicateCabinet, amplifierCabinetOrder
};

struct Block {
    // Stable *in-program* block identity. Never a ChannelId, slot, processor
    // lease or JUCE pointer, and never inferred from a DOM position.
    std::uint32_t id=0;
    Module module=Module::gate;
    bool enabled=true;
    // Normalized domain values; typed per-module mapping comes later.
    std::array<float,valuesPerBlock> values{};
};
struct Program {
    std::uint32_t version=schemaVersion;
    std::uint8_t count=0;
    std::array<Block,maxBlocks> blocks{};
};

constexpr bool known(Module kind) noexcept {
    return kind>=Module::gate && kind<=Module::reverb;
}
constexpr ProgramError validate(const Program& p) noexcept {
    if(p.version!=schemaVersion)return ProgramError::unsupportedSchema;
    if(p.count<2||p.count>maxBlocks)return ProgramError::invalidCount;
    std::size_t amp=maxBlocks,cab=maxBlocks;
    for(std::size_t i=0;i<p.count;++i) {
        const auto& b=p.blocks[i];
        if(b.id==0)return ProgramError::invalidId;
        if(!known(b.module))return ProgramError::invalidModule;
        for(float v:b.values) {
            // Reject NaN and infinities without requiring C++23 constexpr isfinite.
            if(!(v>=0.0f && v<=1.0f))return ProgramError::invalidParameter;
        }
        for(std::size_t j=i+1;j<p.count;++j)
            if(b.id==p.blocks[j].id)return ProgramError::duplicateId;
        if(b.module==Module::amplifier){
            if(amp!=maxBlocks)return ProgramError::duplicateAmplifier;
            amp=i;
        }
        if(b.module==Module::cabinet){
            if(cab!=maxBlocks)return ProgramError::duplicateCabinet;
            cab=i;
        }
    }
    if(amp==maxBlocks)return ProgramError::missingAmplifier;
    if(cab==maxBlocks)return ProgramError::missingCabinet;
    if(cab!=amp+1)return ProgramError::amplifierCabinetOrder;
    return ProgramError::none;
}
constexpr Program factoryCleanProgram() noexcept {
    Program p{};
    p.count=5;
    p.blocks[0]={1,Module::gate,true,{}};
    p.blocks[1]={2,Module::compressor,true,{}};
    p.blocks[2]={3,Module::amplifier,true,{}};
    p.blocks[3]={4,Module::cabinet,true,{}};
    p.blocks[4]={5,Module::reverb,true,{}};
    p.blocks[2].values[0]=0.32f;
    p.blocks[2].values[5]=0.65f;
    return p;
}
static_assert(validate(factoryCleanProgram())==ProgramError::none);

// CONTROL-only reorder; the old accepted program survives any rejection.
// Amp/Cab are anchored as an adjacent pair in V1. Future parallel paths must
// have their own explicit, versioned graph semantics instead of breaking this.
constexpr bool moveBefore(const Program& accepted,std::uint32_t sourceId,
                          std::uint32_t beforeId,Program& result) noexcept {
    if(validate(accepted)!=ProgramError::none||!sourceId||!beforeId)return false;
    std::size_t from=maxBlocks,to=maxBlocks;
    for(std::size_t i=0;i<accepted.count;++i) {
        if(accepted.blocks[i].id==sourceId)from=i;
        if(accepted.blocks[i].id==beforeId)to=i;
    }
    if(from==maxBlocks||to==maxBlocks)return false;
    if(accepted.blocks[from].module==Module::amplifier||
       accepted.blocks[from].module==Module::cabinet)return false;
    if(from==to){result=accepted;return true;}
    Program candidate=accepted;
    const Block moved=candidate.blocks[from];
    for(std::size_t i=from+1;i<accepted.count;++i)
        candidate.blocks[i-1]=candidate.blocks[i];
    const std::size_t insert=from<to?to-1:to;
    for(std::size_t i=accepted.count-1;i>insert;--i)
        candidate.blocks[i]=candidate.blocks[i-1];
    candidate.blocks[insert]=moved;
    if(validate(candidate)!=ProgramError::none)return false;
    result=candidate;
    return true;
}
constexpr bool setEnabled(const Program& accepted,std::uint32_t id,bool enabled,
                          Program& result) noexcept {
    if(validate(accepted)!=ProgramError::none)return false;
    Program candidate=accepted;
    for(std::size_t i=0;i<candidate.count;++i){
        if(candidate.blocks[i].id==id){
            candidate.blocks[i].enabled=enabled;
            result=candidate;
            return true;
        }
    }
    return false;
}
constexpr bool setNormalizedValue(const Program& accepted,std::uint32_t id,
                                  std::size_t parameter,float value,
                                  Program& result) noexcept {
    if(validate(accepted)!=ProgramError::none||parameter>=valuesPerBlock||
       !(value>=0.0f&&value<=1.0f))return false;
    Program candidate=accepted;
    for(std::size_t i=0;i<candidate.count;++i){
        if(candidate.blocks[i].id==id){
            candidate.blocks[i].values[parameter]=value;
            result=candidate;
            return true;
        }
    }
    return false;
}
} // namespace armix::guitarfx
