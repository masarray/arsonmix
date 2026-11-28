// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <vector>
#include <iostream>
#include <cstdlib>
using namespace armix::builtin_fx::de_ezzer;
using Signal=std::vector<float>;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
double amplitude(const Signal& x,double rate,double hz){
    double re=0,im=0;
    for(int i=static_cast<int>(rate*.65);i<static_cast<int>(rate*.95);++i){
        const double a=2*std::numbers::pi*hz*i/rate;
        re+=x[i]*std::cos(a);im+=x[i]*std::sin(a);
    }
    return std::hypot(re,im);
}
double cut(const Signal& x,const Signal& y,double rate,double hz){return 20*std::log10(amplitude(x,rate,hz)/amplitude(y,rate,hz));}
Signal run(const Signal& x,double rate,Settings s,int block){
    Core c;check(c.setSettings(s)&&c.prepare(rate,1),"prepare");
    auto y=x;for(int i=0;i<static_cast<int>(y.size());i+=block)c.process(y.data()+i,nullptr,std::min(block,static_cast<int>(y.size())-i));return y;
}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}){
        for(double hz:{4600.,6200.,8000.}){
            Signal x(static_cast<int>(rate));for(int i=0;i<static_cast<int>(x.size());++i){const double t=i/rate;x[i]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+(t>.25?.25*std::sin(2*std::numbers::pi*hz*t):0));}
            Settings s;s.shape=Shape::Focused;s.amount=.5f;s.rangeDb=18;
            const auto moderate=run(x,rate,s,127);s.amount=1;s.sensitivity=1;
            const auto strong=run(x,rate,s,127);
            const double a=cut(x,moderate,rate,hz),b=cut(x,strong,rate,hz);
            std::cout<<rate<<" Hz "<<hz<<" moderate "<<a<<" strong "<<b<<" body "<<cut(x,strong,rate,800)<<'\n';
            check(b>a+2&&b>=6&&b<=18.5,"strong setting needs measurable benefit");
            check(std::abs(cut(x,strong,rate,800))<.5,"body preservation");
            check(run(x,rate,s,1)==strong,"partition parity");
            s.amount=0;check(run(x,rate,s,480)==x,"zero amount exact unity");
        }
    }
}
