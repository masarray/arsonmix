// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizAutoGain.h"
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
    check(AutoGain::activeFloor(floor,2.f,5.f)==1.f);
    check(AutoGain::activeFloor(floor,2.f,20.f)<=floor+1e-6f);
    check(AutoGain::activeFloor(floor,12.f,1.f)<=floor+1e-6f);
    check(AutoGain::activeFloor(1.f,100.f,100.f)==1.f);
    check(AutoGain::voiceProtection(1.f,100.f)==0.f);
    check(AutoGain::voiceProtection(100.f,1.f)==0.f);
    check(std::abs(AutoGain::voiceProtection(5.f,3.f)-.85f)<1e-6f);
    for(int t=0;t<100;++t)for(int i=0;i<100;++i) {
        const float p=float(t)*.1f,q=float(i)*.1f;
        const float g=AutoGain::voiceProtection(p,q);
        check(std::isfinite(g)&&g>=0.f&&g<=.85f);
        check(g<=AutoGain::voiceProtection(p+.1f,q)+1e-6f);
        check(g<=AutoGain::voiceProtection(p,q+.1f)+1e-6f);
        const float f=AutoGain::activeFloor(floor,p,q);
        check(f>=floor-1e-6f&&f<=1.f);
        check(f>=AutoGain::activeFloor(floor,p+.1f,q)-1e-6f);
        check(f>=AutoGain::activeFloor(floor,p,q+.1f)-1e-6f);
    }
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
