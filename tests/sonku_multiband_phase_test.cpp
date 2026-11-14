// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuStereoIntegrity.h"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <limits>

int main() {
    constexpr float pi=3.14159265358979323846f;
    for(double rate:{44100.,48000.}) {
        using armix::sonku::SonKuStereoIntegrity;
        SonKuStereoIntegrity probe;
        assert(probe.prepare(rate));
        // The P0 full-band detector misses both mixtures: total L/R
        // correlation is positive while a local stereo-side band is lost.
        for(bool invertBass:{false,true}){
            probe.reset();
            double power=0.;
            for(int i=0;i<30000;++i) {
                const float lo=(invertBass?.10f:.29f)*
                    std::sin(2.f*pi*170.f*i/static_cast<float>(rate));
                const float hi=(invertBass?.34f:.10f)*
                    std::sin(2.f*pi*3200.f*i/static_cast<float>(rate));
                const float l=lo+hi;
                const float r=invertBass?(-lo+hi):(lo-hi);
                const float rescue=probe.preservedSide(l,r);
                assert(std::isfinite(rescue));
                if(i>=22000)power+=rescue*rescue;
            }
            assert(probe.recoveryAmount()<.02f);
            assert(std::sqrt(power/8000.)>.035);
        }
        // Positive coherent mono must remain bit-exact zero from the side
        // reconstruction (no new transient phantom or center coloration).
        probe.reset();
        for(int i=0;i<22000;++i) {
            const float v=.20f*std::sin(2.f*pi*440.f*i/static_cast<float>(rate));
            assert(probe.preservedSide(v,v)==0.f);
        }
        assert(probe.recoveryAmount()==0.f);
        assert(probe.coherentAmount()>.95f);
        probe.reset();
        for(int i=0;i<22000;++i) {
            const float v=.20f*std::sin(2.f*pi*440.f*i/static_cast<float>(rate));
            (void)probe.preservedSide(v,-v);
        }
        assert(probe.recoveryAmount()>.95f);
        for(int i=0;i<25000;++i)assert(probe.preservedSide(0.f,0.f)==0.f);
        assert(probe.recoveryAmount()<.05f);
        assert(probe.prepare(rate));
        assert(probe.recoveryAmount()==0.f);
        assert(probe.coherentAmount()==0.f);
        assert(std::isfinite(probe.preservedSide(
            std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN())));
    }
    std::puts("PASS SonKu P1.1 mixed-polarity 3-band guard 44.1/48k");
}
