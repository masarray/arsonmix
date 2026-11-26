// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RenderPressureEvidence.h"
#include "../engine/core/CallbackCadenceEvidence.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
    armix::rt::RenderPressureEvidence p;
    std::uint64_t start=10000;
    for(unsigned i=0;i<=64;++i){
        const double period=i%2?1000.:2000.;
        const bool over=i%4>=2;
        p.finish(1,7,start,static_cast<std::uint64_t>(period*(over?1.2:.2)),period);
        if(i<64)assert(!p.snapshot().valid);
        start+=static_cast<std::uint64_t>(period*(i%2?2.:over?1.3:1.));
    }
    auto s=p.snapshot();assert(s.valid&&s.withinThenOnTime==16&&s.withinThenLate==16&&
        s.overThenOnTime==16&&s.overThenLate==16);
    p.finish(1,8,start,10,1000.);assert(!p.snapshot().valid);
    for(unsigned i=0;i<64;++i){start+=1000;p.finish(1,8,start,10,1000.);}
    s=p.snapshot();assert(s.valid&&s.withinThenOnTime==64);
    p.breakSequence();assert(!p.snapshot().valid);
    for(unsigned i=0;i<65;++i){start+=2000;p.finish(2,8,start,1500,1000.);}
    s=p.snapshot();assert(s.valid&&s.overThenLate==64);
    p.finish(2,8,start-1,10,1000.);assert(!p.snapshot().valid);
    p.finish(2,8,start,10,std::numeric_limits<double>::quiet_NaN());assert(!p.snapshot().valid);
    armix::rt::CallbackCadenceEvidence c;c.prepare(48000.);
    c.observe(1000,48);c.breakSequence();
    assert(c.observe(100000,48).kind==armix::rt::CallbackCadenceEvidence::Kind::first);
    std::puts("PASS: 64-pair pressure cells, mixed quantum, revision/epoch/gap/reset and clock fences");
}
