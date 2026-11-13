// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/guitar/GuitarRigCore.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

using namespace armix::guitarfx;
static_assert(GuitarRigCore::cabinetLatencySamples==32);
int main() {
    GuitarRigCore core;
    const auto factory=GuitarRigCore::startingProgram();
    assert(validate(factory)==ProgramError::none);
    assert(!core.prepare(0.,factory));
    assert(!core.prepare(std::numeric_limits<double>::quiet_NaN(),factory));
    assert(!core.prepare(192001.,factory));
    assert(core.prepare(48000.,factory));
    assert(core.isReady());
    std::array<float,1024> l{},r{};
    constexpr float gain=0.11f;
    for(std::size_t i=0;i<l.size();++i) {
        l[i]=gain*std::sin(2.*3.141592653589793*440.*i/48000.);
        r[i]=l[i];
    }
    const auto original=l;
    core.processStereo(l.data(),r.data(),l.size());
    double wet=0.,changed=0.,difference=0.;
    for(std::size_t i=0;i<l.size();++i){
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(l[i]==r[i]); // stereo channel histories independently identical
        wet+=l[i]*l[i];difference+=std::fabs(l[i]-original[i]);
        changed+=std::fabs(l[i]);
    }
    assert(wet>0.00001 && difference>0.005 && changed>0.001);
    const auto priorWet=wet;
    Program quiet=factory;
    quiet.blocks[2].values[0]=0.95f; // higher amp drive produces a different PCM.
    assert(core.prepare(48000.,quiet));
    l=original;r=original;
    core.processStereo(l.data(),r.data(),l.size());
    double differenceInTone=0.;
    for(std::size_t i=128;i<l.size();++i)
        differenceInTone+=std::fabs(l[i]-r[i]); // same mono input => same output
    assert(differenceInTone<1.e-6);
    double moreDrive=0.;
    for(auto s:l)moreDrive+=s*s;
    assert(std::fabs(priorWet-moreDrive)>1.e-5);

    // Explicit cabinet bypass preserves group delay but not spectral contour.
    auto raw=factory;raw.blocks[3].enabled=false;raw.blocks[0].enabled=false;
    raw.blocks[2].enabled=false;raw.blocks[1].enabled=false;
    assert(core.prepare(48000.,raw));
    l.fill(0.f);r.fill(0.f);l[0]=1.f;r[0]=1.f;
    core.processStereo(l.data(),r.data(),l.size());
    for(int i=0;i<GuitarRigCore::cabinetLatencySamples;++i)
        assert(std::fabs(l[static_cast<std::size_t>(i)])<1.e-7f);
    assert(std::fabs(l[32]-1.f)<1.e-7f);
    assert(std::fabs(r[32]-1.f)<1.e-7f);

    // The original 65-tap symmetric cabinet FIR must remain linear-phase
    // after pairwise multiply optimization. Partitioning buffers cannot
    // change output, history ownership or the 32-sample PDC.
    auto cabinetOnly=raw;
    cabinetOnly.blocks[3].enabled=true;
    for(const double rate:{44100.,48000.,96000.}){
        assert(core.prepare(rate,cabinetOnly));
        l.fill(0.f);r.fill(0.f);l[0]=1.f;r[0]=1.f;
        core.processStereo(l.data(),r.data(),l.size());
        const auto full=l;
        for(int i=0;i<GuitarRigCore::cabinetTaps;++i) {
            const auto left=static_cast<std::size_t>(i);
            const auto mirror=static_cast<std::size_t>(
                GuitarRigCore::cabinetTaps-1-i);
            assert(std::fabs(l[left]-l[mirror])<0.000001f);
            assert(std::isfinite(l[left]) && l[left]==r[left]);
        }
        assert(std::fabs(l[32])>0.02f);
        core.reset();
        l.fill(0.f);r.fill(0.f);l[0]=1.f;r[0]=1.f;
        for(std::size_t offset=0;offset<l.size();offset+=64)
            core.processStereo(l.data()+offset,r.data()+offset,64);
        for(std::size_t i=0;i<l.size();++i)
            assert(std::fabs(l[i]-full[i])<0.000001f);
    }

    // A realtime gain change MUST NOT flush a warm FIR history.
    // Compatible prepared snapshots inherit temporal state, with identical
    // output when the settings themselves have not changed.
    GuitarRigCore source{},same{},changedRate{},changedTone{},cold{};
    assert(source.prepare(48000.,factory));
    assert(same.prepare(48000.,factory));
    assert(changedRate.prepare(44100.,factory));
    auto brighter=factory;
    brighter.blocks[2].values[1]=0.81f;
    assert(changedTone.prepare(48000.,brighter));
    assert(cold.prepare(48000.,brighter));
    std::array<float,512> warmLeft{},warmRight{};
    for(std::size_t i=0;i<warmLeft.size();++i)
        warmLeft[i]=warmRight[i]=0.17f*
            std::sin(2.f*3.14159265358979323846f*440.f*
                     static_cast<float>(i)/48000.f);
    source.processStereo(warmLeft.data(),warmRight.data(),warmLeft.size());
    assert(!changedRate.inheritCompatibleHistory(source));
    assert(same.inheritCompatibleHistory(source));
    assert(changedTone.inheritCompatibleHistory(source));
    std::array<float,128> advance{},reference{},bright{},coldInput{};
    for(std::size_t i=0;i<advance.size();++i){
        const auto value=0.17f*std::sin(2.f*3.14159265358979323846f*
            440.f*static_cast<float>(512+i)/48000.f);
        advance[i]=reference[i]=bright[i]=coldInput[i]=value;
    }
    auto expectedRight=advance;
    auto actualRight=reference;
    source.processStereo(advance.data(),expectedRight.data(),advance.size());
    same.processStereo(reference.data(),actualRight.data(),reference.size());
    for(std::size_t i=0;i<advance.size();++i) {
        assert(std::fabs(advance[i]-reference[i])<0.000001f);
        assert(expectedRight[i]==actualRight[i]);
    }
    auto brightRight=bright;
    changedTone.processStereo(bright.data(),brightRight.data(),bright.size());
    auto coldRight=coldInput;
    cold.processStereo(coldInput.data(),coldRight.data(),coldInput.size());
    double transientDifference=0.;
    for(std::size_t i=0;i<16;++i) {
        transientDifference+=std::fabs(bright[i]-coldInput[i]);
        assert(std::isfinite(bright[i]));
    }
    assert(transientDifference>0.0001); // history is materially preserved

    // Neither a malformed program nor unimplemented enabled modules may
    // silently claim a valid guitar DSP graph. Previous accepted core remains.
    Program invalid=factory;
    invalid.blocks[2].id=1;
    assert(!core.prepare(48000.,invalid));
    assert(core.isReady() && core.program().blocks[2].id==3);
    invalid=factory;invalid.count=5;
    invalid.blocks[4]={5,Module::reverb,true,{}};
    assert(!core.prepare(48000.,invalid));
    assert(core.isReady() && core.program().count==4);
    // No exotic input should yield NaN, infinity or corrupt output.
    l.fill(0.f);r.fill(0.f);
    l[3]=std::numeric_limits<float>::infinity();
    r[5]=std::numeric_limits<float>::quiet_NaN();
    core.processStereo(l.data(),r.data(),l.size());
    for(auto sample:l)assert(std::isfinite(sample));
    for(auto sample:r)assert(std::isfinite(sample));
    return 0;
}
