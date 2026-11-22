// SPDX-License-Identifier: AGPL-3.0-only
#undef NDEBUG
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include "../engine/builtin_fx/sonku/SonKuLyricFrontPocket.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <memory>
#include <array>
using namespace armix::sonku;
using namespace armix::builtin_fx;
constexpr double pi=3.14159265358979323846;
static double db(double x){return 10.*std::log10(std::max(x,1.e-30));}
static float tone(int rate,double frequency){
    p100::LyricFrontPocket p;assert(p.prepare(rate));
    double in=0.,out=0.;
    for(int i=0;i<rate;++i){
        const float x=.1f*float(std::sin(2*pi*frequency*i/rate));
        const auto y=p.process(x,x,true);
        assert(y.left==y.right && std::isfinite(y.left));
        if(i>rate/2){in+=double(x)*x;out+=double(y.left)*y.left;}
    }
    return float(db(out/in));
}
static void unit(int rate){
    assert(std::abs(tone(rate,1800.)+2.f)<.02f);
    assert(std::abs(tone(rate,200.))<.15f);
    assert(std::abs(tone(rate,100.))<.05f);
    assert(tone(rate,1000.)<-.8f && tone(rate,3000.)<-.8f);
    assert(tone(rate,10000.)>-.3f);
    p100::LyricFrontPocket p;assert(p.prepare(rate));
    for(int i=0;i<rate;++i){
        const float x=.125f*float(std::sin(.21*i));
        auto y=p.process(x,-x,true);
        assert(y.left==x && y.right==-x);
        y=p.process(x,x,false); // exercise both transitions
        assert(std::isfinite(y.left));
    }
    p.reset();auto y=p.process(.12f,-.08f,false);
    assert(y.left==.12f && y.right==-.08f);
    std::printf("POCKET_FILTER,%d,200=%.3f,1000=%.3f,1800=%.3f,3000=%.3f,10000=%.3f\n",rate,tone(rate,200),tone(rate,1000),tone(rate,1800),tone(rate,3000),tone(rate,10000));
}
struct Render { std::vector<float> l,r; SonKuSpatialCore::PocketTrace trace; double c50; };
static Render render(int rate,Settings s,bool on,int pattern,int block){
    auto dsp=std::make_unique<SonKuSpatialCore>();
    dsp->optInImmerSIBVocalRoom();
    if(on)dsp->optInImmerSIBLyricPocket();
    assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(dsp->setSettings(s));
    const int n=32768;
    Render result{std::vector<float>(n),std::vector<float>(n),{},0.};
    // First 8192 frames settle scene parameters (not part of impulse energy).
    for(int i=0;i<n;++i){
        const double t=double(i)/rate;
        const float m=.04f*float(std::sin(2*pi*1800*t)+.7*std::sin(2*pi*250*t));
        const float v=.03f*float(std::sin(2*pi*3100*t)+.6*std::sin(2*pi*700*t));
        if(pattern==0){result.l[i]=m+v;result.r[i]=m-v;}
        if(pattern==1){result.l[i]=m+v;result.r[i]=-(m+v);}
        if(pattern==2){result.l[i]=m+v;result.r[i]=0.f;}
        if(pattern==3 && i==8192){result.l[i]=result.r[i]=.12f;}
    }
    for(int i=0;i<n;i+=block)dsp->process(result.l.data()+i,result.r.data()+i,block);
    result.trace=dsp->pocketTrace();
    double first=0.,tail=0.;
    for(int i=0;i<n;++i){
        const float l=result.l[i],r=result.r[i];
        assert(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<=.9805f&&std::abs(r)<=.9805f);
        if(pattern==3 && i>=8192){
            double e=double(l)*l+double(r)*r;
            (i<8192+int(rate*.05)?first:tail)+=e;
        }
    }
    result.c50=db(first/std::max(tail,1.e-30));
    return result;
}
int main(){
    for(int rate:{44100,48000}){
        unit(rate);
        for(const auto& scene:imersipP92Presets){
            auto s=imersipP92Factory(scene.id);
            for(int pattern=0;pattern<4;++pattern){
                auto a=render(rate,s,false,pattern,128);
                auto b=render(rate,s,true,pattern,128);
                auto c=render(rate,s,true,pattern,256);
                assert(b.l==c.l && b.r==c.r);
                // Upstream fields unaffected, regardless of late return.
                assert(a.trace.direct==b.trace.direct && a.trace.early==b.trace.early);
                assert(std::abs(a.trace.lateSide-b.trace.lateSide)<1.e-5*std::max(a.trace.lateSide,1.e-9));
                if(s.sceneVoicing!=4 && s.sceneVoicing!=5)assert(a.l==b.l && a.r==b.r);
                if(pattern==0 && (s.sceneVoicing==4 || s.sceneVoicing==5)){
                    assert(a.l!=b.l && a.trace.lateMid>b.trace.lateMid);
                    std::printf("POCKET_LATE,%d,%d,mid_delta_db=%.4f\n",rate,scene.id,db(b.trace.lateMid/a.trace.lateMid));
                }
                if(pattern==3)std::printf("POCKET_IMPULSE_C50_PROXY,%d,%d,base=%.3f,candidate=%.3f\n",rate,scene.id,a.c50,b.c50);
            }
        }
        auto s=imersipP92Factory(2);
        s.spatialMix=0.f;
        auto a=render(rate,s,false,0,128), b=render(rate,s,true,0,128);
        assert(a.l==b.l && a.r==b.r);
        s=imersipP92Factory(2);s.spacePreset=0;
        a=render(rate,s,false,0,128);b=render(rate,s,true,0,128);
        assert(a.l==b.l && a.r==b.r);
        std::printf("LYRIC_POCKET_FULL_PCM,%d,PASS\n",rate);
    }
}
