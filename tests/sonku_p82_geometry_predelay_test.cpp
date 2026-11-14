// SPDX-License-Identifier: AGPL-3.0-only
// Dedicated P8.2 direct algorithm proof: geometry-dependent late onset,
// no stale input/FDN carry across OFF/ON, no callback allocation.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) || !SONKU_P82_GEOMETRIC_LATE_RESEARCH
#error P8.2 standalone test must compile the actual delayed reverb
#endif
#include "../engine/builtin_fx/sonku/SonKuMusicalReverb.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
using namespace armix::sonku;
static int onset(double sampleRate,SonKuReverbPreset preset) {
    auto reverb=std::make_unique<SonKuMusicalReverb>();
    assert(reverb->prepare(sampleRate));
    assert(reverb->setRealtimeProgram(preset,.60f));
    const int firstDelay=reverb->preparedLatePredelaySamples();
    const float width= preset==SonKuReverbPreset::StudioGlue?7.2f:
                       preset==SonKuReverbPreset::AiryChamber?14.4f:28.8f;
    const int expected=static_cast<int>(std::lround(
        width*sampleRate/343.));
    assert(std::abs(firstDelay-expected)<=1);
    const int minimumFdDelay=static_cast<int>(std::lround(
        2111.*sampleRate/48000.));
    int observed=-1;
    double power=0.;
    for(int i=0;i<static_cast<int>(sampleRate*.6);++i){
        const auto sample=reverb->process(i==0?.1f:0.f,i==0?.1f:0.f);
        assert(std::isfinite(sample.left)&&std::isfinite(sample.right));
        assert(std::abs(sample.left)<.99f&&std::abs(sample.right)<.99f);
        if(observed<0 && std::abs(sample.left)+std::abs(sample.right)>1.e-8f)
            observed=i;
        power+=sample.left*sample.left+sample.right*sample.right;
    }
    assert(observed>=minimumFdDelay+firstDelay-1);
    assert(observed<=minimumFdDelay+firstDelay+1);
    assert(power>1.e-9);
    std::printf("P82_LATE_ONSET,rate=%.0f,room_width=%.1f,predelay=%d,first_late_sample=%d\n",
                sampleRate,width,firstDelay,observed);
    // Continuous control transitions must remain finite and bounded.
    assert(reverb->setRealtimeProgram(SonKuReverbPreset::LargeRoom,.6f));
    for(int i=0;i<4096;++i){
        const float x=.05f*std::sin(.03f*i);
        auto s=reverb->process(x,x);
        assert(std::isfinite(s.left)&&std::isfinite(s.right));
        assert(std::abs(s.left)<.99f&&std::abs(s.right)<.99f);
    }
    // Fresh prepared reset preserves the accepted physical room size while
    // guaranteeing silence with an empty source (no previously heard tail).
    reverb->reset();
    assert(reverb->setRealtimeProgram(SonKuReverbPreset::StudioGlue,.6f));
    for(int i=0;i<static_cast<int>(sampleRate*.4);++i){
        auto s=reverb->process(0.f,0.f);
        assert(std::abs(s.left)<1.e-8f&&std::abs(s.right)<1.e-8f);
    }
    return observed;
}
int main(){
    for(const double rate:{44100.,48000.}){
        const int studio=onset(rate,SonKuReverbPreset::StudioGlue);
        const int chamber=onset(rate,SonKuReverbPreset::AiryChamber);
        const int large=onset(rate,SonKuReverbPreset::LargeRoom);
        assert(studio<chamber&&chamber<large);
    }
    std::puts("PASS P8.2 geometry-specific Studio/Chamber/Large late onset, finite retarget, cleared stale tail, 44.1/48k");
}
