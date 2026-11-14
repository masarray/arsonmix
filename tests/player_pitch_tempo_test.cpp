// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PlayerPitchTempoProcessor.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <vector>
#include <cmath>
#include <numeric>
#include <chrono>
#include <cstdio>
#include <algorithm>
namespace {
constexpr int rate=48000,frames=48000;
constexpr double pi=3.14159265358979323846;
using Processor=armix::player::PlayerPitchTempoProcessor;
struct Evidence {double hz=0,ratio=0,correlation=0;};
Evidence test(Processor::Controls c) {
    Processor processor;
    assert(processor.prepare(rate,c));
    std::vector<float> inputL(frames),inputR(frames),outL,outR;
    for(int i=0;i<frames;++i) {
        const float v=static_cast<float>(.45*std::sin(2*pi*440.*i/rate));
        inputL[i]=v;inputR[i]=-v; // 2 real lanes, opposite phase
    }
    int read=0;
    constexpr int quantum=512;
    std::array<float,quantum> left{},right{};
    while(read<frames) {
        const int inFrames=processor.nextInputFrames(quantum);
        if(read+inFrames>frames)break;
        const float* const inputs[]={inputL.data()+read,inputR.data()+read};
        float* const outputs[]={left.data(),right.data()};
        assert(processor.process(inputs,inFrames,outputs,quantum));
        outL.insert(outL.end(),left.begin(),left.end());
        outR.insert(outR.end(),right.begin(),right.end());
        read+=inFrames;
    }
    const double expectedOutput=frames*100.0/c.tempoPercent;
    assert(std::abs(static_cast<double>(outL.size())-expectedOutput)<1025);
    assert(outL.size()==outR.size());
    const int start=static_cast<int>(std::min<std::size_t>(
        outL.size()/2,20000));
    const int count=std::min(8192,static_cast<int>(outL.size())-start-1);
    assert(count>=4000);
    int crossings=0;
    double power=0,xy=0,x2=0,y2=0;
    for(int i=0;i<count;++i) {
        const double x=outL[start+i],y=outR[start+i];
        assert(std::isfinite(x)&&std::isfinite(y));
        power+=x*x;xy+=x*y;x2+=x*x;y2+=y*y;
        if(i>0 && outL[start+i-1]<=0 && x>0)++crossings;
    }
    assert(power>10); // nonzero actual pitch-shifted backing PCM
    const double hz=rate*crossings/static_cast<double>(count);
    const double correlation=xy/std::sqrt(x2*y2);
    const double expectedHz=440.*std::pow(2.,c.keySemitones/12.);
    assert(std::abs(hz-expectedHz)<18);
    assert(correlation<-.9); // phase coherence of linked stereo shift
    return {hz,static_cast<double>(outL.size())/frames,correlation};
}
// Simulate a real performer changing the key after a long dry passage, then
// changing tempo, bypassing the DSP and re-entering at a different pitch.
// Previous unit coverage only checked constant controls on synthetic sine.
void testLiveControlTransitions(int sampleRate) {
    Processor processor;
    constexpr int quantum=512;
    assert(processor.prepare(sampleRate,{0,100}));
    std::array<float,768> sourceL{},sourceR{};
    std::array<float,quantum> resultL{},resultR{};
    std::int64_t sourceFrame=0;
    int longestSilentRun=0,silentRun=0;
    double recentEnergy=0;
    double processTimeMs=0;
    for(int block=0;block<192;++block) {
        if(block==32)processor.setControls({4,100}); // bypass -> transposed
        if(block==64)processor.setControls({4,88});  // independent tempo
        if(block==96)processor.setControls({0,100}); // DSP -> bit-exact dry
        if(block==128)processor.setControls({-3,112}); // bypass -> DSP again
        if(block==160)processor.setControls({-3,100}); // active tempo change
        const int nInput=processor.nextInputFrames(quantum);
        assert(nInput>0 && nInput<=static_cast<int>(sourceL.size()));
        for(int i=0;i<nInput;++i) {
            const double time=static_cast<double>(sourceFrame+i)/sampleRate;
            const float sample=static_cast<float>(
                0.36*std::sin(2*pi*220*time)+
                0.14*std::sin(2*pi*660*time)+
                0.07*std::sin(2*pi*1760*time));
            sourceL[i]=sample;
            sourceR[i]=-sample;
        }
        sourceFrame+=nInput;
        const float* const inputs[]={sourceL.data(),sourceR.data()};
        float* const outputs[]={resultL.data(),resultR.data()};
        const auto started=std::chrono::steady_clock::now();
        assert(processor.process(inputs,nInput,outputs,quantum));
        processTimeMs+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        for(int i=0;i<quantum;++i) {
            const float x=resultL[i],y=resultR[i];
            assert(std::isfinite(x)&&std::isfinite(y));
            assert(std::abs(x+y)<0.02f); // linked stereo phase coherence
            if(block>12) {
                if(std::abs(x)<0.00001f) {
                    ++silentRun;
                    longestSilentRun=std::max(longestSilentRun,silentRun);
                } else silentRun=0;
            }
            if(block==47 || block==79 || block==111 ||
               block==143 || block==175)recentEnergy+=x*x;
        }
    }
    // A cold 120ms FFT analysis window on every dry->DSP transition would
    // fail this, even though a constant-pitch frequency test still passes.
    assert(longestSilentRun<sampleRate/12);
    assert(recentEnergy>5.0);
    assert(!processor.process(nullptr,0,nullptr,512));
    const double audioTimeMs=192.0*quantum*1000.0/sampleRate;
    std::printf("M2 hot-gesture: %d Hz, silence %d frames, DSP %.1fms / %.1fms audio (CI reference)\n",
                sampleRate,longestSilentRun,processTimeMs,audioTimeMs);
}
}
int main() {
    using P=Processor;
    double rem=0;
    assert(!P::valid({-7,100}));
    assert(!P::valid({3,125}));
    assert(P::valid({-6,80})&&P::valid({6,120}));
    for(int i=0,total=0;i<1000;++i)
        total+=P::inputFramesForOutput(512,{1,87},rem);
    assert(std::abs(rem)<1 && rem>=0);
    assert(P::inputFramesForOutput(0,{0,100},rem)==0);
    const auto neutral=test({0,100});
    const auto up=test({3,80});
    const auto down=test({-2,120});
    assert(std::abs(neutral.hz-440)<8);
    assert(up.hz>neutral.hz && down.hz<neutral.hz);
    assert(up.ratio>1.24 && down.ratio<.85);
    testLiveControlTransitions(44100);
    testLiveControlTransitions(48000);
    testLiveControlTransitions(96000);
    std::printf("M2: pass neutral %.1fHz, +3st/80%% %.1fHz, -2st/120%% %.1fHz; stereo=%.3f\n",
                neutral.hz,up.hz,down.hz,up.correlation);
}
