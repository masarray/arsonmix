// SPDX-License-Identifier: AGPL-3.0-only
// Benchmark-only deterministic native core driver; no JUCE or audio-device I/O.
// The A/B script builds this SAME translation unit against two git snapshots.
#include "ChannelCompressor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using armix::builtin_fx::ChannelCompressor;
using armix::builtin_fx::CompressorSettings;
int main() {
    constexpr int channels=16,frames=128,blocks=950;
    struct Scene {const char* name;float threshold,ratio,knee,mix;bool automation;};
    constexpr Scene scenes[]={
        {"active_soft",-38.f,12.f,20.f,1.f,false},
        {"active_hard",-30.f,10.f,0.f,1.f,false},
        {"below_threshold",0.f,20.f,0.f,1.f,false},
        {"mix_dry_meter",-45.f,15.f,12.f,0.f,false},
        {"automation",-38.f,9.f,14.f,.7f,true}
    };
    for(const auto& scene:scenes) {
        std::array<ChannelCompressor,channels> fx{};
        CompressorSettings s{};
        s.thresholdDb=scene.threshold;s.ratio=scene.ratio;
        s.kneeDb=scene.knee;s.mix=scene.mix;
        s.attackMs=1.1f;s.releaseMs=90.f;
        ChannelCompressor::PreparedEdit edits[2]{};
        CompressorSettings alternative=s;
        alternative.thresholdDb=-12.f;alternative.ratio=3.f;
        alternative.kneeDb=6.f;alternative.mix=.25f;
        if(!ChannelCompressor::prepareEdit(s,48000,edits[0])||
           !ChannelCompressor::prepareEdit(alternative,48000,edits[1]))
            return 2;
        for(auto& c:fx)
            if(!c.prepare(48000)||!c.resetUnpublished(s))return 3;
        std::array<float,frames> sourceL{},sourceR{},left{},right{};
        for(int k=0;k<frames;++k) {
            sourceL[k]=static_cast<float>(.48+.12*std::sin(k*.091));
            sourceR[k]=static_cast<float>(-.36-.08*std::cos(k*.11));
        }
        // Warm the standard-library math functions and the core instruction cache.
        for(int w=0;w<24;++w) {
            left=sourceL;right=sourceR;
            fx[0].processStereo(left.data(),right.data(),frames);
        }
        double checksum=0.;
        const auto start=std::chrono::steady_clock::now();
        for(int block=0;block<blocks;++block) {
            for(int lane=0;lane<channels;++lane) {
                auto& c=fx[static_cast<std::size_t>(lane)];
                if(scene.automation && block%7==0 && 
                   !c.applyPrepared(edits[(block/7)%2]))return 4;
                left=sourceL;right=sourceR;
                c.processStereo(left.data(),right.data(),frames);
                checksum+=left[0]*.25+right[frames-1]*.5+
                          c.gainReductionDb()+c.inputPeak()*.1+
                          c.outputPeak()*.1;
            }
        }
        const auto ns=std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-start).count();
        if(!std::isfinite(checksum)||ns<=0.)return 5;
        std::printf("SCENE %s %.6f %.12f\n",scene.name,
                    ns/(channels*frames*blocks),checksum);
    }
}
