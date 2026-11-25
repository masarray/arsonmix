// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace armix::builtin_fx::vocal_delay;
extern std::atomic<bool> noHeap;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
static void run(ContinuousDelayCore& c,float* l,float* r,std::size_t n){
    noHeap=true;c.processStereo(l,r,n);noHeap=false;
}
static void scenario(double rate,float amplitude){
    Parameters p{};p.timeMs=100;p.feedback=.7f;p.echo=.5f;p.duck=.75f;
    p.highPassHz=p.lowPassHz=0;p.duckAttackMs=10;p.duckReleaseMs=100;
    ContinuousDelayCore classic,adaptive,muted;
    CHECK(classic.prepare(rate,p));p.adaptiveFocus=true;CHECK(adaptive.prepare(rate,p));
    p.echo=0;CHECK(muted.prepare(rate,p));
    const auto n=std::size_t(rate*1.2);
    std::vector<float> l(n),r(n),a(n),b(n),dry(n),dr(n);
    const auto phrase=std::size_t(rate*.5);
    for(std::size_t i=0;i<n;++i){
        const float input=i<phrase?amplitude*float(std::sin(6.283185307179586*200.*double(i)/rate)):0.f;
        l[i]=r[i]=a[i]=b[i]=dry[i]=dr[i]=input;
    }
    double classicWet=0,adaptiveWet=0,lateWet=0;
    const auto from=std::size_t(rate*.3);
    for(std::size_t i=0;i<n;){
        const auto count=std::min<std::size_t>(64,n-i);
        run(classic,l.data()+i,r.data()+i,count);run(adaptive,a.data()+i,b.data()+i,count);run(muted,dry.data()+i,dr.data()+i,count);
        for(std::size_t k=i;k<i+count;++k){
            const float expected=k<phrase?amplitude*float(std::sin(6.283185307179586*200.*double(k)/rate)):0.f;
            CHECK(dry[k]==expected&&dr[k]==expected&&std::isfinite(a[k]));
            if(k>=from&&k<phrase){classicWet+=std::abs(l[k]-expected);adaptiveWet+=std::abs(a[k]-expected);}
            if(k>=std::size_t(rate*.7))lateWet+=std::abs(a[k]);
        }
        i+=count;
    }
    CHECK(lateWet>0.0001); // wet tail survives phrase and detector, no gate
    if(amplitude<=.04f)CHECK(adaptiveWet<classicWet*.65);
    else CHECK(adaptiveWet<=classicWet*1.25);
    std::printf("FOCUS rate=%.0f amp=%.3f during_classic=%.6f during_adaptive=%.6f ratio=%.4f tail=%.6f\n",
        rate,amplitude,classicWet,adaptiveWet,adaptiveWet/classicWet,lateWet);
}
int main(){
    for(double rate:{8000.,44100.,48000.,96000.,192000.})for(float amplitude:{.035f,.3f})scenario(rate,amplitude);
    Parameters p{};p.timeMs=20;p.echo=.5f;p.feedback=0;p.highPassHz=p.lowPassHz=0;p.duck=0;p.adaptiveFocus=true;
    State s{};s.continuous=p;StateCodec::Bytes bytes{};State decoded{};
    CHECK(StateCodec::encode(s,bytes)&&bytes[4]==4&&bytes[42]==1);
    CHECK(StateCodec::decode(bytes.data(),bytes.size(),decoded)&&sameParameters(p,decoded.continuous));
    auto invalid=bytes;invalid[42]=2;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),decoded));
    invalid=bytes;invalid[4]=2;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),decoded));
    p.adaptiveFocus=false;s.continuous=p;CHECK(StateCodec::encode(s,bytes)&&bytes[4]==2);
    CHECK(StateCodec::decode(bytes.data(),bytes.size(),decoded)&&!decoded.continuous.adaptiveFocus);
    p.style=EchoStyle::dual;s.continuous=p;CHECK(StateCodec::encode(s,bytes)&&bytes[4]==3);
    CHECK(StateCodec::decode(bytes.data(),bytes.size(),decoded)&&!decoded.continuous.adaptiveFocus);
    p.style=EchoStyle::classic;p.adaptiveFocus=true;ContinuousDelayCore core;CHECK(core.prepare(48000,p));
    std::array<float,1000> l{},r{};l[0]=r[0]=.8f;run(core,l.data(),r.data(),l.size());
    const auto m=core.meter();CHECK(std::abs(m.wetPeak-.4f)<.002f&&m.duckReduction==0);
    core.invalidate();const auto reset=core.meter();CHECK(reset.wetPeak==0&&reset.duckReduction==0&&reset.sequence!=m.sequence);
    p.duck=.9f;p.feedback=.9f;CHECK(core.prepare(48000,p));
    for(int b=0;b<200;++b){
        p.adaptiveFocus=(b%2)==0;ContinuousDelayCore::Prepared q;CHECK(core.prepareEdit(p,48000,q));
        noHeap=true;CHECK(core.applyPrepared(q));noHeap=false;
        l.fill(.035f);r.fill(.035f);run(core,l.data(),r.data(),l.size());
        const auto m2=core.meter();CHECK(m2.duckReduction>=0&&m2.duckReduction<=1);
        for(float x:l)CHECK(std::isfinite(x)&&std::abs(x)<9);
    }
    std::puts("PASS adaptive soft/loud vocal focus, dry identity, tail release, V2/V3/V4 state, coherent block meters, no heap and transitions");
}
