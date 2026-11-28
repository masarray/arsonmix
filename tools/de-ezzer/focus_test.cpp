// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <vector>
#include <iostream>
#include <cstdlib>
using namespace armix::builtin_fx::de_ezzer;
using Signal=std::vector<float>;
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
struct Result { Signal samples; float weight; };
Result run(const Signal& dry,double rate,Settings s,int block=127){Core c;check(c.setSettings(s)&&c.prepare(rate,1),"prepare");auto x=dry;for(int i=0;i<static_cast<int>(x.size());i+=block)c.process(x.data()+i,nullptr,std::min(block,static_cast<int>(x.size())-i));return {std::move(x),c.focusLowerWeight()};}
double amp(const Signal& x,double rate,double hz){double re=0,im=0;int a=static_cast<int>(rate*.8),b=static_cast<int>(rate*1.2);for(int i=a;i<b;++i){double w=2*std::numbers::pi*hz*i/rate;re+=x[i]*std::cos(w);im+=x[i]*std::sin(w);}return std::hypot(re,im);}
double cut(const Signal& a,const Signal& b,double rate,double hz){return 20*std::log10(amp(a,rate,hz)/amp(b,rate,hz));}
int main(){
 for(double rate:{44100.,48000.,88200.,96000.}){
  for(double hz:{4600.,8000.}){
    Signal dry(static_cast<int>(rate*1.3));for(int i=0;i<static_cast<int>(dry.size());++i){double t=i/rate;dry[i]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.015*std::sin(2*std::numbers::pi*12000*t)+(t>.3?.25*std::sin(2*std::numbers::pi*hz*t):0));}
    Settings shelf;shelf.rangeDb=18;auto old=run(dry,rate,shelf);const double desired=cut(dry,old.samples,rate,hz);
    Settings focused=shelf;focused.shape=Shape::Focused;
    // Match center-frequency benefit, not knob value; independent test of collateral attenuation.
    float lo=0,hi=1;for(int i=0;i<14;++i){focused.amount=(lo+hi)*.5f;auto y=run(dry,rate,focused);if(cut(dry,y.samples,rate,hz)<desired)lo=focused.amount;else hi=focused.amount;}
    auto y=run(dry,rate,focused);double actual=cut(dry,y.samples,rate,hz),airOld=cut(dry,old.samples,rate,12000),airNew=cut(dry,y.samples,rate,12000),body=cut(dry,y.samples,rate,800);
    std::cout<<rate<<' '<<hz<<" target "<<desired<<'/'<<actual<<" air "<<airOld<<'/'<<airNew<<" body "<<body<<" weight "<<y.weight<<" amount "<<focused.amount<<'\n';
    check(desired>=2&&std::abs(actual-desired)<.02,"matched attenuation");check(airOld-airNew>=.8,"air preservation");check(std::abs(body)<.5,"body preservation");check(hz==4600?y.weight>.7:y.weight<.3,"target follows dominant band");
    check(run(dry,rate,focused,1).samples==y.samples,"partition invariance");
    Core c;check(c.setSettings(focused)&&c.prepare(rate,2),"stereo");auto l=dry,r=dry;for(auto& x:r)x=-x;c.process(l.data(),r.data(),static_cast<int>(l.size()));check(l==y.samples,"stereo parity");for(std::size_t i=0;i<l.size();++i)check(l[i]==-r[i],"stereo link");
    focused.amount=0;check(run(dry,rate,focused).samples==dry,"exact unity");focused.shape=static_cast<Shape>(99);check(!c.setSettings(focused),"invalid shape");
    // Active shape transition must settle to the same PCM as a continuously running shelf.
    Settings active=shelf;active.shape=Shape::Focused;Core sw,ref;check(sw.setSettings(active)&&sw.prepare(rate,1)&&ref.setSettings(shelf)&&ref.prepare(rate,1),"transition setup");auto a=dry,b=dry;int mid=static_cast<int>(rate*.65);sw.process(a.data(),nullptr,mid);check(sw.setSettings(shelf),"switch");sw.process(a.data()+mid,nullptr,static_cast<int>(a.size())-mid);ref.process(b.data(),nullptr,static_cast<int>(b.size()));for(int i=static_cast<int>(rate*.95);i<static_cast<int>(a.size());++i)check(a[i]==b[i],"settled shelf parity");
  }
 }
 return 0;
}
