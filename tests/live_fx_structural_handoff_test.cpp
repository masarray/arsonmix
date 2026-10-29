// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/LiveGraphHandoff.h"
#include "../engine/core/FxMoveRamp.h"
#include <array>
#include <atomic>
#include <cassert>
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
  auto original=std::make_unique<Graph>();
  original->nodes[0]=std::make_shared<Processor>(1,deleted);
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
      staged->nodes[1]=std::make_shared<Processor>(k+2,deleted);
      staged->size=2;
    }
    check(publisher.stage(staged.get()));
    auto first=publisher.beginCycle();
    check(first.graph==original.get() && first.fadeOut && !first.fadeIn);
    check(armix::dsp::FxMoveRamp::gain(1,127,128)==0.f);
    check(deleted.load()==k/2-((k%2)==0?0:0)); // removed nodes already retired on prior round
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
}
