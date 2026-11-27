// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizAutoGain.h"
#include "../engine/builtin_fx/anti_noiz/AntiNoizContext.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using armix::builtin_fx::anti_noiz::AutoGain;
void check(bool ok){if(!ok)std::abort();}
int main(){
    constexpr float floor=.4f;
    check(AutoGain::activeFloor(floor,4.f)==1.f);
    check(std::abs(AutoGain::activeFloor(floor,8.f)-.7f)<1e-6f);
    check(std::abs(AutoGain::activeFloor(floor,12.f)-floor)<1e-6f);
    check(std::abs(AutoGain::target(floor,1.f,1.f)-floor)<1e-6f);
    check(AutoGain::target(floor,3.f,1.f)==1.f);
    check(AutoGain::target(floor,1.f,8.f)==1.f);
    check(AutoGain::target(1.f,1.f,1.f)==1.f);
    check(AutoGain::detailReserve(4.f,.75f)==0.f);
    check(AutoGain::detailReserve(1.f,.75f)==.75f);
    check(std::abs(AutoGain::detailFloor(floor,.75f,0.f)-.85f)<1e-6f);
    check(AutoGain::detailFloor(floor,.75f,1.f)==floor);
    check(AutoGain::detailFloor(1.f,.75f,0.f)==1.f);
    for(int a=0;a<=100;++a)for(int b=0;b<=100;++b){
        const float base=float(a)*.01f,ratio=float(b)*.1f;
        const float reserve=AutoGain::detailReserve(ratio,.75f);
        const float value=AutoGain::detailFloor(base,reserve,ratio);
        check(std::isfinite(value)&&value>=base-1e-6f&&value<=1.f);
        check(value>=AutoGain::detailFloor(base,reserve,ratio+.1f)-1e-6f);
        check(value>=AutoGain::detailFloor(base,reserve*.5f,ratio)-1e-6f);
    }
    check(AutoGain::activeFloor(floor,2.f,5.f)==1.f);
    check(AutoGain::activeFloor(floor,2.f,20.f)<=floor+1e-6f);
    check(AutoGain::activeFloor(floor,12.f,1.f)<=floor+1e-6f);
    check(AutoGain::activeFloor(1.f,100.f,100.f)==1.f);
    for(int t=0;t<100;++t)for(int i=0;i<100;++i) {
        const float p=float(t)*.1f,q=float(i)*.1f;
        const float f=AutoGain::activeFloor(floor,p,q);
        check(f>=floor-1e-6f&&f<=1.f);
        check(f>=AutoGain::activeFloor(floor,p+.1f,q)-1e-6f);
        check(f>=AutoGain::activeFloor(floor,p,q+.1f)-1e-6f);
    }
    // A localized strong source can be hidden by broadband noise energy.
    // Check the real context-to-authority seam, including a quiet other channel.
    using Guard=armix::builtin_fx::anti_noiz::ContextGuard<129>;
    Guard context;context.prepare(48000,128);
    Guard::Powers learned{},observed{};
    for(auto& channel:learned)channel.fill(1.f);
    observed=learned;
    for(int k=0;k<8;++k)observed[1][k]=20.f;
    check(!context.update(observed,learned,2,129,true));
    check(context.broadbandRatio()<4.f&&context.subbandPeakRatio()==20.f);
    check(context.relativeNoise(learned,2,0)==1.f);
    learned[0][0]=.1f;
    context.update(observed,learned,2,129,true);
    check(context.relativeNoise(learned,2,0)<.11f);
    learned[0][0]=1.f;
    check(AutoGain::activeFloor(floor,context.broadbandRatio())==1.f);
    check(AutoGain::activeFloor(floor,context.broadbandRatio(),context.subbandPeakRatio())<=floor+1e-6f);
    observed=learned;
    for(int k=0;k<8;++k)observed[1][k]=4.f;
    context.update(observed,learned,2,129,true);
    check(AutoGain::activeFloor(floor,context.broadbandRatio(),context.subbandPeakRatio())==1.f);
    context.reset();
    check(context.subbandPeakRatio()==0.f);
    for(int a=0;a<=100;++a){
        const float f=.1f+.009f*float(a);
        for(int b=0;b<=100;++b)for(int c=0;c<=100;++c){
            const float t=float(b)*.1f,i=float(c)*.1f;
            const float g=AutoGain::target(f,t,i);
            check(std::isfinite(g)&&g>=f-1e-6f&&g<=1.f);
            check(g<=AutoGain::target(f,t+.1f,i)+1e-6f);
            check(g<=AutoGain::target(f,t,i+.1f)+1e-6f);
        }
    }
    std::cout<<"Auto gain bounds, weak foreground, temporal and onset protection PASS\n";
}
