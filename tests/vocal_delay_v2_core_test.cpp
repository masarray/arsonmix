// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/ContinuousDelayCore.h"
#include "../engine/builtin_fx/VocalThrow.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>
using namespace armix::builtin_fx::vocal_delay;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
extern std::atomic<bool> noHeap;
static bool near(float a,float b,float e=1.e-6f){return std::abs(a-b)<=e;}
static Parameters transparent(){Parameters p;p.timeMs=100;p.feedback=.5f;p.echo=.5f;p.highPassHz=0;p.lowPassHz=0;p.duck=0;return p;}
static void process(ContinuousDelayCore& c,float* l,float* r,std::size_t n){
    noHeap=true;c.processStereo(l,r,n);noHeap=false;
}
static void edit(ContinuousDelayCore& c,const Parameters& p,double rate){
    ContinuousDelayCore::Prepared q;CHECK(c.prepareEdit(p,rate,q));
    noHeap=true;const bool ok=c.applyPrepared(q);noHeap=false;CHECK(ok);
}
static void impulse(double rate,StereoMode mode,float feedback,float ms){
    auto p=transparent();p.mode=mode;p.feedback=feedback;p.timeMs=ms;
    ContinuousDelayCore c;CHECK(c.prepare(rate,p));
    const auto d=std::size_t(std::llround(rate*ms*.001));
    const auto count=d*8+4;
    std::vector<float> l(count),r(count);l[0]=.8f;
    for(std::size_t i=0;i<count;){auto n=std::min<std::size_t>(127,count-i);process(c,l.data()+i,r.data()+i,n);i+=n;}
    CHECK(l[0]==.8f&&r[0]==0.f);
    for(std::size_t i=1;i<count;++i){
        float a=0,b=0;
        if(i%d==0){const int repeat=int(i/d)-1;float v=.4f*std::pow(feedback,repeat);
            if(mode==StereoMode::mono)a=b=v*.5f;
            else if(mode==StereoMode::pingPong&&repeat%2)b=v;
            else a=v;
        }
        CHECK(near(l[i],a)&&near(r[i],b));
    }
}
static void fractionalAndWrap(){
    auto p=transparent();p.timeMs=20.0625f;p.feedback=0;
    ContinuousDelayCore c;CHECK(c.prepare(8000,p));
    std::vector<float> l(70000),r(l.size());
    l[0]=1;l[40000]=1;
    process(c,l.data(),r.data(),l.size());
    CHECK(near(l[160],.25f)&&near(l[161],.25f));
    CHECK(near(l[40160],.25f)&&near(l[40161],.25f));
    CHECK(l[159]==0&&l[162]==0);
}
static void continuousVsLegacy(double rate){
    auto p=transparent();p.feedback=0;
    ContinuousDelayCore c;CHECK(c.prepare(rate,p));
    armix::builtin_fx::VocalThrow old;CHECK(old.prepare(rate));
    const auto n=std::size_t(rate*.9);
    std::vector<float> l(n,.4f),r(n,.4f),ol(l),orr(r);
    process(c,l.data(),r.data(),n);
    old.processStereo(ol.data(),orr.data(),n);
    CHECK(old.throwCount()==0);
    const auto d=std::size_t(std::llround(rate*.1));
    CHECK(near(l[d-1],.4f)&&near(l[d],.6f)&&near(l.back(),.6f));
    CHECK(ol.back()==.4f&&orr.back()==.4f);
}
static void blocksAndEdits(){
    constexpr double rate=48000;constexpr std::size_t n=48000;
    auto p=Parameters{};p.timeMs=25;p.feedback=.85f;p.echo=.8f;
    ContinuousDelayCore a,b;CHECK(a.prepare(rate,p)&&b.prepare(rate,p));
    std::vector<float> al(n),ar(n),bl(n),br(n);
    for(std::size_t i=0;i<n;++i){al[i]=bl[i]=.25f*std::sin(float(i)*.07f);ar[i]=br[i]=.19f*std::sin(float(i)*.042f);}
    for(std::size_t start=0;start<n;start+=4000){
        p.timeMs=25.f+float(start%17000)*.03f;p.mode=(start/4000)%2?StereoMode::pingPong:StereoMode::stereo;
        p.echo=(start/4000)%2?.8f:.2f;
        edit(a,p,rate);edit(b,p,rate);
        process(a,al.data()+start,ar.data()+start,4000);
        for(std::size_t j=start;j<start+4000;){auto q=std::min<std::size_t>((j%193)+1,start+4000-j);process(b,bl.data()+j,br.data()+j,q);j+=q;}
    }
    for(std::size_t i=0;i<n;++i){CHECK(al[i]==bl[i]&&ar[i]==br[i]);CHECK(std::isfinite(al[i])&&std::abs(al[i])<4);}
}
static void bypassAndReset(){
    auto p=transparent();p.timeMs=100;p.feedback=.8f;
    ContinuousDelayCore c;CHECK(c.prepare(48000,p));
    std::vector<float> l(5000),r(5000);l[0]=r[0]=1;process(c,l.data(),r.data(),100);
    p.enabled=false;edit(c,p,48000);
    std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);
    process(c,l.data(),r.data(),l.size());
    p.enabled=true;edit(c,p,48000);
    process(c,l.data(),r.data(),l.size());
    for(float x:l)CHECK(x==0.f);
    p.echo=0;CHECK(c.prepare(48000,p));l.assign(5000,.123f);r.assign(5000,-.34f);
    process(c,l.data(),r.data(),l.size());for(std::size_t i=0;i<l.size();++i)CHECK(l[i]==.123f&&r[i]==-.34f);
    p.echo=.5f;CHECK(c.prepare(48000,p));l.assign(5000,0);l[0]=.8f;
    process(c,l.data(),l.data(),l.size());CHECK(near(l[4800],.4f));
}
static void invalidAndDucking(){
    ContinuousDelayCore c;ContinuousDelayCore::Prepared q;auto p=transparent();
    CHECK(!c.applyPrepared(q));CHECK(!c.prepare(7999,p));CHECK(c.prepare(48000,p));
    CHECK(!c.applyPrepared(q));CHECK(c.prepareEdit(p,44100,q));CHECK(!c.applyPrepared(q));
    p.feedback=.91f;CHECK(!c.prepareEdit(p,48000,q));p.feedback=.5f;
    p.timeMs=std::numeric_limits<float>::quiet_NaN();CHECK(!c.prepareEdit(p,48000,q));
    p=transparent();p.duck=1;CHECK(c.prepare(48000,p));
    std::vector<float> l(14400,.4f),r(l);
    process(c,l.data(),r.data(),l.size());
    CHECK(l.back()>.4f&&l.back()<.45f);
    std::array<float,4> bad{{NAN,INFINITY,-INFINITY,0}},badR=bad;
    process(c,bad.data(),badR.data(),bad.size());for(float x:bad)CHECK(std::isfinite(x));
}
static void stagePingPong(double rate){
    auto p=transparent();p.smartStage=true;p.mode=StereoMode::pingPong;
    ContinuousDelayCore c;CHECK(c.prepare(rate,p));
    const auto d=std::size_t(rate*.1);std::vector<float> l(d*5+1),r(l);
    l[0]=r[0]=.8f;process(c,l.data(),r.data(),l.size());
    CHECK(l[0]==.8f&&r[0]==.8f);
    for(std::size_t k=1;k<=5;++k){
        const float v=.4f*std::pow(.5f,int(k-1));
        CHECK(near(l[k*d],k%2?v:0)&&near(r[k*d],k%2?0:v));
    }
    // A true mono output fold must stay positive, never cancel.
    CHECK(c.prepare(rate,p));l.assign(d*3+1,0.f);l[0]=.8f;
    process(c,l.data(),l.data(),l.size());
    CHECK(near(l[d],.2f)&&near(l[2*d],.1f));
}
static void stagePhrase(double rate){
    auto p=transparent();p.duck=.95f;p.adaptiveFocus=true;p.duckAttackMs=5;p.duckReleaseMs=110;
    ContinuousDelayCore prior,stage;CHECK(prior.prepare(rate,p));p.smartStage=true;CHECK(stage.prepare(rate,p));
    const auto n=std::size_t(rate*2.),end=std::size_t(rate*1.2),gap=std::size_t(rate*.04);
    std::vector<float> l(n),r(n),a(n),b(n),dry(n);
    for(std::size_t i=0;i<end;++i){
        if(i>=std::size_t(rate*.8)&&i<std::size_t(rate*.8)+gap)continue;
        dry[i]=.2f*std::sin(float(i)*.071f);
    }
    l=r=a=b=dry;
    for(std::size_t i=0;i<n;){const auto k=std::min<std::size_t>(127,n-i);
        process(prior,l.data()+i,r.data()+i,k);process(stage,a.data()+i,b.data()+i,k);i+=k;}
    double oldWet=0,newWet=0,shortGap=0,tail=0;
    for(std::size_t i=std::size_t(rate*.5);i<n;++i){
        CHECK(std::isfinite(a[i])&&std::isfinite(b[i]));
        if(i<end){oldWet+=std::abs(l[i]-dry[i]);newWet+=std::abs(a[i]-dry[i]);}
        if(i>=std::size_t(rate*.8)&&i<std::size_t(rate*.8)+gap)shortGap+=std::abs(a[i]);
        if(i>=end&&i<end+std::size_t(rate*.3))tail+=std::abs(a[i]);
    }
    CHECK(newWet<oldWet*.75&&tail>0.001&&shortGap<tail);
    p.echo=0;CHECK(stage.prepare(rate,p));a=b=dry;process(stage,a.data(),b.data(),n);CHECK(a==dry&&b==dry);
}
int main(){
    CHECK(ContinuousDelayCore::addedLatencySamples==0);
    for(double rate:{8000.,44100.,48000.,96000.,192000.}){
        impulse(rate,StereoMode::stereo,.5f,100);
        impulse(rate,StereoMode::stereo,0,100);
        impulse(rate,StereoMode::mono,.5f,100);
        impulse(rate,StereoMode::pingPong,.5f,100);
        stagePingPong(rate);stagePhrase(rate);
        continuousVsLegacy(rate);
    }
    impulse(8000,StereoMode::stereo,.5f,4000);
    fractionalAndWrap();blocksAndEdits();bypassAndReset();invalidAndDucking();
    ContinuousDelayCore c;CHECK(c.prepare(192000));CHECK(c.ringBytes()==(768000+2)*2*sizeof(float));
    std::puts("PASS Vocal Delay V2 continuous PCM, legacy contrast, timing, stereo, no-heap, bypass, block invariance");
}
