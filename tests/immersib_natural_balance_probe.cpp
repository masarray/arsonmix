// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>
using namespace armix::sonku;
constexpr double pi=3.14159265358979323846;
constexpr std::array<int,15> bands{100,140,194,280,400,560,800,1120,1600,2240,2800,4000,5600,8000,10000};
using Stereo=std::array<std::vector<float>,2>;
static Stereo measure(int rate,int scene,int mode,bool corrected,int block,bool dry=false,bool roomOff=false){
    auto c=std::make_unique<SonKuSpatialCore>();c->optInImmerSIBStageMonitor();
#if defined(ARMIX_IMMERSIB_NATURAL_BALANCE_RESEARCH) && ARMIX_IMMERSIB_NATURAL_BALANCE_RESEARCH
    if(corrected)c->optInImmerSIBNaturalBalance();
#else
    assert(!corrected);
#endif
    assert(c->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=armix::builtin_fx::imersipP92Factory(scene>=20?2:scene);
    if(scene==20)s.sourceDistanceMeters=1.1f;
    if(scene==21)s.sourceDistanceMeters=2.5f;
    if(dry)s.spatialMix=0.f;
    if(roomOff){s.spacePreset=0;s.roomAmount=0;s.ambience=0;}
    assert(c->setSettings(s));
    const int n=rate+rate/2;
    Stereo x{std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i){
        double sum=0;
        for(unsigned j=0;j<bands.size();++j)
            sum+=.0025*std::sin(2*pi*bands[j]*i/rate+pi*j*(j-1)/bands.size());
        x[0][i]=float(sum);
        x[1][i]=mode==0?float(sum):mode==1?0.f:-float(sum);
    }
    for(int i=0;i<n;i+=block)c->process(x[0].data()+i,x[1].data()+i,std::min(block,n-i));
    for(auto& ear:x)for(float v:ear)assert(std::isfinite(v)&&std::abs(v)<.5f);
    return x;
}
int main(int argc,char** argv){
    const bool corrected=argc==2&&std::string_view(argv[1])=="--candidate";
    std::puts("rate,scene,mode,hz,db,lre,lim,rre,rim");
    for(int rate:{44100,48000}){
        if(corrected){
            assert(measure(rate,2,0,true,128)==measure(rate,2,0,true,256));
            // Steady-state MIX0 / ROOM OFF parity. First second contains
            // existing scene transitions; compare the settled last half.
            for(int k=0;k<2;++k){
                auto a=measure(rate,2,0,false,128,k==0,k==1);
                auto b=measure(rate,2,0,true,128,k==0,k==1);
                for(int ear=0;ear<2;++ear)for(int i=rate;i<rate+rate/2;++i)
                    assert(std::abs(a[ear][i]-b[ear][i])<1.e-7f);
            }
        }
        for(int scene:{2,3,4,5,6,7,8,20,21})for(int mode:{0,1,2}){
            const auto x=measure(rate,scene,mode,corrected,128);
            for(int hz:bands){
                double re[2]{},im[2]{};
                for(int i=rate;i<rate+rate/2;++i){
                    const double phase=2*pi*hz*i/rate,co=std::cos(phase),si=std::sin(phase);
                    for(int ear=0;ear<2;++ear){re[ear]+=x[ear][i]*co;im[ear]-=x[ear][i]*si;}
                }
                for(int ear=0;ear<2;++ear){re[ear]*=4./rate;im[ear]*=4./rate;}
                const double power=re[0]*re[0]+im[0]*im[0]+re[1]*re[1]+im[1]*im[1];
                const double db=10*std::log10(std::max(power,1.e-20)/(.0025*.0025*(mode==1?1.:2.)));
                std::printf("%d,%d,%d,%d,%.9f,%.12g,%.12g,%.12g,%.12g\n",rate,scene,mode,hz,db,re[0],im[0],re[1],im[1]);
            }
        }
    }
}
