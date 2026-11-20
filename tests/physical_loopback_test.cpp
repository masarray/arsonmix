// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/diagnostics/PhysicalLoopback.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using Probe = armix::diagnostics::PhysicalLoopback;
constexpr std::array<int, 2> mainPair {1, 2};

static void require(bool valid, const char* condition)
{
    if (!valid) {
        std::cerr << condition << '\n';
        std::exit(1);
    }
}

static Probe::Capture capture(int rate,int block,int delay,bool echo=false)
{
    Probe probe;
    require(probe.start(rate,block,1,2,2,3,42),"Calibrator admits valid device format");
    std::vector<float> history(static_cast<std::size_t>(rate));
    std::vector<float> unused(block), input(block), left(block), right(block);
    const float* inputs[] {unused.data(),input.data()};
    float* outputs[] {unused.data(),left.data(),right.data()};
    for (int begin=0;probe.state()==Probe::State::capturing;begin+=block) {
        for (int f=0;f<block;++f) {
            const int offset=begin+f-delay;
            const float noise=0.00004f*static_cast<float>(((begin+f)*37)%31-15);
            input[f]=0.001f+noise+(offset>=0?history[offset]*0.3f:0.0f);
            if (echo && offset>=96) input[f]+=history[offset-96]*0.3f;
        }
        require(probe.captureInput(inputs,2,block),"Raw callback capture");
        // Emulate accepted graph writes on memory aliased with an input.
        std::fill(input.begin(),input.end(),0.97f);
        require(probe.emitProbe(outputs,3,block,42,true,mainPair),"Probe writes to accepted MAIN");
        for (int f=0;f<block;++f) {
            require(unused[f]==0 && left[f]==0,"Other physical pins stay silent");
            require(std::abs(right[f])<=Probe::amplitude,"Probe is level-limited");
            history[begin+f]=right[f];
        }
    }
    require(probe.state()==Probe::State::complete,"Bounded capture finishes");
    return probe.take();
}

static void rejectsUnauthorisedBlock(bool permitted,std::uint64_t epoch,
                                      std::array<int,2> pair,bool cancel)
{
    Probe probe;
    float input[64]{},output[64]{};
    const float* inputs[]{input};
    float* outputs[]{output};
    require(probe.start(48000,64,0,0,1,1,42),"Start probe");
    if(cancel) probe.requestCancel();
    require(probe.captureInput(inputs,1,64),"Capture pending block");
    require(!probe.emitProbe(outputs,1,64,epoch,permitted,pair),"Probe must decline output");
    require(probe.state()==Probe::State::cancelled,"Probe cancels instead of resuming");
}

int main()
{
    for (const int rate : {44100,48000,96000}) {
        for (const int block : {64,128,256,512}) {
            const int frames=3*block+19;
            const auto result=Probe::analyse(capture(rate,block,frames));
            require(!result.error && result.frames==frames && result.confidence>0.99,
                    "Known wired sample delay and confidence");
        }
    }
    auto pcm=capture(48000,128,459);
    std::fill(pcm.received.begin(),pcm.received.end(),0.0f);
    require(Probe::analyse(pcm).error,"Silence cannot pass as zero latency");
    for (std::size_t i=0;i<pcm.received.size();++i)
        pcm.received[i]=0.001f*static_cast<float>((i*16127)%3989-1994.0);
    require(Probe::analyse(pcm).error,"Clipped noise must fail");
    for (auto& sample : pcm.received) sample*=0.02f;
    require(Probe::analyse(pcm).error,"Uncorrelated background noise must fail");
    require(Probe::analyse(capture(48000,128,459,true)).error,"Ambiguous doubled/feedback returns fail");
    rejectsUnauthorisedBlock(false,42,{0,-1},false);
    rejectsUnauthorisedBlock(true,43,{0,-1},false);
    rejectsUnauthorisedBlock(true,42,{1,-1},false);
    rejectsUnauthorisedBlock(true,42,{0,-1},true);
    std::cout << "Wired loopback matrix passed\n";
}
