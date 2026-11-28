// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <vector>
#include <iostream>
#include <cstdlib>
using armix::builtin_fx::de_ezzer::Core;
using armix::builtin_fx::de_ezzer::Settings;
void require(bool ok,const char* msg){if(!ok){std::cerr<<msg<<'\n';std::exit(1);}}
int main(){
 for(double rate:{44100.,48000.,88200.,96000.}){
    const int n=static_cast<int>(rate),start=n/3,end=2*n/3;
    std::vector<float> dry(n),body(n),s(n),out,other;
    for(int i=0;i<n;++i){body[i]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*i/rate));s[i]=i>=start&&i<end?static_cast<float>(.25*std::sin(2*std::numbers::pi*7200*i/rate)):0;dry[i]=body[i]+s[i];}
    Core a,b;require(a.prepare(rate,1)&&b.prepare(rate,1),"prepare");out=dry;other=dry;
    a.process(out.data(),nullptr,n);
    for(int i=0;i<n;){int count=std::min(n-i,1+(i%137));b.process(other.data()+i,nullptr,count);i+=count;}
    require(out==other,"block partition");
    auto measure=[&](const std::vector<float>& y){double h=0,hh=0,lo=0,ll=0;for(int i=start+n/20;i<end-n/20;++i){h+=y[i]*s[i];hh+=s[i]*s[i];lo+=y[i]*body[i];ll+=body[i]*body[i];}return std::array<double,2>{-20*std::log10(std::abs(h/hh)),20*std::log10(std::abs(lo/ll))};};
    const auto m=measure(out);std::cout<<rate<<" default HF cut dB="<<m[0]<<" body deviation dB="<<m[1]<<'\n';
    require(m[0]>=3&&m[0]<=8&&std::abs(m[1])<.5,"benefit/preservation");
    Settings strong;strong.amount=1;strong.rangeDb=12;require(a.setSettings(strong),"strong setting");a.reset();other=dry;a.process(other.data(),nullptr,n);require(measure(other)[0]>m[0]+1,"amount monotonic");
    a.reset();Settings zero;zero.amount=0;require(a.setSettings(zero),"zero setting");other=dry;a.process(other.data(),nullptr,n);require(other==dry,"unity");
    a.reset();require(a.setSettings(Settings{}),"default");other=body;a.process(other.data(),nullptr,n);require(other==body,"body negative control");
    require(!a.setSettings({NAN,.5f,8,true}),"invalid settings");
    require(a.prepare(rate,2),"stereo prepare");out=dry;other=dry;for(auto& x:other)x=-x;a.process(out.data(),other.data(),n);for(int i=0;i<n;++i)require(out[i]==-other[i],"antiphase link");
    require(a.prepare(rate,1),"reprepare");float invalid[]={NAN,INFINITY,0,.1f};a.process(invalid,nullptr,4);for(float x:invalid)require(std::isfinite(x),"finite recovery");
 }
 require(Core::latencySamples()==0,"latency");std::cout<<"PASS\n";
}
