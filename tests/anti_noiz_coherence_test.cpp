// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCoherence.h"
#include <cstdlib>
#include <iostream>
#include <random>
using armix::builtin_fx::anti_noiz::ForegroundCoherence;
static void require(bool b,const char* s){if(!b){std::cerr<<s<<'\n';std::exit(1);}}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}){
        const int hop=rate>48000?256:128;ForegroundCoherence<512> g;
        g.prepare(rate,hop,2);ForegroundCoherence<512>::Spectrum x{};
        std::mt19937 gen(518);std::uniform_real_distribution<float> phase(-3.14159f,3.14159f);
        for(int i=0;i<256;++i){
            x[0][2]=std::polar(.002f,.73f*i);x[1][2]=std::polar(.002f,phase(gen));
            g.update(x,true);
            if(i<11)require(g.support(0,2)==0.f,"warmup must not grant authority");
        }
        require(g.support(0,2)>.99f,"coherent positive evidence");
        require(g.support(1,2)<.15f,"independent random phase");
        require(g.support(0,0)==0.f&&g.support(0,1)==0.f&&g.support(0,5)==0.f,"scope bins");
        g.update(x,false);require(g.support(0,2)==0.f,"disabled clears history");
        g.update(x,true);require(g.support(0,2)==0.f,"re-enable warmup");
        g.reset();require(g.support(1,2)==0.f,"reset clears other channel");
    }
    std::cout<<"Coherence evidence, rate, stereo and lifecycle PASS\n";
}
