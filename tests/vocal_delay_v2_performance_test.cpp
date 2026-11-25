// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace armix::builtin_fx::vocal_delay;
extern std::atomic<bool> noHeap;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
static void process(ContinuousDelayCore& c,float* l,float* r,std::size_t n){noHeap=true;c.processStereo(l,r,n);noHeap=false;}
static void edit(ContinuousDelayCore& c,const Parameters& p,double rate){
    ContinuousDelayCore::Prepared q;CHECK(c.prepareEdit(p,rate,q));noHeap=true;CHECK(c.applyPrepared(q));noHeap=false;
}
int main(){
    Parameters p{};CHECK(setTempo(p,123.13,2)&&p.bpmQuarter==493&&valid(p));
    auto prior=p;CHECK(!setTempo(p,40,6)&&sameParameters(p,prior));CHECK(!setTempo(p,NAN,3));
    p.spillover=true;p.hit=true;p.adaptiveFocus=true;p.style=EchoStyle::multitap;
    State s{};s.continuous=p;StateCodec::Bytes bytes{};State out{};
    CHECK(StateCodec::encode(s,bytes)&&bytes[4]==5);
    CHECK(StateCodec::decode(bytes.data(),bytes.size(),out)&&!out.continuous.hit);
    p.hit=false;CHECK(sameParameters(p,out.continuous));
    auto invalid=bytes;invalid[43]|=128;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),out));
    invalid=bytes;invalid[8]=0;invalid[9]=0;invalid[10]=0;invalid[11]=0;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),out));
    freeTime(p,100);CHECK(valid(p)&&p.bpmQuarter==0&&p.division==3);
    p={};p.timeMs=100;p.echo=.4f;p.feedback=.4f;p.duck=0;p.highPassHz=p.lowPassHz=0;
    for(double rate:{8000.,44100.,48000.,96000.,192000.}){
        const auto n=std::size_t(rate*.1);
        for(bool spill:{false,true}){
            p.enabled=true;p.spillover=spill;p.hit=false;
            ContinuousDelayCore c;CHECK(c.prepare(rate,p));
            std::vector<float> l(n*3),r(l);l[0]=r[0]=.8f;
            process(c,l.data(),r.data(),n/2);
            p.enabled=false;edit(c,p,rate);
            // New impulse while OFF is dry only, never admitted to the delay.
            l[n/2]=r[n/2]=.6f;process(c,l.data()+n/2,r.data()+n/2,l.size()-n/2);
            CHECK(l[0]==.8f&&l[n/2]==.6f);
            CHECK(std::abs(l[n]-(spill?.32f:0.f))<1.e-5f);
            CHECK(l[n+n/2]==0.f&&l[2*n+n/2]==0.f);
            if(spill)CHECK(std::abs(l[2*n]-.128f)<1.e-5f);
            p.spillover=false;edit(c,p,rate);
            l.assign(n,0);r=l;process(c,l.data(),r.data(),n);
            // Feed fresh silence, not the previous block's tail output.
            l.assign(n,0);r=l;p.enabled=true;edit(c,p,rate);process(c,l.data(),r.data(),n);
            for(float x:l)CHECK(x==0.f);
        }
        p.enabled=true;p.spillover=false;p.hit=false;
        ContinuousDelayCore base,hit;CHECK(base.prepare(rate,p));p.hit=true;CHECK(hit.prepare(rate,p));
        auto effective=performanceSettings(p);CHECK(effective.echo>p.echo&&effective.feedback>p.feedback);
        std::vector<float> l(n*2+1),r(l),a(l),b(l);l[0]=r[0]=a[0]=b[0]=.8f;
        process(base,l.data(),r.data(),l.size());process(hit,a.data(),b.data(),a.size());
        CHECK(l[0]==a[0]&&a[n]>l[n]&&a[2*n]>l[2*n]);p.hit=false;
        CHECK(performanceSettings(p).echo==p.echo&&performanceSettings(p).feedback==p.feedback);
    }
    // Bounded spill even when settings keep changing while OFF.
    p.enabled=true;p.spillover=true;p.feedback=.9f;p.timeMs=20;
    ContinuousDelayCore c;CHECK(c.prepare(8000,p));
    std::vector<float> l(8000),r(l);l[0]=r[0]=1;process(c,l.data(),r.data(),64);
    p.enabled=false;edit(c,p,8000);
    for(int i=0;i<32;++i){l.assign(8000,0);r=l;edit(c,p,8000);process(c,l.data(),r.data(),l.size());}
    CHECK(c.meter().wetPeak==0);
    NativeSession session;State st{};st.continuous=p;st.continuous.enabled=true;
    CHECK(session.prepareStopped(48000,{1,2,3,4},st));auto q=st.continuous;q.hit=true;
    CHECK(session.submit({{1,2,3,4},1,q})==Submit::queued);session.consumeRealtimeEdits();
    Receipt receipt{};CHECK(session.poll(receipt)&&receipt.accepted.hit);
    CHECK(session.save(bytes)&&StateCodec::decode(bytes.data(),bytes.size(),out)&&!out.continuous.hit);
    CHECK(session.restartStopped(48000,{1,2,4,5})&&!session.acceptedSnapshot().accepted.hit);
    std::puts("PASS performance tempo persistence, transient HIT, wet-only bounded spillover, old power and no heap");
}
