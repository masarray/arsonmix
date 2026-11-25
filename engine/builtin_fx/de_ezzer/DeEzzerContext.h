// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace armix::builtin_fx::de_ezzer::detail {
// Per-channel bounded context, evaluated on the persistent control clock.
// Evidence heuristic, not a phoneme/voice classifier.
struct Context {
    double bodyMemory=0, reference=.02;
    // Count on the persistent detector clock, never on host block boundaries.
    // Only a genuinely quiet interval may discard a previous phrase's context.
    int quietTicks=0, quietResetTicks=3000;
    void prepare(int ticks) noexcept {quietResetTicks=std::max(1,ticks);reset();}
    void reset() noexcept {bodyMemory=0;reference=.02;quietTicks=0;}
    double excess(double body,double hf,double lowBody,double thresholdRatio,
                  double memoryDecay,double adapt) noexcept {
        bodyMemory=std::max(lowBody,bodyMemory*memoryDecay);
        if(bodyMemory<1e-25)bodyMemory=0;
        if(hf<=1e-10){
            if(body<=1e-10&&lowBody<=1e-10){
                if(quietTicks<quietResetTicks&&++quietTicks==quietResetTicks){
                    bodyMemory=0;reference=.02;
                }
            }else quietTicks=0;
            return 0;
        }
        quietTicks=0;
        // A narrow low-mid witness rejects isolated HF sources; decaying memory
        // retains authority into unvoiced consonants after a vowel.
        const double authority=std::clamp((bodyMemory/(hf+1e-10)-.012)/.028,0.,1.);
        if(authority==0)return 0;
        const double ratio=hf/(body+1e-10);
        // Only moderate, currently body-backed observations may raise reference.
        // Severe sibilance and unvoiced intervals cannot train it away.
        if(ratio<=1.3 && lowBody>=.08*hf && body>1e-10)
            reference+=adapt*(std::clamp(ratio,.02,1.)-reference);
        const double effective=std::clamp(reference*1.58489319246111,thresholdRatio,thresholdRatio*3.16227766016838);
        return ratio>effective?authority*10*std::log10(ratio/effective):0.;
    }
};
} // namespace armix::builtin_fx::de_ezzer::detail
