// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include "../engine/builtin_fx/sonku/SonKuLyricFrontPocket.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
constexpr double pi=3.14159265358979323846;
using Audio=std::array<std::vector<float>,2>;
static double gain(int rate,double hz){
    p100::LyricFrontPocket p;assert(p.prepare(rate));
    double x2=0,y2=0;
    for(int i=0;i<rate;++i){
        float x=float(.1*std::sin(2*pi*hz*i/rate));auto y=p.process(x,x);
        assert(y.left==y.right);
        if(i>rate/2){x2+=double(x)*x;y2+=double(y.left)*y.left;}
    }return 10*std::log10(y2/x2);
}
static Audio render(int rate,int scene,bool on,int block,bool off,bool mix0,bool impulse){
    auto c=std::make_unique<SonKuSpatialCore>();c->optInImmerSIBVocalRoom();
    if(on)c->optInImmerSIBLyricPocket();
    assert(c->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=armix::builtin_fx::imersipP92Factory(scene);
    if(off)s.spacePreset=0;
    if(mix0)s.spatialMix=0;
    assert(c->setSettings(s));
    int n=impulse?rate:8192;
    Audio a{std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i){
        double t=double(i)/rate;
        float m=float(.08*std::sin(2*pi*311*t)+.025*std::sin(2*pi*1800*t));
        float d=float(.03*std::sin(2*pi*3200*t));
        a[0][i]=impulse?(i==0?.01f:0.f):m+d;
        a[1][i]=impulse?(i==0?.01f:0.f):m-d;
    }
    for(int i=0;i<n;i+=block)c->process(a[0].data()+i,a[1].data()+i,std::min(block,n-i));
    for(auto& ear:a)for(float v:ear)assert(std::isfinite(v)&&std::abs(v)<=.981f);
    return a;
}
int main(){
    for(int rate:{44100,48000}){
        assert(std::abs(gain(rate,1800)+2.)<.02);
        assert(std::abs(gain(rate,86))<.06);
        assert(std::abs(gain(rate,200))<.18);
        assert(gain(rate,1000)<-.8 && gain(rate,3200)<-.8);
        p100::LyricFrontPocket p;assert(p.prepare(rate));
        for(int i=0;i<rate;++i){
            float l=float(.1*std::sin(i*.19));auto a=p.process(l,-l);
            assert(a.left==l && a.right==-l);
            float r=float(.03*std::cos(i*.31));a=p.process(l,r);
            assert(std::abs((a.left-a.right)-(l-r))<1.e-7f);
            p.reset();
        }
        for(int scene:{2,3,4,5,6,7,8}){
            auto a=render(rate,scene,false,128,false,false,false);
            auto b=render(rate,scene,true,128,false,false,false);
            assert(b==render(rate,scene,true,256,false,false,false));
            assert(render(rate,scene,false,128,true,false,false)==render(rate,scene,true,128,true,false,false));
            assert(render(rate,scene,false,128,false,true,false)==render(rate,scene,true,128,false,true,false));
            double delta=0,e=0;for(int k=0;k<2;++k)for(int i=0;i<8192;++i){double d=a[k][i]-b[k][i];delta+=d*d;e+=double(a[k][i])*a[k][i];}
            assert(delta<.04*e+1.e-12);
            if(scene==2)assert(delta>1.e-10);
            std::printf("P100_PCM,%d,%d,relative_delta=%.6g\n",rate,scene,std::sqrt(delta/std::max(e,1.e-20)));
        }
        auto a=render(rate,2,false,128,false,false,true);
        auto b=render(rate,2,true,128,false,false,true);
        // FDN starts around 44 ms. Compare PRE-FDN output exactly.
        for(int ear=0;ear<2;++ear)for(int i=0;i<rate*30/1000;++i)assert(a[ear][i]==b[ear][i]);
        std::printf("P100_PASS,%d\n",rate);
    }
}
