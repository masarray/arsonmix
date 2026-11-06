// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/ChannelEqBank.h"
#include <array>
#include <atomic>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
static std::atomic<bool> denyHeap{false};
void* operator new(std::size_t n) {if(denyHeap.load()) std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {if(denyHeap.load())std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool v, const char* expression, int line) {if(!v){std::fprintf(stderr,"FAIL at %d: %s\n",line,expression);std::abort();}}
#define CHECK(e) require(bool(e),#e,__LINE__)
using namespace armix::builtin_fx;
int main() {
  ChannelEqBank bank;
  CHECK(!bank.ready());CHECK(!bank.queueBandEdit(0,2,Band{},1,1,1));
  CHECK(bank.prepare(48000));CHECK(!bank.prepare(44100));
  CHECK(bank.addedLatencySamples()==0);
  std::array<float,512> l{},r{};
  for(std::size_t i=0;i<l.size();++i){l[i]=0.1f*std::sin(i*0.13f);r[i]=-l[i];}
  const auto original=l;
  denyHeap.store(true);
  bank.render(0,l.data(),r.data(),l.size());
  denyHeap.store(false);
  CHECK(l==original); // EXACT OFF, with production render hook
  Band b{}; b.enabled=true;b.frequencyHz=1000;b.gainDb=9;b.q=1.0;
  CHECK(!bank.queueBandEdit(0,2,b,0,1,2));
  CHECK(!bank.queueBandEdit(0,2,b,1,0,2));
  CHECK(!bank.queueBandEdit(14,2,b,1,1,2));
  CHECK(!bank.queueBandEdit(0,8,b,1,1,2));
  b.frequencyHz=24000;CHECK(!bank.queueBandEdit(0,2,b,1,1,2));b.frequencyHz=1000;
  CHECK(bank.queueBandEdit(0,2,b,101,22,9));
  ChannelEqBank::Ack a{};
  CHECK(!bank.pollAck(0,a)); // QUEUED is not APPLIED
  denyHeap.store(true);
  bank.render(0,l.data(),r.data(),128);
  denyHeap.store(false);
  CHECK(bank.pollAck(0,a));
  CHECK(a.applied&&a.requestId==101&&a.deviceEpoch==22&&a.graphRevision==9);
  CHECK(a.lane==0&&a.slot==2&&a.accepted.gainDb==9);
  CHECK(!bank.pollAck(0,a));
  // Queue repeated automation while the previous 5ms transition is running.
  // New coefficients apply only after the old fade, with no RT trigonometry.
  b.gainDb=3;CHECK(bank.queueBandEdit(0,2,b,102,22,9));
  b.gainDb=-2;CHECK(bank.queueBandEdit(0,2,b,103,22,9));
  bank.render(0,l.data(),r.data(),32);
  CHECK(!bank.pollAck(0,a)); // previous fade still running
  for(int i=0;i<8;++i)bank.render(0,l.data(),r.data(),32);
  bank.render(0,l.data(),r.data(),32);
  CHECK(bank.pollAck(0,a)&&a.requestId==102);
  for(int i=0;i<8;++i)bank.render(0,l.data(),r.data(),32);
  bank.render(0,l.data(),r.data(),32);
  CHECK(bank.pollAck(0,a)&&a.requestId==103);
  // Demonstrate 14 truly independent EQ strips through one common call shape.
  for(std::size_t lane=1;lane<14;++lane){
    auto copy=original;
    denyHeap.store(true);
    bank.render(lane,copy.data(),r.data(),copy.size());
    denyHeap.store(false);
    CHECK(copy==original);
  }
  // Capacity-full must not lie about accepted UI/native state.
  for(int i=1;i<=32;++i)CHECK(bank.queueBandEdit(13,2,b,i,22,9));
  CHECK(!bank.queueBandEdit(13,2,b,33,22,9));
  // Audio/render must remain bounded even while commands are backpressured.
  denyHeap.store(true);
  bank.render(13,l.data(),r.data(),32);
  denyHeap.store(false);
  CHECK(bank.pollAck(13,a)&&a.requestId==1);
  std::puts("PASS: 14-lane native EQ; SPSC control->audio/ACK, no realtime heap");
}
