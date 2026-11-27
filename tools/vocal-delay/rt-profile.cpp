// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/vocal_delay/ContinuousDelayCore.h"
#include <array>
#include <chrono>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <memory>
using namespace armix::builtin_fx::vocal_delay;
int main(){
    using Clock=std::chrono::steady_clock;
    for(int policy:{0,1,2})for(double rate:{44100.,48000.,96000.})for(std::size_t frames:{64u,128u})for(std::size_t count:{1u,10u}){
        Parameters p{};p.feedback=.65f;p.echo=.28f;p.style=EchoStyle::multitap;p.adaptiveFocus=true;p.smartStage=policy!=0;p.throwMode=policy==2?ThrowMode::concert:ThrowMode::off;p.duck=.92f;p.mode=StereoMode::pingPong;
        std::array<std::unique_ptr<ContinuousDelayCore>,10> cores{};
        for(std::size_t k=0;k<count;++k){cores[k]=std::make_unique<ContinuousDelayCore>();if(!cores[k]->prepare(rate,p))return 1;}
        std::array<std::array<float,128>,10> l{},r{};std::vector<double> us;us.reserve(8000);
        double phase=0;float peak=0;
        for(int b=0;b<9000;++b){
            if(b%25==0){p.timeMs=100.f+float(b%2000);ContinuousDelayCore::Prepared q;if(!ContinuousDelayCore::prepareEdit(p,rate,q))return 2;for(std::size_t k=0;k<count;++k)if(!cores[k]->applyPrepared(q))return 2;}
            for(std::size_t i=0;i<frames;++i){phase+=6.283185307179586*220./rate;const auto step=b%500;const float amp=policy==2?(step<170?.07f:step<330?.34f:0.f):.2f;const float x=amp*float(std::sin(phase));for(std::size_t k=0;k<count;++k)l[k][i]=r[k][i]=x;}
            const auto begin=Clock::now();for(std::size_t k=0;k<count;++k)cores[k]->processStereo(l[k].data(),r[k].data(),frames);const auto end=Clock::now();
            if(b>=1000)us.push_back(std::chrono::duration<double,std::micro>(end-begin).count());
            for(std::size_t k=0;k<count;++k)for(std::size_t i=0;i<frames;++i){if(!std::isfinite(l[k][i])||!std::isfinite(r[k][i]))return 3;peak=std::max(peak,std::abs(l[k][i]));}
        }
        std::sort(us.begin(),us.end());const auto p99=us[us.size()*99/100];
        std::printf("VD_PROFILE policy=%d instances=%zu rate=%.0f frames=%zu p50_us=%.3f p99_us=%.3f deadline_pct=%.3f peak=%.4f ring_bytes=%zu\n",policy,count,rate,frames,us[us.size()/2],p99,p99/(double(frames)/rate*1e6)*100,peak,cores[0]->ringBytes()*count);
    }
}
