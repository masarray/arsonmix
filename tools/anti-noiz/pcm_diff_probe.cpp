// SPDX-License-Identifier: AGPL-3.0-only
// Diagnostic only. Capture identical generated input and each output stream
// from direct Core and Control. No changes to the original RT benchmark.
#include "AntiNoizControl.h"
#include <array>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
using namespace armix::builtin_fx::anti_noiz;
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void capture(unsigned rate,int scenario,const std::string& path){
    constexpr int block=32;
    Control control;Core direct;
    require(control.prepare(rate,2)&&direct.prepare(rate,2),"prepare");
    Core::Settings s;s.mode=scenario==0?Core::Mode::AutoProtect:Core::Mode::Voice;s.silenceDb=0;
    require(control.submit({1,Control::Kind::settings,s})&&direct.setSettings(s),"settings");
    std::ofstream out(path,std::ios::binary);
    require(bool(out),"open output");
    std::mt19937 gen(19231);std::uniform_real_distribution<float> noise(-.003f,.003f);
    std::array<float,2> prev{};
    std::array<float,block> l{},r{},cl{},cr{},dl{},dr{};
    // Preserve the benchmark's initial settings callback then 3s Learn.
    const unsigned start=block+3*rate,end=start+10*rate;
    for(unsigned pos=0;pos<end;){
        if(pos==block){
            require(control.submit({2,Control::Kind::learn,{}})&&direct.beginLearn(),"learn");
        }
        const unsigned boundary=pos<block?block:pos<start?start:end;
        const int n=int(std::min<unsigned>(block,boundary-pos));
        for(int j=0;j<n;++j)for(int c=0;c<2;++c){
            const float x=noise(gen);float y=x;
            if(pos>=start){
                const unsigned sample=pos-start+unsigned(j);
                const double t=double(sample)/rate;
                if(scenario==0){
                    const float amp=sample<3*rate?.001f:sample<6*rate?.008f:.04f;
                    y+=amp*float(std::sin(t*2*std::numbers::pi*(c?750:375)));
                }else{
                    y=scenario==1?std::sqrt(2.f)*x:std::sqrt(1.55f)*(x-.28f*prev[c]);
                    if(sample>=8*rate)y+=float(.05*std::sin(t*2*std::numbers::pi*(c?750:375)));
                }
                prev[c]=x;
            }
            (c?r:l)[j]=y;
        }
        cl=l;cr=r;dl=l;dr=r;
        control.process(cl.data(),cr.data(),n);direct.process(dl.data(),dr.data(),n);
        Control::Completion a;Control::Snapshot m;
        if(pos==0||pos==block)require(control.poll(a)&&a.applied,"ACK");
        while(control.pollTelemetry(m)){}
        for(int j=0;j<n;++j){
            const std::array<float,6> frame{l[j],r[j],cl[j],cr[j],dl[j],dr[j]};
            out.write(reinterpret_cast<const char*>(frame.data()),sizeof(frame));
        }
        pos+=unsigned(n);
    }
    require(control.snapshot().profile&&direct.hasProfile()&&bool(out),"capture complete");
}
}
int main(int argc,char** argv){try{
    require(argc==2,"output prefix");
    for(unsigned rate:{44100u,96000u})for(int scenario=0;scenario<3;++scenario)
        capture(rate,scenario,std::string(argv[1])+"-"+std::to_string(rate)+"-"+std::to_string(scenario)+".f32");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
