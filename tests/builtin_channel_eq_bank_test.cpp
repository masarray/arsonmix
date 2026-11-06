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
  CHECK(a.lane==0&&a.slot==2&&a.accepted.gainDb==9&&!a.gate);
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
  CHECK(bank.queueGateEdit(0,true,false,200,22,9));
  for(int i=0;i<8;++i)bank.render(0,l.data(),r.data(),64);
  CHECK(bank.pollAck(0,a)&&a.applied&&a.gate&&a.installed&&!a.bypassed);
  CHECK(bank.queueGateEdit(0,true,true,201,22,9));
  for(int i=0;i<8;++i)bank.render(0,l.data(),r.data(),64);
  CHECK(bank.pollAck(0,a)&&a.applied&&a.bypassed);
  CHECK(bank.queueGateEdit(0,false,false,202,22,9));
  for(int i=0;i<8;++i)bank.render(0,l.data(),r.data(),64);
  CHECK(bank.pollAck(0,a)&&a.applied&&!a.installed);
  CHECK(!bank.queueGateEdit(14,true,false,203,22,9));
  // Muted/no-output periods do not process an EQ command. It must remain
  // QUEUED until a real audio owner consumes it, not expire on UI wall time.
  CHECK(bank.queueGateEdit(12,true,false,204,22,9));
  CHECK(!bank.pollAck(12,a));
  CHECK(!bank.pollAck(12,a)); // no callback block, still not applied
  bank.render(12,l.data(),r.data(),128,22,9);
  CHECK(bank.pollAck(12,a)&&a.applied&&a.requestId==204&&a.gate);
  CHECK(!bank.pollAck(12,a));
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
  // Native Show V5 recall pre-seeds a NEW unpublished EQ graph: ensure
  // saved bypass preserves bands and the audio owner processes real PCM
  // when that restored insert is enabled. No live graph is mutated.
  ChannelEqBank restored;
  CHECK(restored.prepare(48000));
  Band stored{};stored.enabled=true;stored.frequencyHz=1000.f;
  stored.gainDb=9.f;stored.q=1.f;
  CHECK(restored.setBandOffline(3,2,stored));
  CHECK(restored.setGateOffline(3,true,true)); // saved BYPASS
  std::array<float,128> reference{},out{},silent{};
  double dryEnergy=0,wetEnergy=0;
  for(int block=0;block<64;++block) {
    for(int i=0;i<128;++i)
      reference[static_cast<std::size_t>(i)]=
        static_cast<float>(.1*std::sin(6.283185307179586*
            1000.0*(block*128+i)/48000.0));
    out=reference;
    restored.render(3,out.data(),silent.data(),128);
    CHECK(out==reference); // restored bypass must be bitwise dry
  }
  CHECK(restored.setGateOffline(3,true,false)); // offline only, new candidate
  for(int block=0;block<64;++block) {
    for(int i=0;i<128;++i)
      reference[static_cast<std::size_t>(i)]=
        static_cast<float>(.1*std::sin(6.283185307179586*
            1000.0*(block*128+i)/48000.0));
    out=reference;
    restored.render(3,out.data(),silent.data(),128);
    if(block>=48)for(int i=0;i<128;++i) {
      dryEnergy+=reference[static_cast<std::size_t>(i)]*
                 reference[static_cast<std::size_t>(i)];
      wetEnergy+=out[static_cast<std::size_t>(i)]*
                 out[static_cast<std::size_t>(i)];
    }
  }
  CHECK(dryEnergy>0.001&&wetEnergy>dryEnergy*4);
  CHECK(!restored.setGateOffline(14,true,false));
  // Rack OFF: exact dry PCM once the 5ms fade completes, while the EQ's
  // own ON preference and eight bands remain unchanged.
  CHECK(restored.setRackBypass(3,true));
  for(int block=0;block<10;++block){
    for(int i=0;i<128;++i)
      reference[static_cast<std::size_t>(i)]=
        static_cast<float>(.1*std::sin(6.283185307179586*
            1000.0*(block*128+i)/48000.0));
    out=reference;
    restored.render(3,out.data(),silent.data(),128);
    if(block>=6)CHECK(out==reference);
  }
  CHECK(restored.rackBypassed(3));
  CHECK(restored.setRackBypass(3,false));
  double resumedEnergy=0,dryResumed=0;
  for(int block=0;block<64;++block){
    for(int i=0;i<128;++i)
      reference[static_cast<std::size_t>(i)]=
        static_cast<float>(.1*std::sin(6.283185307179586*
            1000.0*(block*128+i)/48000.0));
    out=reference;
    restored.render(3,out.data(),silent.data(),128);
    if(block>=48)for(int i=0;i<128;++i){
      resumedEnergy+=out[static_cast<std::size_t>(i)]*
                     out[static_cast<std::size_t>(i)];
      dryResumed+=reference[static_cast<std::size_t>(i)]*
                  reference[static_cast<std::size_t>(i)];
    }
  }
  CHECK(resumedEnergy>dryResumed*4);
  CHECK(!restored.rackBypassed(3)&&!restored.setRackBypass(14,true));
  std::puts("PASS: 14-lane EQ, Show V5 offline restore/bypass PCM, SPSC ACK, no RT heap");
}
