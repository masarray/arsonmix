// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/FxBypassEnvelope.h"
#include <cmath>
#include <iostream>
static int failures=0;
static void check(bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL "<<name<<"\n";}}
int main() {
 using armix::dsp::FxBypassEnvelope;
 // Hard bypass must not change gain abruptly. 48 kHz * 8 ms = 384 samples.
 FxBypassEnvelope e;e.prepare(false,48000,0);
 check(!e.transitioning() && e.shouldProcessWet(),"steady wet fast path");
 e.request(true);
 check(e.transitioning() && e.shouldProcessWet(),"first bypass block processes FX");
 auto last=e.next();check(last.wet>0.f && last.wet<1.f,"no immediate dry jump");
 check(std::abs(last.wet+last.dry-1.f)<0.00001f,"unity sum");
 for(int i=1;i<500;++i){
  auto now=e.next();
  check(std::abs(now.wet-last.wet)<0.004f,"bounded sample ramp");
  check(std::abs(now.wet+now.dry-1.f)<0.00001f,"crossfade unity sum");
  last=now;
 }
 check(e.fullyBypassed(),"DSP skipped when stable bypass");
 e.request(false);
 check(e.transitioning() && e.shouldProcessWet(),"reactivate processes wet during ramp");
 last=e.next();check(last.wet>0.f && last.wet<1.f,"reactivate not jump to wet");
 for(int i=0;i<500;++i)last=e.next();
 check(!e.transitioning() && e.shouldProcessWet(),"fully active after fade");
 // Discontinuous rapid user commands must be bounded; do not reset to
 // silence or to an endpoint abruptly.
 e.request(true);for(int i=0;i<50;++i)last=e.next();
 const auto atToggle=last;
 e.request(false);last=e.next();
 check(std::abs(last.wet-atToggle.wet)<0.004f,"rapid reverse continuous");
 // With plugin latency, never mix time-misaligned wet and dry; switch at zero.
 FxBypassEnvelope delayed;delayed.prepare(false,48000,512);
 delayed.request(true);
 bool sawDry=false,sawZero=false;
 for(int i=0;i<1000;++i){
  const auto x=delayed.next();
  check(x.wet==0.f||x.dry==0.f,"latency path never sums misaligned signals");
  if(x.wet==0.f&&x.dry==0.f)sawZero=true;
  if(x.dry>0.f)sawDry=true;
 }
 check(sawZero && sawDry && delayed.fullyBypassed(),"latency switch at silence");
 delayed.request(false);
 for(int i=0;i<1000;++i)last=delayed.next();
 check(!delayed.transitioning() && delayed.shouldProcessWet(),"delayed reactivation");
 return failures?1:0;
}
