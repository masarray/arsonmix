// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <vector>
#include <iostream>
#include <cstdlib>
using namespace armix::builtin_fx::de_ezzer;
using Signal=std::vector<float>;
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
double tone(double rate,int i,double hz){return std::sin(2*std::numbers::pi*hz*i/rate);}
double amp(const Signal& x,double rate,double hz,double from,double to){double re=0,im=0;int a=static_cast<int>(rate*from),b=static_cast<int>(rate*to);for(int i=a;i<b;++i){double w=2*std::numbers::pi*hz*i/rate;re+=x[i]*std::cos(w);im+=x[i]*std::sin(w);}return std::hypot(re,im);}
double cut(const Signal& dry,const Signal& wet,double rate,double hz){return 20*std::log10(amp(dry,rate,hz,.6,.9)/amp(wet,rate,hz,.6,.9));}
int main(){
 for(double rate:{44100.,48000.,88200.,96000.}){
    Settings s;s.shape=Shape::Focused;s.amount=1;s.rangeDb=12;
    const int n=static_cast<int>(rate);Signal l(n),r(n);
    for(int i=0;i<n;++i){double body=.12*tone(rate,i,800);l[i]=static_cast<float>(body+(i>n/4?.25*tone(rate,i,4600):0));r[i]=static_cast<float>(body+(i>n/4?.25*tone(rate,i,8000):0));}
    Core a,b;check(a.setSettings(s)&&a.prepare(rate,2)&&b.setSettings(s)&&b.prepare(rate,2),"prepare stereo");auto wl=l,wr=r,sl=r,sr=l;
    a.process(wl.data(),wr.data(),n);for(int i=0;i<n;){int count=std::min(n-i,1+i%193);b.process(sl.data()+i,sr.data()+i,count);i+=count;}
    const double cl=cut(l,wl,rate,4600),cr=cut(r,wr,rate,8000);
    std::cout<<rate<<" stereo cuts "<<cl<<' '<<cr<<" weight "<<a.focusLowerWeight();
    check(wl==sr&&wr==sl,"channel swap and partition invariance");
    check(cl>=3&&cr>=3,"both stereo targets attenuated");
    check(std::abs(cut(l,wl,rate,800))<.5&&std::abs(cut(r,wr,rate,800))<.5,"body preservation");
    Core c;check(c.setSettings(s)&&c.prepare(rate,1),"prepare alternating");
    const int warm=static_cast<int>(rate*.3),step=static_cast<int>(rate*.02),probe=static_cast<int>(rate*.01);Signal x(warm+4*step);
    for(int i=0;i<static_cast<int>(x.size());++i){double hz=i<warm?4600:((i-warm)/step%2==0?8000:4600);x[i]=static_cast<float>(.12*tone(rate,i,800)+.25*tone(rate,i,hz));}
    c.process(x.data(),nullptr,warm);check(c.focusLowerWeight()>.7,"initial lower");
    for(int k=0;k<4;++k){int start=warm+k*step;c.process(x.data()+start,nullptr,probe);const double w=c.focusLowerWeight();std::cout<<" switch "<<w;check(k%2==0?w<.30:w>.70,"target reaches dominant band within 10ms");c.process(x.data()+start+probe,nullptr,step-probe);}
    std::cout<<'\n';
 }
}
