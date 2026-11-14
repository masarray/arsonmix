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
    float previousOutput=0.f;
    float worstControlSeam=0.f;
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
        if(block==32 || block==64 || block==96 ||
           block==128 || block==160)
            worstControlSeam=std::max(worstControlSeam,
                                     std::abs(resultL[0]-previousOutput));
        previousOutput=resultL[quantum-1];
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
    // A ~0.47 FS one-sample jump was measured before seam correction.
    // The boundary must now be continuous to within float rounding.
    assert(worstControlSeam<0.02f);
    assert(recentEnergy>5.0);
    assert(!processor.process(nullptr,0,nullptr,512));
    const double audioTimeMs=192.0*quantum*1000.0/sampleRate;
    std::printf("M2 hot-gesture: %d Hz, silence %d frames, seam %.3f, DSP %.1fms / %.1fms audio (CI reference)\n",
                sampleRate,longestSilentRun,worstControlSeam,processTimeMs,audioTimeMs);
}
// Offline fixed-file QA: a spectral stretcher must receive inputLatency()
// zero-input frames and then flush outputLatency() frames. Verifies song
// ending isn't lost after pitch-shifting or time-stretching a full mix.
// Tempo edits should use a bounded, sample-rate-independent transition
// instead of hard-switching source consumption from 80% to 120% in one block.
void testSmoothTempoEdits(int sampleRate) {
    Processor processor;
    assert(processor.prepare(sampleRate,{0,100}));
    constexpr int quantum=512;
    processor.setControls({0,80});
    double prior=100.;
    for(int block=0;block<48;++block) {
        const int read=processor.nextInputFrames(quantum);
        assert(read>0 && read<=quantum);
        const double current=processor.smoothedTempoPercent();
        assert(current<=prior+1e-7 && current>=80.);
        if(block==0)assert(current>80. && current<100.);
        prior=current;
    }
    assert(std::abs(prior-80.)<0.05);
    processor.setControls({0,120});
    prior=80.;
    for(int block=0;block<48;++block) {
        const int read=processor.nextInputFrames(quantum);
        assert(read>0 && read<=static_cast<int>(quantum*1.2)+1);
        const double current=processor.smoothedTempoPercent();
        assert(current>=prior-1e-7 && current<=120.);
        if(block==0)assert(current>80. && current<120.);
        prior=current;
    }
    assert(std::abs(prior-120.)<0.05);
    processor.setControls({0,100});
    assert(processor.nextInputFrames(quantum)==quantum);
    assert(processor.smoothedTempoPercent()==100.);
}
void testNeutralBitExact() {
    Processor processor;
    assert(processor.prepare(48000,{0,100}));
    constexpr int n=257;
    std::array<float,n> inL{},inR{},outL{},outR{};
    for(int i=0;i<n;++i) {
        inL[i]=static_cast<float>(.71*std::sin(i*.19));
        inR[i]=static_cast<float>(.31*std::cos(i*.23));
    }
    const float* const in[]={inL.data(),inR.data()};
    float* const out[]={outL.data(),outR.data()};
    assert(processor.process(in,n,out,n));
    for(int i=0;i<n;++i) {
        assert(outL[i]==inL[i] && outR[i]==inR[i]);
    }
    assert(processor.inputLatency()==0 && processor.outputLatency()==0);
}
void testEndOfSongTail(int sampleRate,Processor::Controls controls) {
    Processor processor;
    assert(processor.prepare(sampleRate,controls));
    const int totalInput=sampleRate/3;
    constexpr int quantum=512;
    std::array<float,768> sourceL{},sourceR{};
    std::array<float,quantum> outputL{},outputR{};
    int fed=0;
    std::int64_t produced=0;
    auto energy=[](const float* left,const float* right,int count) {
        double sum=0;
        for(int i=0;i<count;++i) {
            assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            assert(std::abs(left[i]+right[i])<0.05f);
            sum+=left[i]*left[i];
        }
        return sum;
    };
    while(fed<totalInput) {
        int nOutput=quantum;
        int nInput=processor.nextInputFrames(nOutput);
        assert(nInput>0 && nInput<=static_cast<int>(sourceL.size()));
        if(nInput>totalInput-fed) {
            nOutput=std::min(quantum,static_cast<int>(std::ceil(
                (totalInput-fed)*100.0/controls.tempoPercent)));
            nInput=totalInput-fed;
        }
        for(int i=0;i<nInput;++i) {
            const double t=(fed+i)*2*pi/sampleRate;
            const float v=static_cast<float>(0.35*std::sin(330*t)
                       +0.11*std::sin(790*t)
                       +((fed+i)>totalInput-sampleRate/16
                         ?0.2*std::sin(1930*t):0.0));
            sourceL[i]=v;sourceR[i]=-v;
        }
        const float* const input[]={sourceL.data(),sourceR.data()};
        float* const output[]={outputL.data(),outputR.data()};
        assert(processor.process(input,nInput,output,nOutput));
        energy(outputL.data(),outputR.data(),nOutput);
        fed+=nInput;produced+=nOutput;
    }
    const int analysisDelay=processor.inputLatency();
    const int synthesisDelay=processor.outputLatency();
    assert(analysisDelay>0 && synthesisDelay>0);
    assert(analysisDelay<sampleRate && synthesisDelay<sampleRate);
    int remaining=analysisDelay;
    double recoveredTailEnergy=0;
    while(remaining>0) {
        const int nOutput=std::min(quantum,std::max(1,
            static_cast<int>(std::ceil(
                remaining*100.0/controls.tempoPercent))));
        int nInput=processor.nextInputFrames(nOutput);
        nInput=std::clamp(nInput,1,remaining);
        sourceL.fill(0.f);sourceR.fill(0.f);
        const float* const input[]={sourceL.data(),sourceR.data()};
        float* const output[]={outputL.data(),outputR.data()};
        assert(processor.process(input,nInput,output,nOutput));
        recoveredTailEnergy+=energy(outputL.data(),outputR.data(),nOutput);
        remaining-=nInput;produced+=nOutput;
    }
    std::vector<float> lastL(synthesisDelay),lastR(synthesisDelay);
    float* const ends[]={lastL.data(),lastR.data()};
    processor.flush(ends,synthesisDelay);
    recoveredTailEnergy+=energy(ends[0],ends[1],synthesisDelay);
    produced+=synthesisDelay;
    const double expected=totalInput*100.0/controls.tempoPercent+
        analysisDelay*100.0/controls.tempoPercent+synthesisDelay;
    assert(std::abs(produced-expected)<quantum*2);
    assert(recoveredTailEnergy>0.01); // last tones survive the ending
    std::printf("M2 EOF: %d Hz, KEY %+d, tempo %d%%, recovered %.2f energy, delay %d+%d frames\n",
                sampleRate,controls.keySemitones,controls.tempoPercent,
                recoveredTailEnergy,analysisDelay,synthesisDelay);
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
    testNeutralBitExact();
    testSmoothTempoEdits(44100);
    testSmoothTempoEdits(48000);
    testSmoothTempoEdits(96000);
    testLiveControlTransitions(44100);
    testLiveControlTransitions(48000);
    testLiveControlTransitions(96000);
    testEndOfSongTail(44100,{3,80});
    testEndOfSongTail(48000,{-4,120});
    testEndOfSongTail(96000,{4,100});
    std::printf("M2: pass neutral %.1fHz, +3st/80%% %.1fHz, -2st/120%% %.1fHz; stereo=%.3f\n",
                neutral.hz,up.hz,down.hz,up.correlation);
}
