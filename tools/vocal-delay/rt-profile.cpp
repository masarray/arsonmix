// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/vocal_delay/ContinuousDelayCore.h"
#include <array>
#include <chrono>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>
using namespace armix::builtin_fx::vocal_delay;
int main(){
    using Clock=std::chrono::steady_clock;
    for(double rate:{44100.,48000.,96000.})for(std::size_t frames:{64u,128u}){
        ContinuousDelayCore c;Parameters p{};p.feedback=.65f;p.echo=.28f;p.style=EchoStyle::multitap;p.adaptiveFocus=true;
        if(!c.prepare(rate,p))return 1;
        std::array<float,128> l{},r{};std::vector<double> us;us.reserve(8000);
        double phase=0;float peak=0;
        for(int b=0;b<9000;++b){
            if(b%25==0){p.timeMs=100.f+float(b%2000);ContinuousDelayCore::Prepared q;if(!c.prepareEdit(p,rate,q)||!c.applyPrepared(q))return 2;}
            for(std::size_t i=0;i<frames;++i){phase+=6.283185307179586*220./rate;l[i]=r[i]=.2f*float(std::sin(phase));}
            const auto begin=Clock::now();c.processStereo(l.data(),r.data(),frames);const auto end=Clock::now();
            if(b>=1000)us.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            for(std::size_t i=0;i<frames;++i){if(!std::isfinite(l[i])||!std::isfinite(r[i]))return 3;peak=std::max(peak,std::abs(l[i]));}
        }
        std::sort(us.begin(),us.end());const auto p99=us[us.size()*99/100];
        std::printf("VD3_PROFILE rate=%.0f frames=%zu p50_us=%.3f p99_us=%.3f deadline_pct=%.3f peak=%.4f ring_bytes=%zu\n",rate,frames,us[us.size()/2],p99,p99/(double(frames)/rate*1e6)*100,peak,c.ringBytes());
    }
}
