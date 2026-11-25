// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizProfile.h"
#include <cstdlib>
#include <iostream>
using Profile=armix::builtin_fx::anti_noiz::NoiseProfile<129>;
void check(bool value,const char* reason){if(!value){std::cerr<<reason<<'\n';std::exit(1);}}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}){
        const int hop=rate>48000?256:128;
        Profile p;p.prepare(rate,hop,2,129);Profile::Powers noise{};
        noise[0].fill(.1f);noise[1].fill(.0001f);
        check(p.begin(),"start learn");
        auto old=noise;old[1].fill(100.f);
        for(int i=0;i<2;++i)check(p.observe(old)==Profile::LearnResult::waiting,"discard pre-command overlap");
        const int frames=int(std::ceil(2.*rate/hop));
        for(int i=0;i<frames;++i)check(p.observe(noise)==(i==frames-1?Profile::LearnResult::accepted:Profile::LearnResult::waiting),"learn duration");
        check(p.valid()&&!p.learning(),"trusted profile");const auto anchor=p.anchor();
        check(p.begin(),"relearn");for(int i=0;i<12;++i)p.observe(noise);
        auto polluted=noise;polluted[1].fill(.001f);
        // Total stereo energy rises <1%, but the quiet channel rises 10x.
        check(p.observe(polluted)==Profile::LearnResult::rejected,"quiet channel contamination");
        check(p.valid()&&p.anchor()==anchor&&p.powers()==anchor,"transaction rollback");
        auto drift=noise;for(auto& c:drift)for(float& v:c)v*=1.2f;
        const int hold=int(std::ceil(.5*rate/hop));
        for(int i=0;i<hold;++i)p.track(drift,true);
        check(p.powers()==anchor,"idle hold before tracking");
        for(int i=0;i<int(8*rate/hop);++i)p.track(drift,true);
        const float ratio=p.powers()[1][30]/anchor[1][30];
        check(ratio>1.17f&&ratio<1.21f,"bounded moderate drift tracking");
        const auto tracked=p.powers();for(int i=0;i<hold*2;++i)p.track(polluted,false);
        check(p.powers()==tracked&&p.anchor()==anchor,"foreground freeze");
        for(int i=0;i<int(20*rate/hop);++i)p.track(polluted,true);
        check(p.powers()[1][30]<=1.3f*anchor[1][30],"upper anchor bound");
        Profile::Powers zero{};for(int i=0;i<int(20*rate/hop);++i)p.track(zero,true);
        check(p.powers()[1][30]>=.7f*anchor[1][30],"lower anchor bound");
        check(p.begin(),"cancel transaction");p.cancel();check(p.valid()&&p.anchor()==anchor,"cancel preserves anchor");
        p.prepare(rate,hop,1,129);check(!p.valid()&&!p.learning(),"new format invalidates profile");
        std::cout<<rate<<" Hz: drift ratio "<<ratio<<", rollback, freeze and bounds PASS\n";
    }
}
