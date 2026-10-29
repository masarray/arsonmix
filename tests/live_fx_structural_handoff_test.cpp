// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/LiveGraphHandoff.h"
#include "../engine/core/FxMoveRamp.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <cstdio>
#include <cstdint>
static void check(bool ok){if(!ok){std::fputs("FX_STRUCTURAL_ACK_FAIL\n",stderr);std::abort();}}
struct Processor {
  explicit Processor(int id,std::atomic<int>& deleted):id(id),deleted(deleted){}
  ~Processor(){deleted.fetch_add(1);}
  int id;
  std::atomic<int>& deleted;
};
struct Graph {
  std::array<std::shared_ptr<Processor>,8> nodes;
  int size=0;
};
int main(){
  std::atomic<int> deleted{0};
  std::atomic<int> released{0};
  // Simulate vendor releaseResources exactly once when final CONTROL owner drops.
  auto newVendor=[&](int id){return std::shared_ptr<Processor>(
    new Processor(id,deleted),[&](Processor* p){released.fetch_add(1);delete p;});};
  auto original=std::make_unique<Graph>();
  original->nodes[0]=newVendor(1);
  original->size=1;
  armix::rt::LiveGraphHandoff<Graph> publisher;
  publisher.initialize(original.get());
  // Thousands of ADD/REMOVE transactions in two buffer boundaries.
  // Retained vendor objects must NEVER destruct when the old graph retires.
  for(int k=0;k<2000;++k){
    auto staged=std::make_unique<Graph>();
    staged->nodes[0]=original->nodes[0];
    staged->size=1;
    if((k%2)==0) {
      staged->nodes[1]=newVendor(k+2);
      staged->size=2;
    }
    check(publisher.stage(staged.get()));
    auto first=publisher.beginCycle();
    check(first.graph==original.get() && first.fadeOut && !first.fadeIn);
    check(armix::dsp::FxMoveRamp::gain(1,127,128)==0.f);
    check(deleted.load()==k/2); // removed nodes already retired on prior round
    publisher.endCycle(first); // RT does pointer exchange only
    auto second=publisher.beginCycle();
    check(second.graph==staged.get() && second.fadeIn);
    check(armix::dsp::FxMoveRamp::gain(2,0,128)==0.f);
    check(publisher.takeRetired()==original.get());
    original=std::move(staged);
    publisher.completeRetirement();
  }
  original.reset();
  check(deleted.load()==1001);
  check(released.load()==1001); // never two releases of reused processor
  // Failed stage on control thread must also release a prepared vendor instance.
  { auto cancelled=newVendor(9999); check(released.load()==1001); }
  check(released.load()==1002);
}
