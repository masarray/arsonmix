// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <vector>
#include <iostream>
#include <cstdint>
#include <cstdlib>
using namespace armix::builtin_fx::de_ezzer;
using Signal=std::vector<float>;
void check(bool x,const char* s){if(!x){std::cerr<<s<<'\n';std::exit(1);}}
Signal run(Signal x,double rate,Mode mode){Core c; Settings s;s.mode=mode;check(c.prepare(rate,1)&&c.setSettings(s),"setup");for(int i=0;i<static_cast<int>(x.size());){const int n=std::min(static_cast<int>(x.size())-i,1+i%197);c.process(x.data()+i,nullptr,n);i+=n;}return x;}
double rms(const Signal& x,double rate,double a,double b){double sum=0;int lo=static_cast<int>(rate*a),hi=static_cast<int>(rate*b);for(int i=lo;i<hi;++i)sum+=static_cast<double>(x[i])*x[i];return std::sqrt(sum/(hi-lo));}
double cut(const Signal& x,const Signal& y,double rate,double a,double b){return 20*std::log10(rms(x,rate,a,b)/rms(y,rate,a,b));}
int main(){for(double rate:{44100.,48000.,88200.,96000.}){
 int n=static_cast<int>(rate); Signal hiss(n),tone(n),quiet(n),multi(n);
 detail::Band b1,b2;b1.prepare(rate,6500,1.4);b2.prepare(rate,9000,1.8);std::uint32_t seed=12345;
 for(int i=0;i<n;++i){seed=1664525u*seed+1013904223u;double white=static_cast<double>(seed)/4294967296.*2-1;double z=.20*(b1.tick(white)+b2.tick(white));double t=i/rate;hiss[i]=static_cast<float>(t<.22|| (t>.6&&t<.8)?z:0);quiet[i]=hiss[i]*.001f;tone[i]=static_cast<float>(.1*std::sin(2*std::numbers::pi*7200*t));multi[i]=static_cast<float>(.1*std::sin(2*std::numbers::pi*7200*t)+.08*std::sin(2*std::numbers::pi*8400*t));}
 auto a=run(hiss,rate,Mode::Auto),v=run(hiss,rate,Mode::Voice),t=run(tone,rate,Mode::Voice),q=run(quiet,rate,Mode::Voice),m=run(multi,rate,Mode::Voice);
 std::cout<<rate<<" initial auto/voice "<<cut(hiss,a,rate,.05,.18)<<' '<<cut(hiss,v,rate,.05,.18)<<" late voice "<<cut(hiss,v,rate,.65,.75)<<" tone "<<cut(tone,t,rate,.5,.9)<<" quiet "<<cut(quiet,q,rate,.05,.18)<<" two-tone "<<cut(multi,m,rate,.5,.9)<<'\n';
 check(cut(hiss,v,rate,.05,.18)>=3,"initial fricative benefit");check(cut(hiss,v,rate,.65,.75)>=3,"late fricative benefit");check(std::abs(cut(tone,t,rate,.5,.9))<.2,"tone preserve");check(std::abs(cut(quiet,q,rate,.05,.18))<.2,"quiet hiss preserve");check(std::abs(cut(multi,m,rate,.5,.9))<.2,"two-tone preserve");
 Core c;Settings settings;settings.mode=Mode::Voice;check(c.prepare(rate,2)&&c.setSettings(settings),"stereo voice");Signal l=hiss,r=hiss;for(auto& x:r)x=-x;c.process(l.data(),r.data(),n);check(l==v,"voice partition and stereo parity");for(int i=0;i<n;++i)check(l[i]==-r[i],"voice antiphase");Settings invalid=settings;invalid.mode=static_cast<Mode>(99);check(!c.setSettings(invalid)&&c.settings().mode==Mode::Voice,"mode rejection");c.reset();check(c.setSettings(Settings{}),"switch auto");l=hiss;r=hiss;for(auto& x:r)x=-x;c.process(l.data(),r.data(),n);check(l==a,"mode reset auto parity");
 }}
