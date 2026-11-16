// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// Optional single-graph music-level *upward-only* compensation.
// In P9 spatial wet transforms the original stereo energy can be lower.
// A slow detector allows a conservative minimum electrical level without
// turning the entire effect into a compressor or defeating user gain.
// The final existing linked 0.98 sample peak safety limiter stays authoritative.
namespace armix::sonku::p95 {
class MusicalPresence final {
public:
 bool prepare(double sr) noexcept {
  if(!std::isfinite(sr) || (std::abs(sr-44100.)>.1 && std::abs(sr-48000.)>.1))
   return false;
  detector_=static_cast<float>(1.-std::exp(-1./(sr*.28)));
  slew_=static_cast<float>(1.-std::exp(-1./(sr*.45)));
  reset();return true;
 }
 void reset() noexcept {inPower_=outPower_=0.f;target_=makeup_=1.f;counter_=0;}
 float process(float inL,float inR,float wetL,float wetR) noexcept {
  const float a=.5f*(inL*inL+inR*inR);
  const float b=.5f*(wetL*wetL+wetR*wetR);
  inPower_+=detector_*(a-inPower_);
  outPower_+=detector_*(b-outPower_);
  // At most one square root per 256 samples, outside inner FIR loops.
  if(++counter_>=256){
   counter_=0;
   if(inPower_>1.e-7f&&outPower_>1.e-9f){
    const float ratio=std::sqrt(inPower_/outPower_);
    // 1.12× = +0.98dB *electrical* minimum when possible.
    // Never auto-reduce the deliberate user outputTrim boost.
    target_=std::clamp(1.12f*ratio,1.f,1.65f);
   }else target_=1.f;
  }
  makeup_+=slew_*(target_-makeup_);
  return makeup_;
 }
 float gain() const noexcept {return makeup_;}
private:
 float detector_=0.f,slew_=0.f,inPower_=0.f,outPower_=0.f;
 float target_=1.f,makeup_=1.f;
 int counter_=0;
};
} // namespace armix::sonku::p95
