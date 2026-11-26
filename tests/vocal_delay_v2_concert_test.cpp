// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace armix::builtin_fx::vocal_delay;
extern std::atomic<bool> noHeap;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
static void run(ContinuousDelayCore& c,float* l,float* r,std::size_t n){noHeap=true;c.processStereo(l,r,n);noHeap=false;}
static void scenario(double rate,std::size_t preset){
    const auto p=presetSettings(concertPresetStart+preset);
    const auto n=std::size_t(rate*6);
    std::vector<float> dry(n),l(n),r(n),a(n),b(n),wetL(n),wetR(n);
    // Ordinary syllables, short breath, lone impulse, strong terminal note,
    // then next lyric. Synthetic, not human corpus.
    for(std::size_t i=0;i<n;++i){
        const auto t=double(i)/rate;float amp=0;
        if(t<1.2&&std::fmod(t,.24)<.18)amp=.07f;
        if(t>=1.65&&t<2.10)amp=.075f;
        if(t>=2.10&&t<2.55)amp=.34f;
        if(t>=4.2&&t<4.8)amp=.06f;
        dry[i]=amp*float(std::sin(6.283185307179586*190*t));
    }
    dry[std::size_t(rate*1.4)]=.8f;l=r=a=b=wetL=wetR=dry;
    ContinuousDelayCore x,z,wet(true);CHECK(x.prepare(rate,p)&&z.prepare(rate,p)&&wet.prepare(rate,p));
    run(wet,wetL.data(),wetR.data(),n);
    for(std::size_t i=0;i<n;){const auto k=std::min<std::size_t>(64,n-i);run(x,l.data()+i,r.data()+i,k);i+=k;}
    for(std::size_t i=0;i<n;){const auto k=std::min<std::size_t>(251,n-i);run(z,a.data()+i,b.data()+i,k);i+=k;}
    double tail=0,side=0;std::size_t first=n;
    for(std::size_t i=0;i<n;++i){
        CHECK(l[i]==dry[i]+wetL[i]&&r[i]==dry[i]+wetR[i]);
        if(i<std::size_t(rate*2.55)||i>=std::size_t(rate*4.23)||p.throwMode==ThrowMode::manual)
            CHECK(wetL[i]==0.f&&wetR[i]==0.f);
        CHECK(l[i]==a[i]&&r[i]==b[i]&&std::isfinite(l[i])&&std::abs(l[i])<=1.00001f&&std::abs(r[i])<=1.00001f);
        if(i<std::size_t(rate*2.55)||i>=std::size_t(rate*4.23)||p.throwMode==ThrowMode::manual)CHECK(l[i]==dry[i]&&r[i]==dry[i]);
        if(l[i]!=dry[i]||r[i]!=dry[i])first=std::min(first,i);
        if(i>=std::size_t(rate*2.55)&&i<std::size_t(rate*4.2)){tail+=std::abs(l[i]-dry[i])+std::abs(r[i]-dry[i]);side+=std::abs(l[i]-r[i]);}
    }
    if(p.throwMode!=ThrowMode::manual){CHECK(tail>rate*.0005&&side>rate*.0001);CHECK(first>=std::size_t(rate*2.65));}
    else CHECK(tail==0);
    std::printf("VD_THROW rate=%.0f preset=%s first_ms=%.1f tail=%.6f side=%.6f\n",rate,singerPresets[concertPresetStart+preset].name,1000.*first/rate,tail,side);
}
static void manualAndReset(){
    constexpr double rate=48000;auto p=presetSettings(concertPresetStart+7);ContinuousDelayCore c;
    CHECK(c.prepare(rate,p));std::vector<float> l(24000),r(24000);
    for(std::size_t i=0;i<l.size();++i)l[i]=r[i]=.15f*std::sin(float(i)*.025f);
    run(c,l.data(),r.data(),l.size());
    p.hit=true;ContinuousDelayCore::Prepared q;CHECK(ContinuousDelayCore::prepareEdit(p,rate,q));CHECK(c.applyPrepared(q));
    l.assign(l.size(),0);r=l;run(c,l.data(),r.data(),l.size());
    double sum=0;for(float v:l)sum+=std::abs(v);CHECK(sum>1.);
    c.invalidate();l.assign(l.size(),0);r=l;run(c,l.data(),r.data(),l.size());for(float v:l)CHECK(v==0.f);
}
static void rejectNoiseAndSwitch(){
    constexpr double rate=48000;auto p=presetSettings(concertPresetStart);
    ContinuousDelayCore c;CHECK(c.prepare(rate,p));
    const auto n=std::size_t(rate*5);std::vector<float> dry(n),l(n),r(n);
    std::uint32_t random=12345;
    for(std::size_t i=0;i<n;++i){
        const double time=double(i)/rate;
        if(time<1.0)dry[i]=.07f*float(std::sin(i*.025));
        if(time>=1.3&&time<1.35)dry[i]=.8f*float(std::sin(i*.025)); // short shout
        if(time>=1.7&&time<2.3){random=random*1664525u+1013904223u;dry[i]=.4f*(float(random>>8)/8388608.f-1.f);}
        if(time>=2.6&&time<3.7)dry[i]=.075f*float(std::sin(i*.025));
    }
    l=r=dry;run(c,l.data(),r.data(),n);CHECK(l==dry&&r==dry);
    auto prior=presetSettings(0);ContinuousDelayCore::Prepared q;
    CHECK(ContinuousDelayCore::prepareEdit(prior,rate,q)&&c.applyPrepared(q));
    l.assign(n,.2f);r=l;run(c,l.data(),r.data(),n);
    CHECK(ContinuousDelayCore::prepareEdit(p,rate,q)&&c.applyPrepared(q));
    l.assign(n,0);r=l;run(c,l.data(),r.data(),n);
    for(std::size_t i=std::size_t(rate*.025);i<n;++i)CHECK(l[i]==0.f&&r[i]==0.f);
}
static void rejectPrematureReturn(){
    constexpr double rate=48000;auto p=presetSettings(concertPresetStart);
    ContinuousDelayCore c;CHECK(c.prepare(rate,p));
    const auto n=std::size_t(rate*4);std::vector<float> dry(n),l(n),r(n);
    for(std::size_t i=0;i<n;++i){
        const double time=double(i)/rate;float amp=0;
        if(time<.65)amp=.07f;
        if(time>=.65&&time<1.1)amp=.34f;
        // Return after detector confirms but before a 420ms delay can sound.
        if(time>=1.30&&time<2.4)amp=.07f;
        dry[i]=amp*float(std::sin(i*.025));
    }
    l=r=dry;run(c,l.data(),r.data(),n);CHECK(l==dry&&r==dry);
}
static void staccatoIsNotClimax(double rate){
    auto p=presetSettings(concertPresetStart);ContinuousDelayCore c;
    CHECK(c.prepare(rate,p));const auto n=std::size_t(rate*4);
    std::vector<float> dry(n),l(n),r(n);
    for(std::size_t i=0;i<n;++i){
        const double time=double(i)/rate;float amp=0;
        if(time<.7)amp=.07f;
        // Five 60ms accents separated by 50ms, each well below
        // the minimum sustained climax. Total high time exceeds it.
        if(time>=.9&&time<1.45&&std::fmod(time-.9,.11)<.06)amp=.34f;
        if(time>=2.5&&time<3.0)amp=.07f;
        dry[i]=amp*float(std::sin(6.283185307179586*190*time));
    }
    l=r=dry;run(c,l.data(),r.data(),n);CHECK(l==dry&&r==dry);
}
static void continuousWetAndPower(double rate){
    auto p=presetSettings(0);
    ContinuousDelayCore wet(true),insert;
    CHECK(wet.prepare(rate,p)&&insert.prepare(rate,p));
    const auto n=std::size_t(rate*2);
    std::vector<float> dry(n),l(n),r(n),wl(n),wr(n);
    for(std::size_t i=0;i<n;++i)dry[i]=.16f*float(std::sin(i*.031));
    l=r=wl=wr=dry;
    run(wet,wl.data(),wr.data(),n);run(insert,l.data(),r.data(),n);
    double energy=0;
    for(std::size_t i=0;i<n;++i){CHECK(l[i]==dry[i]+wl[i]&&r[i]==dry[i]+wr[i]);energy+=std::abs(wl[i])+std::abs(wr[i]);}
    CHECK(wl[0]==0&&wr[0]==0&&energy>1);
    p.enabled=false;p.spillover=false;ContinuousDelayCore::Prepared q;
    CHECK(ContinuousDelayCore::prepareEdit(p,rate,q)&&wet.applyPrepared(q));
    wl=wr=dry;run(wet,wl.data(),wr.data(),n);
    for(std::size_t i=std::size_t(rate*.02);i<n;++i)CHECK(wl[i]==0.f&&wr[i]==0.f);
}
int main(){
    for(double rate:{8000.,44100.,48000.,96000.,192000.})continuousWetAndPower(rate);
    for(double rate:{8000.,44100.,48000.,96000.,192000.})staccatoIsNotClimax(rate);
    rejectPrematureReturn();
    rejectNoiseAndSwitch();
    for(double rate:{8000.,44100.,48000.,96000.,192000.})for(std::size_t i=0;i<concertPresetCount;++i)scenario(rate,i);
    manualAndReset();std::puts("PASS 40 concert cases, dry identity, selected tail, stereo, manual/reset, block invariance, no heap");
}
