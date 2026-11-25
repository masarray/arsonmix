// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace armix::builtin_fx::vocal_delay;
extern std::atomic<bool> noHeap;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
static void run(ContinuousDelayCore& c,std::vector<float>& l,std::vector<float>& r){
    noHeap=true;c.processStereo(l.data(),r.data(),l.size());noHeap=false;
}
int main(){
    Parameters p{};p.timeMs=100;p.feedback=0;p.echo=.5f;p.duck=0;p.highPassHz=0;p.lowPassHz=0;
    for(auto style:{EchoStyle::classic,EchoStyle::voiced,EchoStyle::dual,EchoStyle::multitap,EchoStyle::tape}){
        p.style=style;State s{};s.continuous=p;StateCodec::Bytes bytes{};State out{};
        CHECK(StateCodec::encode(s,bytes)&&bytes[4]==(style==EchoStyle::classic?2:3));
        CHECK(StateCodec::decode(bytes.data(),bytes.size(),out)&&sameParameters(p,out.continuous));
        if(style!=EchoStyle::classic){auto invalid=bytes;invalid[41]=9;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),out));}
        ContinuousDelayCore c;CHECK(c.prepare(48000,p));
        std::vector<float> l(10000),r(l);l[0]=r[0]=.8f;run(c,l,r);
        CHECK(l[0]==.8f&&r[0]==.8f);
        auto near=[](float a,float b){return std::abs(a-b)<1.e-6f;};
        if(style==EchoStyle::dual){CHECK(near(l[4800],.4f)&&near(r[3600],.4f)&&r[4800]==0);}
        else if(style==EchoStyle::multitap){CHECK(near(l[2400],.10f)&&near(l[3600],.04f)&&near(l[4800],.26f));}
        else if(style==EchoStyle::tape){CHECK(l[4800]>.25f&&l[4800]<.4f);}
        else CHECK(near(l[4800],.4f));
        for(auto x:l)CHECK(std::isfinite(x));
    }
    p.style=EchoStyle::voiced;p.lowPassHz=1000;
    ContinuousDelayCore c;CHECK(c.prepare(48000,p));std::vector<float> l(10000),r(l);l[0]=r[0]=.8f;
    run(c,l,r);CHECK(l[4800]>0&&l[4800]<.1f); // first echo is actually filtered
    p.feedback=.9f;p.echo=1;p.timeMs=20;p.duck=.3f;
    for(int b=0;b<300;++b){
        p.style=static_cast<EchoStyle>(b%5);p.timeMs=20.f+float(b%20);
        ContinuousDelayCore::Prepared edit{};CHECK(c.prepareEdit(p,48000,edit));
        noHeap=true;CHECK(c.applyPrepared(edit));noHeap=false;
        l.assign(127,.2f);r.assign(127,-.15f);run(c,l,r);
        for(float x:l)CHECK(std::isfinite(x)&&std::abs(x)<9);
    }
    std::puts("PASS signature wet algorithms: V2/V3 compatibility, first echo tone, asymmetric dual, three taps, saturation, no heap and transitions");
}
