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

    // A professional stereo gate is a LINKED detector, not two competing
    // channel gates. A quiet L lane must stay at exactly the same gain
    // reduction as a louder R lane; the processed stereo ratio is preserved.
    Program gateOnly=factory;
    gateOnly.blocks[1].enabled=false; // isolate gate from nonlinear stages
    gateOnly.blocks[2].enabled=false;
    gateOnly.blocks[3].enabled=false; // same fixed 32-sample dry PDC
    gateOnly.blocks[0].values[0]=0.5f; // -55 dB
    gateOnly.blocks[0].values[1]=0.0f;
    gateOnly.blocks[0].values[2]=0.0f;
    GuitarRigCore stereoGate{};
    assert(stereoGate.prepare(48000.,gateOnly));
    std::array<float,4096> weak{},strong{};
    weak.fill(0.0008f);strong.fill(0.08f);
    stereoGate.processStereo(weak.data(),strong.data(),weak.size());
    for(std::size_t i=512;i<weak.size();++i) {
        assert(std::isfinite(weak[i])&&std::isfinite(strong[i]));
        assert(strong[i]>0.07f);
        assert(std::fabs(weak[i]/strong[i]-0.01f)<0.000002f);
    }
    // Hold is truly implemented in DSP. With a threshold of -30 dB and
    // a quiet sustained tail, the no-hold gate closes while the linked
    // 120ms hold gate remains open. The release knob changes real audio
    // attenuation, not merely the appearance of the GUI.
    gateOnly.blocks[0].values[0]=1.f;
    auto longHold=gateOnly;
    longHold.blocks[0].values[1]=1.f;
    auto slowClose=gateOnly;
    slowClose.blocks[0].values[2]=1.f;
    GuitarRigCore noHold{},held{},slow{};
    assert(noHold.prepare(48000.,gateOnly));
    assert(held.prepare(48000.,longHold));
    assert(slow.prepare(48000.,slowClose));
    std::array<float,256> loudA{},loudB{};
    loudA.fill(0.1f);loudB.fill(0.1f);
    for(int block=0;block<12;++block){ // 64ms to establish open gate
        auto a=loudA,b=loudB;noHold.processStereo(a.data(),b.data(),a.size());
        a=loudA;b=loudB;held.processStereo(a.data(),b.data(),a.size());
        a=loudA;b=loudB;slow.processStereo(a.data(),b.data(),a.size());
    }
    std::array<float,4096> tailNoHold{},tailHold{},tailSlow{};
    std::array<float,4096> tailR1{},tailR2{},tailR3{};
    tailNoHold.fill(0.005f);tailHold.fill(0.005f);tailSlow.fill(0.005f);
    tailR1=tailNoHold;tailR2=tailHold;tailR3=tailSlow;
    noHold.processStereo(tailNoHold.data(),tailR1.data(),tailNoHold.size());
    held.processStereo(tailHold.data(),tailR2.data(),tailHold.size());
    slow.processStereo(tailSlow.data(),tailR3.data(),tailSlow.size());
    const auto finalSample=tailNoHold.size()-64u;
    assert(tailHold[finalSample]>0.004f);
    assert(tailNoHold[finalSample]<0.0010f);
    assert(tailSlow[finalSample]>tailNoHold[finalSample]*3.f);
    assert(tailHold[finalSample]==tailR2[finalSample]);
    // The normalized control schema remains unchanged: old GRX1 v1
    // programs with reserved zeros are still valid first-class tones.
    assert(validate(gateOnly)==ProgramError::none);
    assert(GuitarRigCore::supportsProgram(gateOnly));
    assert(validate(longHold)==ProgramError::none);
    assert(validate(slowClose)==ProgramError::none);

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
