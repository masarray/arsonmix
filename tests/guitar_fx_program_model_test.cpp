// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/guitar/GuitarFactoryTones.h"
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>

using namespace armix::guitarfx;
static_assert(maxBlocks==16);
static_assert(valuesPerBlock==8);
static_assert(std::is_trivially_copyable_v<Program>);
static_assert(validate(factoryCleanProgram())==ProgramError::none);
static_assert(factoryToneNames.size()==4);
static_assert(identifyFactoryTone(factoryTone(0))==1);
static_assert(identifyFactoryTone(factoryTone(1))==2);
static_assert(identifyFactoryTone(factoryTone(2))==3);
static_assert(identifyFactoryTone(factoryTone(3))==4);
static_assert(sameTone(factoryTone(2),factoryTone(2)));
static_assert(!sameTone(factoryTone(1),factoryTone(3)));

int main() {
    constexpr auto base=factoryCleanProgram();
    for(std::size_t i=0;i<factoryToneNames.size();++i) {
        const auto tone=factoryTone(i);
        assert(validate(tone)==ProgramError::none);
        assert(identifyFactoryTone(tone)==static_cast<int>(i)+1);
        auto custom=tone;
        custom.blocks[0].enabled=!custom.blocks[0].enabled;
        assert(identifyFactoryTone(custom)==0);
        custom=tone;
        custom.blocks[2].values[1]=0.99f;
        assert(identifyFactoryTone(custom)==0);
        custom=tone;
        custom.blocks[3].enabled=false;
        assert(identifyFactoryTone(custom)==0);
    }
    Program output{};
    output.version=999; // Sentinel proves rejected mutation preserves output.
    assert(moveBefore(base,5,1,output));
    assert(output.blocks[0].id==5);
    assert(output.blocks[3].module==Module::amplifier);
    assert(output.blocks[4].module==Module::cabinet);
    assert(base.blocks[0].id==1); // Accepted program is immutable.
    assert(validate(output)==ProgramError::none);

    Program sentinel{};
    sentinel.version=777;
    assert(!moveBefore(base,3,1,sentinel)); // AMP is anchored.
    assert(!moveBefore(base,4,1,sentinel)); // CAB is anchored.
    assert(!moveBefore(base,1,4,sentinel)); // Cannot split AMP→CAB.
    assert(!moveBefore(base,42,1,sentinel));
    assert(!moveBefore(base,1,42,sentinel));
    assert(sentinel.version==777);

    assert(setEnabled(base,2,false,output));
    assert(!output.blocks[1].enabled);
    assert(base.blocks[1].enabled);
    assert(!setEnabled(base,500,false,sentinel));
    assert(sentinel.version==777);

    assert(setNormalizedValue(base,3,0,.74f,output));
    assert(output.blocks[2].values[0]==.74f);
    assert(!setNormalizedValue(base,3,valuesPerBlock,.4f,sentinel));
    assert(!setNormalizedValue(base,3,0,1.01f,sentinel));
    assert(!setNormalizedValue(base,3,0,-.01f,sentinel));
    assert(!setNormalizedValue(base,3,0,std::numeric_limits<float>::quiet_NaN(),sentinel));
    assert(sentinel.version==777);

    Program bad=base;
    bad.version=999;
    assert(validate(bad)==ProgramError::unsupportedSchema);
    bad=base;bad.count=0;
    assert(validate(bad)==ProgramError::invalidCount);
    bad=base;bad.count=17;
    assert(validate(bad)==ProgramError::invalidCount);
    bad=base;bad.blocks[0].id=0;
    assert(validate(bad)==ProgramError::invalidId);
    bad=base;bad.blocks[1].id=1;
    assert(validate(bad)==ProgramError::duplicateId);
    bad=base;bad.blocks[0].module=static_cast<Module>(255);
    assert(validate(bad)==ProgramError::invalidModule);
    bad=base;bad.blocks[2].values[0]=std::numeric_limits<float>::infinity();
    assert(validate(bad)==ProgramError::invalidParameter);
    bad=base;bad.blocks[2].module=Module::gate;
    assert(validate(bad)==ProgramError::missingAmplifier);
    bad=base;bad.blocks[3].module=Module::reverb;
    assert(validate(bad)==ProgramError::missingCabinet);
    bad=base;bad.blocks[0].module=Module::amplifier;
    assert(validate(bad)==ProgramError::duplicateAmplifier);
    bad=base;bad.blocks[1].module=Module::cabinet;
    assert(validate(bad)==ProgramError::duplicateCabinet);
    bad=base;bad.blocks[3].module=Module::reverb;
    bad.blocks[4].module=Module::cabinet;
    assert(validate(bad)==ProgramError::amplifierCabinetOrder);
}
