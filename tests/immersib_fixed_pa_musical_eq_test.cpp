// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include "../engine/builtin_fx/sonku/SonKuFixedPaMusicalEq.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
using Stereo=std::array<std::vector<float>,2>;
constexpr double pi=3.14159265358979323846;
static Stereo render(int rate,int scene,int mode,bool on,int block,bool dry=false,bool roomOff=false){
    auto c=std::make_unique<SonKuSpatialCore>();c->optInImmerSIBStageMonitor();
    if(on)c->optInImmerSIBMusicalEq();
    assert(c->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=armix::builtin_fx::imersipP92Factory(scene);
    if(dry)s.spatialMix=0;
    if(roomOff){s.spacePreset=0;s.roomAmount=0;s.ambience=0;}
    assert(c->setSettings(s));c->reset();
    const int n=rate/2;
    Stereo x{std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i){
        const float v=float(.008*std::sin(2*pi*193*i/rate)+.006*std::sin(2*pi*1120*i/rate)+.004*std::sin(2*pi*9000*i/rate));
        const float d=float(.005*std::sin(2*pi*3137*i/rate));
        x[0][i]=v+d;x[1][i]=mode==0?v+d:mode==1?0.f:-(v+d);
    }
    for(int i=0;i<n;i+=block)c->process(x[0].data()+i,x[1].data()+i,std::min(block,n-i));
    for(auto& a:x)for(float v:a)assert(std::isfinite(v)&&std::abs(v)<=.981f);
    return x;
}
int main(){
    for(int rate:{44100,48000}){
        for(int scene:{2,3,4,5,6,7,8})for(int mode:{0,1,2}){
            auto baseline=render(rate,scene,mode,false,128);
            auto actual=render(rate,scene,mode,true,128);
            assert(actual==render(rate,scene,mode,true,256));
            p102::FixedPaMusicalEq eq;assert(eq.prepare(rate));
            // Independently compose exact Fixed PA output + standalone EQ.
            // No differences permitted in spatial engine before this stage.
            for(size_t i=0;i<baseline[0].size();++i)eq.process(baseline[0][i],baseline[1][i],1.f);
            assert(actual==baseline);
        }
        for(int k=0;k<2;++k)assert(render(rate,2,0,false,128,k==0,k==1)==render(rate,2,0,true,128,k==0,k==1));
        for(int hz:{86,193,560,1120,3240,7400,10000}){
            p102::FixedPaMusicalEq eq;assert(eq.prepare(rate));double before=0,after=0;
            for(int i=0;i<rate;++i){const float v=float(.04*std::sin(2*pi*hz*i/rate));float l=v,r=-v;eq.process(l,r,1.f);assert(l==-r);if(i>rate/2){before+=double(v)*v;after+=double(l)*l;}}
            const double db=10*std::log10(after/before);
            assert(std::isfinite(db)&&db>-.25&&db<5.);
            if(hz==193)assert(db>2.5);
            if(hz==1120)assert(db>2.);
            std::printf("P102_EQ,%d,%d,%.5f\n",rate,hz,db);
        }
        p102::FixedPaMusicalEq eq;assert(eq.prepare(rate));
        for(int i=0;i<rate;++i){float l=.98f*std::sin(2*pi*193*i/rate),r=l;eq.process(l,r,1.f);assert(l==r&&std::abs(l)<=.981f);}
        std::printf("P102_EXACT_FIXED_PA_PLUS_EQ_PASS,%d\n",rate);
    }
}
