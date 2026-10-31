// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/CaptureBoundaryEvidence.h"
#include "../engine/core/CaptureClickRepair.h"
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>
using P=armix::rt::CaptureBoundaryEvidence;
static void check(bool value){if(!value)std::abort();}
int main() {
 P p;
 std::array<float,480> l{},r{};
 const float* in[2]{l.data(),r.data()};
 l.fill(.125f);r.fill(-.125f);
 check(p.observe(in,2,480)==.125f);
 check(p.candidateBlocks()==0);
 // JUCE-like whole-channel zero padding of the first 64 frames.
 for(int i=0;i<64;++i)l[i]=r[i]=0;
 check(p.observe(in,2,480)==.125f);
 check(p.candidateBlocks()==1&&p.candidateFrames()==64);
 // Ordinary samples, no zero head; a loud transient must not count.
 l.fill(.9f);r.fill(-.8f);
 check(p.observe(in,2,480)==.9f);
 check(p.candidateBlocks()==1);
 // Partial-channel silence is not a simultaneous missing duplex frame.
 l.fill(0.f);r.fill(.2f);
 p.observe(in,2,480);
 check(p.candidateBlocks()==1);
 // Short exact-zero leading span should not report a missing block.
 l.fill(.2f);r.fill(.2f);p.observe(in,2,480);
 for(int i=0;i<31;++i)l[i]=r[i]=0;
 p.observe(in,2,480);
 check(p.candidateBlocks()==1);
 // Fully silent block after a signalled tail remains ONLY a candidate.
 l.fill(0);r.fill(0);p.observe(in,2,480);
 check(p.candidateBlocks()==2&&p.candidateFrames()==544);
 p.observe(in,2,480);
 check(p.candidateBlocks()==2); // no double-count for continuing silence
 // Invalid nonfinite source must be counted, never poison peak.
 l.fill(.25f);r.fill(.25f); l[20]=std::numeric_limits<float>::quiet_NaN();
 p.observe(in,2,480);
 check(p.nonFiniteSamples()==1);
 // Changing native callback generation resets all continuity history.
 p.reset();
 check(p.candidateBlocks()==0&&p.candidateFrames()==0&&p.nonFiniteSamples()==0);
 // Uniform finite stereo never generates a false candidate even in long soak.
 l.fill(.25f);r.fill(.25f);
 for(int i=0;i<100000;++i)check(p.observe(in,2,480)==.25f);
 check(p.candidateBlocks()==0);
 // Empty input invalidates callback continuity context.
 p.observe(nullptr,0,480);
 l.fill(0);r.fill(0);p.observe(in,2,480);
 check(p.candidateBlocks()==0);
 check(std::strstr(P::provenance(),"NOT_DRIVER_XRUN")!=nullptr);
 // One 480-frame buffer's candidate at most; no new FIFO/latency.
 P evidence;
 armix::rt::CaptureClickRepair repair;
 std::array<float,480> outL{},outR{};
 repair.reset();evidence.reset();
 l.fill(.125f);r.fill(-.125f);
 auto beforeBlock=evidence.candidateBlocks(),beforeFrames=evidence.candidateFrames();
 evidence.observe(in,2,480);
 check(!repair.process(in,2,480,0,false,outL.data(),outR.data()));
 check(repair.repairedBlocks()==0);
 for(int i=0;i<64;++i)l[i]=r[i]=0.f;
 beforeBlock=evidence.candidateBlocks();beforeFrames=evidence.candidateFrames();
 evidence.observe(in,2,480);
 check(evidence.candidateBlocks()==beforeBlock+1);
 const int head=static_cast<int>(evidence.candidateFrames()-beforeFrames);
 check(head==64);
 check(repair.process(in,2,480,head,false,outL.data(),outR.data()));
 check(outL[0]>.12f&&outL[63]==0.f&&outL[64]>0.f&&outL[64]<.01f);
 check(outR[0]<-.12f&&outR[63]==0.f&&r[0]==0.f); // immutable source
 check(repair.repairedBlocks()==1&&repair.repairedFrames()==128);
 // Full-block silence after a signal is NOT concealed as fake music.
 l.fill(.125f);r.fill(-.125f);
 evidence.observe(in,2,480);
 repair.process(in,2,480,0,false,outL.data(),outR.data());
 l.fill(0.f);r.fill(0.f);
 evidence.observe(in,2,480);
 check(!repair.process(in,2,480,480,false,outL.data(),outR.data()));
 // Single-channel zero and honest music produce no repair.
 l.fill(.125f);r.fill(-.125f);
 repair.process(in,2,480,0,false,outL.data(),outR.data());
 for(int i=0;i<64;++i)l[i]=0.f;
 check(!repair.process(in,2,480,0,false,outL.data(),outR.data()));
 // Non-finite PCM only enters prepared scratch on exceptional blocks.
 l.fill(.25f);r.fill(-.25f);
 l[21]=std::numeric_limits<float>::quiet_NaN();
 check(repair.process(in,2,480,0,true,outL.data(),outR.data()));
 check(outL[21]==0.f&&std::isfinite(outL[21])&&repair.sanitizedSamples()==1);
 repair.reset();evidence.reset();
 check(repair.repairedBlocks()==0&&repair.sanitizedSamples()==0);
 l.fill(.125f);r.fill(-.125f);
 for(int i=0;i<100000;++i){
   evidence.observe(in,2,480);
   check(!repair.process(in,2,480,0,false,outL.data(),outR.data()));
 }
 check(repair.repairedBlocks()==0&&repair.sanitizedSamples()==0);
 check(std::strstr(armix::rt::CaptureClickRepair::provenance(),"NOT_CLOCK_DRIFT")!=nullptr);
}
