// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizBroadband.h"
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
using armix::builtin_fx::anti_noiz::BroadbandNoise;
namespace {
using G=BroadbandNoise<257>;
void require(bool ok,const char* what){if(!ok){std::cerr<<what<<'\n';std::exit(1);}}
// Stable cross-compiler input generator: no implementation-dependent
// uniform_real_distribution or runtime transcendental input generation.
float unit(std::mt19937& r){return float(r()>>8)*(1.f/16777216.f);}
template<class T> float limit(T& g,int k,const typename T::Powers& p,float floor,float gain){
    if constexpr(requires{g.mayLimit(k,gain,floor);}) {
        if(!g.mayLimit(k,gain,floor))return gain;
    }
    return std::min(gain,g.cap(k,p,floor));
}
template<class T> bool skipped(T& g,int k,float gain,float floor){
    if constexpr(requires{g.mayLimit(k,gain,floor);})return !g.mayLimit(k,gain,floor);
    return false;
}
}
int main(){
    std::mt19937 rng(99302);std::uint64_t hash=14695981039346656037ull,checks=0,skip=0;
    auto mix=[&](std::uint32_t v){hash=(hash^v)*1099511628211ull;};
    for(double rate:{44100.,48000.,88200.,96000.})for(int channels:{1,2}){
        const int fft=rate>48000.?512:256,bins=fft/2+1;
        const int first=int(std::ceil(3000.*fft/rate)),last=int(std::ceil(12000.*fft/rate));
        G g;g.prepare(rate,fft/2,fft,channels);G::Powers anchor{},p{};
        for(int epoch=0;epoch<3;++epoch){
            for(int c=0;c<channels;++c)for(int k=0;k<bins;++k)
                anchor[c][k]=(k>=first&&k<last?.5f+unit(rng):.0005f)*(epoch==1?.01f:1.f);
            g.configure(anchor);require(g.eligible(),"eligible randomized profile");
            for(int frame=0;frame<320;++frame){
                const bool enabled=frame%107!=106, residual=frame%137<100;
                for(int c=0;c<channels;++c)for(int k=0;k<bins;++k){
                    const float scale=frame%80<56?1.f+unit(rng):.1f+4.f*unit(rng);
                    p[c][k]=anchor[c][k]*scale;
                }
                if(frame%41==40)p[channels-1][first+4]=anchor[channels-1][first+4]*100.f;
                if(frame%59==58)p[0][1]=100.f;
                g.update(p,anchor,enabled,residual);mix(g.residualIdle()?1u:0u);
                for(int k=0;k<bins;++k){
                    const float gain=unit(rng),floor=frame%3==0?.063f:frame%3==1?.2f:.5f;
                    const float exact=std::min(gain,g.cap(k,p,floor)),actual=limit(g,k,p,floor,gain);
                    require(std::bit_cast<std::uint32_t>(exact)==std::bit_cast<std::uint32_t>(actual),"fast path must equal unconditional cap");
                    mix(std::bit_cast<std::uint32_t>(actual));++checks;skip+=skipped(g,k,gain,floor);
                }
            }
            g.reset();require(!g.residualIdle(),"reset clears residual");
        }
    }
    std::cout<<"CAP_EQUIVALENCE,"<<checks<<','<<hash<<'\n';
    std::cerr<<"CAP_BYPASS,"<<skip<<'/'<<checks<<'\n';
}
