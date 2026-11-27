// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/AutoDuckerCore.h"
#include <vector>
#include <iostream>
#include <limits>
#include <cstdlib>
using namespace armix::builtin_fx;
static void check(bool ok,const char* message){
    if(!ok){std::cerr<<message<<'\n';std::exit(1);}
}
int main(){
    for(const int rate:{8000,44100,48000,96000,192000,384000}){
        const int n=rate*3;
        std::vector<float> l(n,1),r(n,-.5f),key(n),anti(n);
        for(int i=rate/4;i<rate;i++)key[i]=.2f*std::sin(6.283185307179586*1000*i/rate);
        for(int i=0;i<n;i++)anti[i]=-key[i];
        auto a=l,b=r;
        AutoDuckerCore whole,split;
        check(whole.prepare(rate)&&split.prepare(rate),"valid rate");
        whole.process(l.data(),r.data(),n,{key.data(),anti.data()});
        for(int i=0;i<n;){
            const int count=std::min(n-i,1+(i%257));
            split.process(a.data()+i,b.data()+i,count,{key.data()+i,anti.data()+i});
            i+=count;
        }
        check(l==a&&r==b,"callback partition parity");
        check(l[rate/8]==1.f,"silent key unity");
        check(l[rate/2]>.249f&&l[rate/2]<.255f,"12 dB bounded range");
        check(l[rate+rate/10]<.26f,"phrase hold");
        check(l[n-1]>.998f,"release recovery");
        for(int i=1;i<n;++i){
            check(std::isfinite(l[i])&&l[i]>=.25118f&&l[i]<=1.f,"gain bounds");
            check(r[i]==-.5f*l[i],"linked stereo gain");
            check(std::abs(l[i]-l[i-1])<.02f,"no step discontinuity");
        }
        AutoDuckerCore missing;
        check(missing.prepare(rate),"prepare missing");
        std::fill(a.begin(),a.end(),1.f);std::fill(b.begin(),b.end(),1.f);
        missing.process(a.data(),b.data(),rate,{key.data(),nullptr});
        check(missing.gain()<.26f,"mono trigger");
        missing.process(a.data()+rate,b.data()+rate,rate,{});
        check(missing.gain()>.98f&&!missing.active(),"missing key fail open");
        AutoDuckerCore dc;check(dc.prepare(rate),"prepare DC");
        std::fill(key.begin(),key.end(),.2f);
        dc.process(a.data(),b.data(),n,{key.data(),nullptr});
        check(dc.gain()>.998f&&!dc.active(),"DC rejection");
        std::fill(key.begin(),key.end(),std::numeric_limits<float>::quiet_NaN());
        dc.process(a.data(),b.data(),n,{key.data(),nullptr});
        check(dc.gain()==1.f,"nonfinite key isolation");
    }
    AutoDuckerCore::Prepared p;AutoDuckerSettings invalid;
    invalid.depthDb=-1;
    check(!AutoDuckerCore::prepareSettings(48000,invalid,p),"negative depth rejected");
    check(!AutoDuckerCore::prepareSettings(0,{},p),"invalid rate rejected");
    std::cout<<"Auto Ducker DSP: rate, partition, stereo, hold, release, key loss, finite input PASS\n";
}
