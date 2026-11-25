// SPDX-License-Identifier: AGPL-3.0-only
// Same-source baseline/candidate. PCM written outside measured callbacks.
#include "../../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(int argc,char** argv){try{
    require(argc==2,"PCM output path");std::ofstream pcm(argv[1],std::ios::binary);require(bool(pcm),"open PCM");
    using Clock=std::chrono::steady_clock;
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(int channels:{1,2})for(int kind=0;kind<2;++kind){
        Core fx;Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=kind?24.f:0.f;
        require(fx.prepare(rate,channels)&&fx.setSettings(s)&&fx.beginLearn(),"prepare");
        std::mt19937 rng(682);std::uniform_real_distribution<float> random(-1.f,1.f);
        std::array<float,2> lp{};std::array<float,128> l{},r{};
        std::vector<double> elapsed;elapsed.reserve(4*rate/128+1);
        for(unsigned phase=0;phase<2;++phase){
            const unsigned duration=(phase?4u:3u)*rate;
            for(unsigned pos=0;pos<duration;){
                const int n=int(std::min(128u,duration-pos));
                for(int j=0;j<n;++j)for(int c=0;c<channels;++c){
                    const float white=random(rng);lp[c]+=.025f*(white-lp[c]);
                    const unsigned i=pos+unsigned(j);
                    const double t=2.*std::numbers::pi*i/rate;
                    const float noise=.08f*lp[c]+.001f*white;
                    const float fg=phase?float((kind?.02:.05)*(std::sin(375.*t)+.4*std::sin(937.5*t)+.2*std::sin((c?1500.:1312.5)*t))):0.f;
                    (c?r:l)[j]=noise+fg;
                }
                const auto a=Clock::now();fx.process(l.data(),channels==2?r.data():nullptr,n);const auto b=Clock::now();
                if(phase){
                    elapsed.push_back(std::chrono::duration<double,std::micro>(b-a).count());
                    for(int j=0;j<n;++j){
                        require(std::isfinite(l[j])&&(channels==1||std::isfinite(r[j])),"finite PCM");
                        pcm.write(reinterpret_cast<const char*>(&l[j]),sizeof(float));
                        if(channels==2)pcm.write(reinterpret_cast<const char*>(&r[j]),sizeof(float));
                    }
                }
                pos+=unsigned(n);
            }
            require(fx.hasProfile()&&!fx.learning()&&fx.latencySamples()==(rate>48000?512:256),"profile latency");
        }
        std::sort(elapsed.begin(),elapsed.end());const double budget=128.e6/rate;
        const auto p99=elapsed[(elapsed.size()-1)*99/100];
        require(p99<budget*.5,"P99 budget");
        std::cout<<rate<<','<<channels<<','<<kind<<','<<elapsed[elapsed.size()/2]<<','<<p99<<','<<budget<<'\n';
    }
    pcm.close();require(bool(pcm),"write PCM");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
