// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/NativeSession.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
using namespace armix::builtin_fx::vocal_delay;
extern std::atomic<bool> noHeap;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
static void codec(){
    // Independent literal JUCE AVTH v1 fixture: Hybrid, 120 BPM, quarter,
    // 3 repeats, 460ms, 220ms, -35dB, .42, .55, .7.
    const StateCodec::Bytes old{{0x48,0x54,0x56,0x41,1,0,0,0,
      2,0,0,0,0,0,0xf0,0x42,0,0,0,0,3,0,0,0,
      0,0,0xe6,0x43,0,0,0x5c,0x43,0,0,0x0c,0xc2,
      0x3d,0x0a,0xd7,0x3e,0xcd,0xcc,0x0c,0x3f,0x33,0x33,0x33,0x3f}};
    State decoded{};StateCodec::Bytes bytes{};
    CHECK(StateCodec::decode(old.data(),old.size(),decoded));
    CHECK(decoded.kind==State::Kind::legacyThrow&&decoded.legacy.mode==2&&decoded.legacy.bpm==120.f);
    CHECK(StateCodec::encode(decoded,bytes)&&bytes==old);
    State s{};s.continuous.timeMs=1345.25f;s.continuous.mode=StereoMode::pingPong;s.continuous.enabled=false;
    CHECK(StateCodec::encode(s,bytes)&&bytes[4]==2);
    CHECK(StateCodec::decode(bytes.data(),bytes.size(),decoded)&&decoded.kind==State::Kind::continuous);
    CHECK(decoded.continuous.timeMs==1345.25f&&decoded.continuous.mode==StereoMode::pingPong&&!decoded.continuous.enabled);
    const auto good=bytes;
    for(std::size_t n=0;n<48;++n)CHECK(!StateCodec::decode(bytes.data(),n,decoded));
    bytes[4]=3;CHECK(!StateCodec::decode(bytes.data(),48,decoded));bytes=good;
    bytes[40]=3;CHECK(!StateCodec::decode(bytes.data(),48,decoded));bytes=good;
    bytes[44]=2;CHECK(!StateCodec::decode(bytes.data(),48,decoded));bytes=good;
    bytes[8]=0;bytes[9]=0;bytes[10]=0xc0;bytes[11]=0x7f;
    CHECK(!StateCodec::decode(bytes.data(),48,decoded));
    CHECK(decoded.continuous.timeMs==1345.25f); // transactional failure
}
static void session(){
    NativeSession n;State s{};s.continuous.timeMs=20;s.continuous.echo=.5f;
    s.continuous.feedback=0;s.continuous.duck=0;s.continuous.highPassHz=0;s.continuous.lowPassHz=0;
    Lease l{1,2,3,4};CHECK(n.prepareStopped(48000,l,s));
    auto p=s.continuous;p.timeMs=30;
    CHECK(n.submit({{1,9,3,4},1,p})==Submit::stale);
    CHECK(n.submit({l,1,p})==Submit::queued);
    CHECK(n.submit({l,2,p})==Submit::busy);
    StateCodec::Bytes bytes{};CHECK(!n.save(bytes));
    CHECK(n.acceptedSnapshot().revision==0&&n.acceptedSnapshot().lastSubmitted==1&&!n.acceptedSnapshot().quiescent);
    noHeap=true;n.consumeRealtimeEdits();noHeap=false;
    CHECK(!n.save(bytes)); // applied but receipt has not reached control
    Receipt r{};CHECK(n.poll(r)&&r.applied&&r.revision==1&&r.accepted.timeMs==30);
    CHECK(n.save(bytes));CHECK(n.submit({l,1,p})==Submit::stale);
    p.timeMs=40;CHECK(n.submit({l,2,p})==Submit::queued);
    noHeap=true;n.consumeRealtimeEdits();noHeap=false;
    Lease next{1,2,4,5};CHECK(n.restartStopped(96000,next));
    CHECK(n.acceptedSnapshot().accepted.timeMs==40&&!n.poll(r));
    CHECK(n.submit({l,3,p})==Submit::stale);
    p.timeMs=50;CHECK(n.submit({next,1,p})==Submit::queued);
    // Unconsumed old edit does not survive a device restart.
    Lease third{1,2,5,6};CHECK(n.restartStopped(48000,third));
    CHECK(n.acceptedSnapshot().accepted.timeMs==40);
    CHECK(n.submit({third,1,p})==Submit::queued);
    n.onRealtimeBypass(true,true);n.consumeRealtimeEdits();CHECK(n.poll(r)&&r.applied);
    n.onRealtimeBypass(false,false);
    std::array<float,4000> a{},b{};a[0]=b[0]=.8f;
    noHeap=true;n.processStereo(a.data(),b.data(),a.size());noHeap=false;
    CHECK(a[0]==.8f&&a[2400]>.39f&&a[2400]<.41f);
    CHECK(!n.restartStopped(48000,l));CHECK(!n.prepareStopped(48000,third,s)); // cannot reset revision on same lease
}
static void concurrent(){
    NativeSession n;State s{};Lease l{9,8,7,6};CHECK(n.prepareStopped(48000,l,s));
    constexpr std::uint32_t count=20000;
    std::atomic<bool> stop{false};
    std::thread audio([&]{std::array<float,64> a{},b{};while(!stop.load(std::memory_order_acquire)){
        n.processStereo(a.data(),b.data(),a.size());
    }});
    Receipt r{};
    for(std::uint32_t i=1;i<=count;++i){
        auto p=s.continuous;p.timeMs=20.f+float(i%3000);p.echo=float(i%100)/100.f;
        CHECK(n.submit({l,i,p})==Submit::queued);
        while(!n.poll(r))std::this_thread::yield();
        CHECK(r.applied&&r.revision==i&&r.accepted.timeMs==p.timeMs&&r.accepted.echo==p.echo);
    }
    stop.store(true,std::memory_order_release);audio.join();CHECK(n.quiescent());
}
int main(){codec();session();concurrent();std::puts("PASS VD2 legacy bytes, V2 state, native revision/epoch, applied restart, 20000 concurrent receipts");}
