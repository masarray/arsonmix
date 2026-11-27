// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/builtin_fx/AutoDuckerCore.h"
#include <vector>
#include <iostream>
#include <limits>
#include <cstdlib>
namespace armix::ducker_test {
using namespace armix::builtin_fx;
inline void check(bool ok,const char* message){
    if(!ok){std::cerr<<message<<'\n';std::exit(1);}
}
inline void run(){
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
        check(std::isfinite(dc.gain())&&dc.gain()>.99999f&&!dc.active(),"nonfinite key isolation");
    }
    // Two talkers, overlapping and anti-phase across channels. A missing or
    // corrupt key must not release a valid speaker or double the reduction.
    constexpr int sr=48000,n=sr*3;
    std::vector<float> l(n,1.f),r(n,1.f),a(n),b(n),bad(n,std::numeric_limits<float>::quiet_NaN());
    for(int i=0;i<n;++i){
        const float v=.2f*std::sin(6.283185307179586*1000*i/sr);
        if(i<sr)a[i]=v;
        if(i>=sr/2&&i<2*sr)b[i]=-v;
    }
    AutoDuckerCore multi,partition;
    check(multi.prepare(sr)&&partition.prepare(sr),"multi prepare");
    const std::array<AutoDuckerSource,3> keys{{{7,{a.data(),nullptr}},{19,{b.data(),nullptr}},{32,{bad.data(),nullptr}}}};
    multi.processSources(l.data(),r.data(),n,keys);
    check(l[sr/4]<.26f&&l[sr*3/4]<.26f&&l[sr*3/2]<.26f,"overlap and handover");
    check(l[2*sr+sr/10]<.26f&&l.back()>.95f,"hold/recover after last source");
    for(float g:l)check(g>=.25118f&&g<=1.f&&std::isfinite(g),"multi depth bounded");
    std::vector<float> x(n,1.f),y(n,1.f);
    for(int i=0;i<n;){
        const int count=std::min(n-i,1+(i%257));
        const std::array<AutoDuckerSource,3> chunk{{{7,{a.data()+i,nullptr}},{19,{b.data()+i,nullptr}},{32,{bad.data()+i,nullptr}}}};
        partition.processSources(x.data()+i,y.data()+i,count,chunk);i+=count;
    }
    check(x==l&&y==r,"multi callback partition parity");
    AutoDuckerCore loss;check(loss.prepare(sr),"loss prepare");
    const std::array<AutoDuckerSource,2> both{{{7,{a.data(),nullptr}},{19,{b.data()+sr/2,nullptr}}}};
    loss.processSources(x.data(),y.data(),sr/2,both);
    const std::array<AutoDuckerSource,2> one{{{7,{}},{19,{b.data()+sr,nullptr}}}};
    loss.processSources(x.data(),y.data(),sr/2,one);
    check(loss.gain()<.26f,"one missing other continues");
    loss.processSources(x.data(),y.data(),sr,{});
    check(loss.gain()>.98f&&!loss.active(),"all missing smooth fail open");
    const std::array<AutoDuckerSource,2> duplicate{{{7,{a.data(),nullptr}},{7,{a.data(),nullptr}}}};
    loss.processSources(x.data(),y.data(),sr,duplicate);
    check(!loss.active()&&loss.gain()>.99f,"duplicate rejected");
    // Sparse 32-source roster must be PCM-identical to the one available
    // source, including release and stable-ID state after removing a key.
    AutoDuckerCore sparse,single;check(sparse.prepare(sr)&&single.prepare(sr),"sparse prepare");
    std::array<AutoDuckerSource,32> sparseKeys{};
    for(std::size_t k=0;k<sparseKeys.size();++k)sparseKeys[k].id=k+1;
    sparseKeys[18].key={a.data(),nullptr};
    const std::array<AutoDuckerSource,1> singleKey{{{19,{a.data(),nullptr}}}};
    std::fill(x.begin(),x.end(),1.f);std::fill(y.begin(),y.end(),-.5f);
    l=x;r=y;
    sparse.processSources(x.data(),y.data(),sr,sparseKeys);
    single.processSources(l.data(),r.data(),sr,singleKey);
    check(x==l&&y==r,"sparse 32 key PCM parity");
    sparse.processSources(x.data()+sr,y.data()+sr,2*sr,{});
    single.processSources(l.data()+sr,r.data()+sr,2*sr,{});
    check(x==l&&y==r&&sparse.gain()==1.f,"sparse release and idle unity parity");
    // After reset and without any available trigger, untouched audio is the
    // identity path, including signed zero.
    sparse.reset();float u[2]={-.0f,.25f},v[2]={.125f,-.75f};
    sparse.processSources(u,v,2,{});
    check(std::signbit(u[0])&&u[1]==.25f&&v[0]==.125f&&v[1]==-.75f,"idle identity");
    AutoDuckerCore::Prepared p;AutoDuckerSettings invalid;
    invalid.depthDb=-1;
    check(!AutoDuckerCore::prepareSettings(48000,invalid,p),"negative depth rejected");
    check(!AutoDuckerCore::prepareSettings(0,{},p),"invalid rate rejected");
    std::cout<<"Auto Ducker DSP: rate, partition, stereo, hold, release, key loss, finite input PASS\n";
}

} // namespace armix::ducker_test
