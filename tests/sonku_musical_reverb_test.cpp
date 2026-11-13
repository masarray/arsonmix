// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuMusicalReverb.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <array>

int main(){
    using armix::sonku::SonKuMusicalReverb;
    using armix::sonku::SonKuReverbPreset;
    SonKuMusicalReverb invalid;
    assert(!invalid.prepare(96000.));
    assert(!invalid.prepare(std::numeric_limits<double>::quiet_NaN()));
    for(double rate:{44100.,48000.}){
        SonKuMusicalReverb fx,copy;
        assert(fx.prepare(rate)&&copy.prepare(rate));
        assert(!fx.active());
        assert(!fx.setAmount(-.1f));
        assert(!fx.setAmount(std::numeric_limits<float>::infinity()));
        assert(!fx.setPreset(static_cast<SonKuReverbPreset>(99)));
        assert(fx.setPreset(SonKuReverbPreset::StudioGlue));
        assert(copy.setPreset(SonKuReverbPreset::StudioGlue));
        assert(fx.setAmount(.40f)&&copy.setAmount(.40f));
        assert(!fx.setPreset(SonKuReverbPreset::AiryChamber));
        const int count=static_cast<int>(rate*2.0);
        double energy=0.,width=0.,early=0.,late=0.;
        for(int i=0;i<count;++i){
            const float impulse=i==0?.55f:0.f;
            const auto a=fx.process(impulse,impulse);
            const auto b=copy.process(impulse,impulse);
            assert(std::isfinite(a.left)&&std::isfinite(a.right));
            assert(std::abs(a.left-b.left)<1.e-7f);
            assert(std::abs(a.right-b.right)<1.e-7f);
            assert(std::abs(a.left)<.80f&&std::abs(a.right)<.80f);
            const double magnitude=std::abs(a.left)+std::abs(a.right);
            if(i<1800)early+=magnitude;
            if(i>2000)energy+=magnitude;
            if(i>2000&&i<rate*.35)width+=std::abs(a.left-a.right);
            if(i>rate*1.5)late+=magnitude;
        }
        assert(early==0.);                 // direct + early stays untouched
        assert(energy>.005 && width>.002); // diffuse stereo field, not mono
        assert(late<energy*.12);           // bounded, musically decaying tail
        assert(fx.setAmount(0.f));
        for(int i=0;i<11000;++i)fx.process(0.f,0.f);
        assert(!fx.active());
        const auto silent=fx.process(1.f,1.f);
        assert(silent.left==0.f&&silent.right==0.f);
        assert(fx.setPreset(SonKuReverbPreset::AiryChamber));
        assert(fx.setAmount(.30f));
        double airy=0.;
        for(int i=0;i<static_cast<int>(rate);++i){
            const auto output=fx.process(i==0?.4f:0.f,i==0?.4f:0.f);
            airy+=std::abs(output.left)+std::abs(output.right);
        }
        assert(airy>.005);
        // Prepared, deterministic room-aware presets; explicit old profiles
        // remain unchanged and invalid/unsafe dimensions always choose OFF.
        const auto bad=armix::sonku::musicalAmbienceForRoom(7.2f,7.2f,5.f);
        assert(bad.wet==0.f);
        const auto approved=armix::sonku::musicalAmbienceForRoom(7.2f,7.2f,1.6f);
        const auto spacious=armix::sonku::musicalAmbienceForRoom(14.4f,14.4f,5.f);
        const auto large=armix::sonku::musicalAmbienceForRoom(28.8f,28.8f,8.f);
        assert(approved.preset==SonKuReverbPreset::StudioGlue);
        assert(spacious.preset==SonKuReverbPreset::SpaciousChamber);
        assert(large.preset==SonKuReverbPreset::LargeAuditorium);
        assert(approved.wet>spacious.wet && spacious.wet>large.wet);
        assert(large.wet>=.26f && approved.wet<=.38f);
        SonKuMusicalReverb spaciousFx,largeFx;
        assert(spaciousFx.prepare(rate) && largeFx.prepare(rate));
        assert(spaciousFx.setPreset(spacious.preset));
        assert(largeFx.setPreset(large.preset));
        assert(spaciousFx.setAmount(spacious.wet));
        assert(largeFx.setAmount(large.wet));
        double largeLate=0.,spaciousLate=0.;
        for(int i=0;i<static_cast<int>(rate*2.8);++i) {
            const float sample=i==0?.5f:0.f;
            const auto x=spaciousFx.process(sample,sample);
            const auto y=largeFx.process(sample,sample);
            assert(std::isfinite(x.left)&&std::isfinite(x.right));
            assert(std::isfinite(y.left)&&std::isfinite(y.right));
            assert(std::abs(x.left)<1.f && std::abs(y.right)<1.f);
            if(i>rate*1.8) {
                spaciousLate+=std::abs(x.left)+std::abs(x.right);
                largeLate+=std::abs(y.left)+std::abs(y.right);
            }
        }
        assert(largeLate>spaciousLate*.4);
    }
    return 0;
}
