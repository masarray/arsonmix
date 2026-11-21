// SPDX-License-Identifier: AGPL-3.0-only
// Realtime core integration: opt-in ImmerSIB vs exact same source without opt-in.
// This is synthetic PCM evidence, NOT headphone/ASIO field listening.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using armix::builtin_fx::imersipP92Factory;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using Stereo=std::array<std::vector<float>,2>;
constexpr double pi=3.1415926535897932384626433832795;

static Stereo run(int rate,int scene,int material,bool candidate,
                  int block=128,bool stageP94=true,bool withRoom=true) {
    // 6144 frames covers direct, wall/floor/ceiling ER and the first
    // audible 44ms+ late FDN; prior 2048-frame test ended before LATE.
    constexpr int frames=6144;
    auto c=std::make_unique<SonKuSpatialCore>();
    c->optInImmerSIBVocalRoom();
    if(candidate)c->optInImmerSIBConcertCandidate(stageP94);
    assert(c->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=imersipP92Factory(scene);
    if(!withRoom) {
        s.spacePreset=0;
        s.roomAmount=0.f;
        s.ambience=0.f;
    }
    assert(c->setSettings(s));
    Stereo pcm{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i){
        const double t=double(i)/rate;
        const float voice=float(.048*std::sin(2*pi*250*t)+
                                .045*std::sin(2*pi*1050*t)+
                                .025*std::sin(2*pi*4900*t));
        const float guitar=float(.04*std::sin(2*pi*2050*t)+
                                 .03*std::sin(2*pi*3450*t));
        const float bass=float(.065*std::sin(2*pi*86*t));
        if(material==0){ // finished stereo master: voice center + panned guitar
            pcm[0][i]=voice+guitar+bass;
            pcm[1][i]=voice+.25f*guitar+bass;
        }else if(material==1){ // strict correlated center
            pcm[0][i]=voice+bass;pcm[1][i]=voice+bass;
        }else if(material==2){ // hard left
            pcm[0][i]=voice+guitar;pcm[1][i]=0.f;
        }else{ // antiphase stereo detail
            pcm[0][i]=guitar;pcm[1][i]=-guitar;
        }
    }
    for(int i=0;i<frames;i+=block)
        c->process(pcm[0].data()+i,pcm[1].data()+i,std::min(block,frames-i));
    for(int i=0;i<frames;++i)
        for(int ear=0;ear<2;++ear){
            assert(std::isfinite(pcm[ear][i]));
            assert(std::abs(pcm[ear][i])<=.981f);
        }
    return pcm;
}
static double energy(const Stereo& a){
    double total=0.;
    for(size_t i=0;i<a[0].size();++i)
        for(int ear=0;ear<2;++ear)total+=double(a[ear][i])*a[ear][i];
    return total;
}
static double difference(const Stereo& a,const Stereo& b){
    double total=0.;
    for(size_t i=0;i<a[0].size();++i)
        for(int ear=0;ear<2;++ear){
            const double d=double(a[ear][i])-b[ear][i];
            total+=d*d;
        }
    return total;
}
int main() {
    for(int rate:{44100,48000}) {
        for(int scene:{2,3,4,5,6,7,8}) {
            for(int material:{0,1,2,3}) {
                auto legacy=run(rate,scene,material,false);
                auto candidate=run(rate,scene,material,true);
                auto differentBlock=run(rate,scene,material,true,256);
                assert(difference(candidate,differentBlock)==0.0);
                const double a=energy(legacy),b=energy(candidate);
                assert(a>1.e-8&&b>1.e-8);
                // Broad corruption/limiter safety, not a loudness-quality claim.
                assert(b/a>.30&&b/a<3.0);
                if(scene==2&&material==0)
                    assert(difference(legacy,candidate)>1.e-6);
                if(material==0) {
                    // SAME opt-in P90 and SAME entire output render graph:
                    // the only v4-v5 difference is P94's real dual-PA
                    // EARLY/DIRECT control. Prove this in FULL PCM, not
                    // merely in P94's isolated gain-coefficient fixture.
                    const auto v4=run(rate,scene,material,true,128,false);
                    assert(difference(v4,candidate)>1.e-8);
                    const double ratio=energy(candidate)/energy(v4);
                    assert(ratio>.60 && ratio<1.60);
                }
            }
        }
        // ROOM OFF makes P94 unity for BOTH v4 and v5; the same P90
        // center reconstruction MUST render strict bit-identical PCM.
        const auto dryV4=run(rate,2,0,true,128,false,false);
        const auto dryV5=run(rate,2,0,true,128,true,false);
        assert(difference(dryV4,dryV5)==0.);
        std::printf("IMMERSIB_CANDIDATE_NATIVE_CORE_7_SCENES,%d,PASS\n",rate);
    }
    std::puts("PASS P9 core integration: 7 scenes x full music/center/hard-L/anti-phase x 44.1/48k, block parity");
}
