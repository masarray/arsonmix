// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cmath>

// P8.7 private music-first psychoacoustic audition. One shared real-time
// callback, no extra FIR, FFT, dry bypass, invented room modes or clipping.
// Air is a modest common minimum-phase lead shelf of the measured DIRECT
// HRTF; identical topology for both ears preserves interaural gain/time
// cues better than independent peak/notch boosting. NEVER invert KEMAR notches.
// Low warmth comes exclusively from actual already-rendered 8-line FDN wet
// and the original per-channel music LF, not an oscillator or uncorrelated bass.
// OFF unless explicitly selected by renderMode==6 and ROOM enabled.
namespace armix::sonku::p87 {
class MusicalAirBody final {
public:
 struct Ears {float left=0.f,right=0.f;};
 bool prepare(double rate) noexcept {
  if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                             std::abs(rate-48000.)>.1))return false;
  constexpr double twoPi=6.28318530717958647692;
  // High shelf +1.9 dB asymptote above the ~4 kHz transition, no sharp Q peak.
  // Adapted from one-pole highpass complement; stable zero/pole within circle.
  airAlpha_=static_cast<float>(1.-std::exp(-twoPi*3900./rate));
  airGain_=static_cast<float>(std::pow(10.,1.9/20.)-1.);
  // A damped, broadband low-mid tail under 180 Hz — no single resonant
  // frequency, no feedback gain changes, no low-Q ringing or peak generation.
  roomAlpha_=static_cast<float>(1.-std::exp(-twoPi*175./rate));
  reset();return true;
 }
 void reset() noexcept {airLow_={};roomLow_={};}
 void enhanceDirect(float& l,float& r) noexcept {
  // Stereo equal coefficients keep both measured HRTF channels independent.
  const std::array<float,2> x{l,r};
  airLow_[0]+=airAlpha_*(x[0]-airLow_[0]);
  airLow_[1]+=airAlpha_*(x[1]-airLow_[1]);
  l=x[0]+airGain_*(x[0]-airLow_[0]);
  r=x[1]+airGain_*(x[1]-airLow_[1]);
 }
 Ears diffuseLowBody(float l,float r) noexcept {
  roomLow_[0]+=roomAlpha_*(l-roomLow_[0]);
  roomLow_[1]+=roomAlpha_*(r-roomLow_[1]);
  // Add ONLY 42% of the diffuse LF signal. Existing FDN RT60 remains intact
  // and four horizontal early reflection image paths are untouched.
  return {0.42f*roomLow_[0],0.42f*roomLow_[1]};
 }
 static constexpr float musicBassRoomGain=1.09f; // room ON, LF only
 float preparedAirBoost()const noexcept{return airGain_;}
private:
 std::array<float,2> airLow_{},roomLow_{};
 float airAlpha_=0.f,roomAlpha_=0.f,airGain_=0.f;
};
} // namespace armix::sonku::p87
