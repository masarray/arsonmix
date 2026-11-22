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
// Frozen P102 baseline measured response at 48k / independent analytic
// reference at both rates below. Do not loosen these when retuning body.
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
// Independent transfer of the archived 193Hz bell, and the unchanged
// three upper EQ bands. RBJ biquad frequency response, no realtime state.
static double oldBell(int rate,double f,double fc,double q,double gain){
    const double w=2*pi*f/rate,t=2*pi*fc/rate,A=std::pow(10.,gain/40.),
                 a=std::sin(t)/(2*q),c=-2*std::cos(t);
    auto mag=[w](double b0,double b1,double b2){
        return std::hypot(b0+b1*std::cos(w)+b2*std::cos(2*w),
                          -b1*std::sin(w)-b2*std::sin(2*w));
    };
    return 20*std::log10(mag(1+a*A,c,1-a*A)/mag(1+a/A,c,1-a/A));
}
static double oldResponse(int rate,double f){
    const double w=2*pi*f/rate,t=2*pi*3240/rate,A=std::pow(10.,3.3/40.),
                 co=std::cos(t),k=std::sqrt(2*A)*std::sin(t);
    auto mag=[w](double b0,double b1,double b2){
        return std::hypot(b0+b1*std::cos(w)+b2*std::cos(2*w),
                          -b1*std::sin(w)-b2*std::sin(2*w));
    };
    const double hi=20*std::log10(mag(A*((A+1)+(A-1)*co+k),
          -2*A*((A-1)+(A+1)*co),A*((A+1)+(A-1)*co-k))/
          mag((A+1)-(A-1)*co+k,2*((A-1)-(A+1)*co),(A+1)-(A-1)*co-k));
    return oldBell(rate,f,193,.53,2.8)+oldBell(rate,f,1120,1.2,2.3)+
        hi+oldBell(rate,f,7400,1.1,-1.8);
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
        for(int hz:{40,60,86,100,150,193,220,250,300,350,400,500,560,800,1120,2000,3240,7400,10000}){
            p102::FixedPaMusicalEq eq;assert(eq.prepare(rate));double before=0,after=0;
            for(int i=0;i<rate;++i){const float v=float(.04*std::sin(2*pi*hz*i/rate));float l=v,r=-v;eq.process(l,r,1.f);assert(l==-r);if(i>rate/2){before+=double(v)*v;after+=double(l)*l;}}
            const double db=10*std::log10(after/before);
            const double prior=oldResponse(rate,hz),delta=db-prior;
            assert(std::isfinite(db)&&db>-.25&&db<6.);
            if(hz==193)assert(db>4.4 && db<4.8);
            if(hz==220||hz==250||hz==300||hz==350)assert(delta>2.0&&delta<3.1);
            if(hz==250)assert(db>5.4&&db<5.8);
            if(hz<=100)assert(delta>=-.55&&delta<=.1);
            if(hz==500)assert(delta<1.);
            if(hz==800)assert(std::abs(delta)<.3);
            if(hz>=1120)assert(std::abs(delta)<.15);
            if(hz>=2000)assert(std::abs(delta)<.04);
            std::printf("P102_WARM_EQ,%d,%d,%.5f,prior=%.5f,delta=%.5f\n",rate,hz,db,prior,delta);
        }
        p102::FixedPaMusicalEq eq;assert(eq.prepare(rate));
        // Stress the new warmth band's maximum boost, and stereo-linked
        // safety under an asymmetric multi-tone burst. No headroom clipping.
        for(int i=0;i<rate;++i){float l=.98f*std::sin(2*pi*250*i/rate),r=l;eq.process(l,r,1.f);assert(l==r&&std::abs(l)<=.981f);}
        eq.reset();
        for(int i=0;i<rate;++i){
            const float x=static_cast<float>(.6*std::sin(2*pi*250*i/rate)+.3*std::sin(2*pi*1120*i/rate));
            float l=x,r=.5f*x;eq.process(l,r,1.f);
            assert(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<=.981f&&std::abs(r)<=.981f);
            assert(std::abs(r-.5f*l)<1.e-6f);
        }
        std::printf("P102_EXACT_FIXED_PA_PLUS_EQ_PASS,%d\n",rate);
    }
}
