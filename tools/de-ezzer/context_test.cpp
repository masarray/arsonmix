// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace armix::builtin_fx::de_ezzer;
using Signal=std::vector<float>;
void check(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
double tone(double rate,int i,double f){return std::sin(2*std::numbers::pi*f*i/rate);}
double amplitude(const Signal& x,double rate,double f,double start,double end){
 double s=0,c=0;int a=static_cast<int>(rate*start),b=static_cast<int>(rate*end);
 for(int i=a;i<b;++i){double w=2*std::numbers::pi*f*i/rate;s+=x[i]*std::sin(w);c+=x[i]*std::cos(w);}
 return std::hypot(s,c)*2/(b-a);
}
double cut(const Signal& x,const Signal& y,double rate,double f,double start,double end){return 20*std::log10(amplitude(x,rate,f,start,end)/amplitude(y,rate,f,start,end));}
Signal render(Signal x,double rate,Settings settings={}){Core c;check(c.prepare(rate,1)&&c.setSettings(settings),"prepare");for(int i=0;i<static_cast<int>(x.size());){int n=std::min(static_cast<int>(x.size())-i,1+i%251);c.process(x.data()+i,nullptr,n);i+=n;}return x;}
int main(){
 detail::Context ctx;
 for(int i=0;i<1000;++i)(void)ctx.excess(1,.7,.3,.3981,.998,.01);
 check(ctx.reference>.65&&ctx.reference<=1,"bounded reference learns");
 const double learned=ctx.reference;
 for(int i=0;i<1000;++i)(void)ctx.excess(.1,1,.3,.3981,.998,.01);
 check(ctx.reference==learned,"severe event cannot train reference");
 for(int i=0;i<1000;++i)(void)ctx.excess(1,.03,.3,.3981,.998,.01);
 check(ctx.reference<.04,"reference recovers");
 ctx.reset();check(ctx.reference==.02&&ctx.bodyMemory==0,"context reset");
 for(double rate:{44100.,48000.,88200.,96000.}){
 int n=static_cast<int>(rate*2);Signal pure(n),bright(n),severe(n),unvoiced(n),weak(n),conditioned(n);
 for(int i=0;i<n;++i){double t=i/rate,b=.12*tone(rate,i,800),h=tone(rate,i,7200);pure[i]=static_cast<float>(.1*h);bright[i]=static_cast<float>(b+.10*h);severe[i]=static_cast<float>(b+(t>=.7&&t<1.3?.25:.0)*h);unvoiced[i]=static_cast<float>(t<.7?b:(t<.8?.18*h:0));weak[i]=severe[i]*.01f;conditioned[i]=static_cast<float>(b+(t>=.9&&t<1.3?.35:.10)*h);}
 auto p=render(pure,rate),b=render(bright,rate),s=render(severe,rate),u=render(unvoiced,rate),w=render(weak,rate),v=render(conditioned,rate);
 Settings mild;mild.amount=.25f;auto less=render(severe,rate,mild); Settings normal12;normal12.rangeDb=12;auto base12=render(severe,rate,normal12); Settings strong=normal12;strong.amount=1;auto more=render(severe,rate,strong);
 double cp=cut(pure,p,rate,7200,1.4,1.8),cb=cut(bright,b,rate,7200,1.4,1.8),cs=cut(severe,s,rate,7200,.95,1.2),body=cut(severe,s,rate,800,.95,1.2),cu=cut(unvoiced,u,rate,7200,.72,.77),cw=cut(weak,w,rate,7200,.95,1.2),cm=cut(severe,more,rate,7200,.95,1.2),cl=cut(severe,less,rate,7200,.95,1.2),c12=cut(severe,base12,rate,7200,.95,1.2),cv=cut(conditioned,v,rate,7200,1.05,1.25);
 std::cout<<rate<<" pure "<<cp<<" bright "<<cb<<" severe "<<cs<<" body "<<body<<" unvoiced "<<cu<<" weak "<<cw<<" amount .25/.5 range8 "<<cl<<"/"<<cs<<" amount .5/1 range12 "<<c12<<"/"<<cm<<" bright-conditioned "<<cv<<'\n';
 {check(std::abs(cp)<.15,"isolated HF negative");check(std::abs(cb)<.5,"sustained bright negative");check(cs>=3&&cs<=8&&std::abs(body)<.5,"phase-independent active gate");check(cu>=3,"unvoiced following vowel");check(cv>=3,"strong event after bright adaptation");check(std::abs(cs-cw)<.2,"level invariance");check(cs>=cl+1&&cm>=c12+1,"Amount alone monotonic");}
 // Stereo one-sided sibilance must trigger linked processing, no crossfeed.
 Core c;check(c.prepare(rate,2),"stereo");Signal left=severe,right(n,0);c.process(left.data(),right.data(),n);{check(left==s,"one-sided parity");check(std::all_of(right.begin(),right.end(),[](float x){return x==0;}),"no crosstalk");}
 }return 0;}
