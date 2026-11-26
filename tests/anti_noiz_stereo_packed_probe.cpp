// SPDX-License-Identifier: AGPL-3.0-only
// Identical driver compiled against exact old and candidate Core sources.
#include "AntiNoizCore.h"
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <cstdlib>
using namespace armix::builtin_fx::anti_noiz;
namespace {
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
void run(std::ostream& out,int rate,bool manual,int scenario){
    Core c;Core::Settings s;s.mode=Core::Mode::Voice;s.spectralManual=manual;s.cleanDb=24;s.silenceDb=0;s.artifactControl=.75f;s.vocalProtection=.5f;
    check(c.prepare(rate,2)&&c.setSettings(s)&&c.beginLearn(),"prepare Learn");
    std::mt19937 rng(907);std::uniform_real_distribution<float> noise(-.003f,.003f);
    std::vector<float> l(rate*3),r(l.size());for(std::size_t i=0;i<l.size();++i){l[i]=noise(rng);r[i]=noise(rng)*.6f;}
    c.process(l.data(),r.data(),int(l.size()));check(c.hasProfile(),"profile accepted");c.resetStreamKeepProfile();
    const float scale=scenario==0?1.f:scenario==1?.001f:0.f;
    for(int i=0;i<int(l.size());++i){const double t=double(i)/rate;l[i]=.4f*float(std::sin(2*std::numbers::pi*431*t))+noise(rng);r[i]=scale*(.2f*float(std::sin(2*std::numbers::pi*719*t))+noise(rng));}
    for(int pos=0;pos<int(l.size());){const int n=std::min(127,int(l.size())-pos);c.process(l.data()+pos,r.data()+pos,n);pos+=n;}
    if(scenario==2){double energy=0;float peak=0;for(float x:r){peak=std::max(peak,std::abs(x));energy+=double(x)*x;}
        check(peak<=1e-6f&&std::sqrt(energy/r.size())<=1e-7,"silent channel leakage");
        std::cout<<"STEREO_ISOLATION,"<<rate<<','<<manual<<",peak="<<peak<<",rms="<<std::sqrt(energy/r.size())<<'\n';}
    for(std::size_t i=0;i<l.size();++i){check(std::isfinite(l[i])&&std::isfinite(r[i]),"finite stereo");out.write(reinterpret_cast<const char*>(&l[i]),sizeof(float));out.write(reinterpret_cast<const char*>(&r[i]),sizeof(float));}
}
}
int main(int argc,char** argv){if(argc!=2)return 64;std::ofstream out(argv[1],std::ios::binary);check(bool(out),"output");for(int rate:{44100,48000,88200,96000})for(bool manual:{false,true})for(int scenario=0;scenario<3;++scenario)run(out,rate,manual,scenario);check(bool(out),"write");}
