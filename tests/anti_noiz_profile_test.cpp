// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizProfile.h"
#include <cstdlib>
#include <iostream>
using Profile=armix::builtin_fx::anti_noiz::NoiseProfile<129>;
void check(bool value,const char* reason){if(!value){std::cerr<<reason<<'\n';std::exit(1);}}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}){
        const int hop=rate>48000?256:128;
        Profile p;p.prepare(rate,hop,2,129);Profile::Powers noise{};
        noise[0].fill(.1f);noise[1].fill(.0001f);
        check(p.begin(),"start learn");
        auto old=noise;old[1].fill(100.f);
        for(int i=0;i<2;++i)check(p.observe(old)==Profile::LearnResult::waiting,"discard pre-command overlap");
        const int frames=int(std::ceil(2.*rate/hop));
        for(int i=0;i<frames;++i)check(p.observe(noise)==(i==frames-1?Profile::LearnResult::accepted:Profile::LearnResult::waiting),"learn duration");
        check(p.valid()&&!p.learning(),"trusted profile");const auto anchor=p.anchor();
        check(p.begin(),"relearn");for(int i=0;i<12;++i)p.observe(noise);
        auto polluted=noise;polluted[1].fill(.001f);
        // Total stereo energy rises <1%, but the quiet channel rises 10x.
        check(p.observe(polluted)==Profile::LearnResult::rejected,"quiet channel contamination");
        check(p.valid()&&p.anchor()==anchor&&p.powers()==anchor,"transaction rollback");
        auto drift=noise;for(auto& c:drift)for(float& v:c)v*=1.2f;
        const int hold=int(std::ceil(.5*rate/hop));
        for(int i=0;i<hold;++i)p.track(drift,true);
        check(p.powers()==anchor,"idle hold before tracking");
        for(int i=0;i<int(8*rate/hop);++i)p.track(drift,true);
        const float ratio=p.powers()[1][30]/anchor[1][30];
        check(ratio>1.17f&&ratio<1.21f,"bounded moderate drift tracking");
        const auto tracked=p.powers();for(int i=0;i<hold*2;++i)p.track(polluted,false);
        check(p.powers()==tracked&&p.anchor()==anchor,"foreground freeze");
        for(int i=0;i<int(20*rate/hop);++i)p.track(polluted,true);
        check(p.powers()[1][30]<=1.3f*anchor[1][30],"upper anchor bound");
        Profile::Powers zero{};for(int i=0;i<int(20*rate/hop);++i)p.track(zero,true);
        check(p.powers()[1][30]>=.7f*anchor[1][30],"lower anchor bound");
        check(p.begin(),"cancel transaction");p.cancel();check(p.valid()&&p.anchor()==anchor,"cancel preserves anchor");
        // Fresh learn, then independent coherent-drift path (not an idle label).
        check(p.begin(),"fresh learn");
        for(int i=0;i<frames+2;++i)p.observe(noise);
        const auto fresh=p.anchor();
        auto twice=noise;for(auto& c:twice)for(float& v:c)v*=2.f;
        const int window=int(std::ceil(.25*rate/hop));
        for(int i=0;i<4*window-1;++i)p.track(twice,false,true,2.f);
        check(p.powers()==fresh,"no drift before four coherent windows");
        for(int i=0;i<24*window;++i)p.track(twice,false,true,2.f);
        const float raised=p.powers()[1][30]/fresh[1][30];
        check(raised>1.85f&&raised<=2.2f&&p.anchor()==fresh,"coherent drift convergence and immutable anchor");
        const auto extended=p.powers();
        // Equal broadband power, but a narrow peak in the quiet channel.
        auto tonal=twice;tonal[1].fill(.0001f);tonal[1][30]=.013f;
        // A single abrupt peak on the last frame must invalidate a qualified
        // window before commit, not just fail the next averaged shape check.
        p.track(tonal,false,true,2.f,17.125f);
        check(p.powers()==extended,"last-frame localized foreground veto");
        // Independently exercise the averaged-shape rejection even when the
        // instantaneous guard has no peak evidence.
        for(int i=0;i<8*window;++i)p.track(tonal,false,true,2.f,2.f);
        check(p.powers()==extended,"reject localized quiet-channel foreground");
        for(int i=0;i<8*window;++i)p.track(twice,false,true,6.f);
        check(p.powers()==extended,"strong foreground veto");
        for(int i=0;i<8*window;++i)p.track(twice,false);
        check(p.powers()==extended,"policy freeze");
        // No accumulation across interrupted qualification windows.
        for(int i=0;i<3*window;++i)p.track(twice,false,true,2.f);
        p.track(twice,false,false);
        for(int i=0;i<3*window;++i)p.track(twice,false,true,2.f);
        check(p.powers()==extended,"interrupted drift candidate must restart");
        for(int i=0;i<hold;++i)p.track(noise,true);
        p.track(noise,true);
        check(p.powers()[1][30]>1.8f*fresh[1][30],"return from drift is gradual");
        for(int i=0;i<int(12*rate/hop);++i)p.track(noise,true);
        check(p.powers()[1][30]<1.02f*fresh[1][30],"return to original noise");
        std::cout<<rate<<" Hz: coherent upward PSD ratio "<<raised<<", narrow/strong foreground freeze, gradual recovery PASS\n";
        // A spectrally tilted, broad noise change is not a global level change.
        check(p.begin(),"spectral learn");for(int i=0;i<frames+2;++i)p.observe(noise);
        const auto spectralAnchor=p.anchor();
        auto tilted=noise;
        for(int k=0;k<129;++k)tilted[1][k]*=.8f+1.8f*float(k)/128.f;
        for(int i=0;i<4*window-1;++i)p.track(tilted,false,true,1.7f,2.6f);
        check(p.powers()==spectralAnchor,"spectral qualification hold");
        for(int i=0;i<24*window+1;++i)p.track(tilted,false,true,1.7f,2.6f);
        const float low=p.powers()[1][4]/spectralAnchor[1][4];
        const float high=p.powers()[1][120]/spectralAnchor[1][120];
        check(low<.98f&&low>.7f&&high>2.05f&&high<=2.2f,"selective broad spectral convergence");
        check(p.powers()[0]==spectralAnchor[0]&&p.anchor()==spectralAnchor,"neutral channel and anchor unchanged");
        const auto spectral=p.powers();
        // A narrow excess and alternating opposite spectral shapes cannot
        // qualify; both channels still veto as a transaction.
        p.track(tonal,false,true,2.f,17.125f);
        for(int i=0;i<8*window;++i)p.track(tonal,false,true,2.f,2.f);
        check(p.powers()==spectral,"spectral contamination rejection");
        auto reverse=tilted;
        for(int k=0;k<129;++k)reverse[1][k]=tilted[1][128-k];
        p.track(tilted,false,false);
        for(int n=0;n<8;++n)for(int i=0;i<window;++i)
            p.track(n%2?tilted:reverse,false,true,1.7f,2.6f);
        check(p.powers()==spectral,"intermittent unstable shape freeze");
        p.limitToIdleBounds();
        check(p.anchor()==spectralAnchor&&p.valid(),"policy transition preserves trusted profile");
        for(int c=0;c<2;++c)for(int k=0;k<129;++k)
            check(p.powers()[c][k]>=.7f*spectralAnchor[c][k]&&p.powers()[c][k]<=1.3f*spectralAnchor[c][k],"policy transition removes extended authority");
        check(p.powers()[1][4]==spectral[1][4]&&p.powers()[0]==spectral[0],"unaffected PSD unchanged");
        std::cout<<rate<<" Hz: spectral low/high ratios "<<low<<"/"<<high<<", stereo contamination and intermittent freeze, policy isolation PASS\n";
        p.prepare(rate,hop,1,129);check(!p.valid()&&!p.learning(),"new format invalidates profile");
        // Colored noise may vary strongly per FFT frame, even though
        // its 50ms energy remains stable. This is an AC-like synthetic
        // stress, not the real-recording evidence.
        const int learnWindow=int(std::ceil(.05*rate/hop));
        Profile::Powers base{};base[0].fill(.1f);
        check(p.begin(),"window learn");
        for(int i=0;i<2;++i)p.observe(base);
        for(int i=0;i<frames;++i){
            auto v=base;
            const float scale=(i%learnWindow==0)?12.f:(i%learnWindow<8?.02f:1.f);
            for(float& x:v[0])x*=scale;
            const auto result=p.observe(v);
            check(result==(i==frames-1?Profile::LearnResult::accepted:Profile::LearnResult::waiting),"short-frame variation");
        }
        check(p.valid()&&p.lastAttemptQualified(),"colored profile accepted");
        check(p.variability()[0]>.8f,"learned frame variability");
        // A complete contaminated window may be skipped, but its spectral
        // contribution must be absent from every bin and count.
        check(p.begin(),"window discard");
        for(int i=0;i<2;++i)p.observe(base);
        for(int i=0;i<frames;++i){
            auto v=base;
            if(i>=4*learnWindow&&i<5*learnWindow)
                for(float& x:v[0])x*=10.f;
            const auto result=p.observe(v);
            check(result==(i==frames-1?Profile::LearnResult::accepted:Profile::LearnResult::waiting),"isolated window discard");
        }
        check(p.lastAttemptQualified()&&p.valid(),"coverage-qualified learn");
        for(float x:p.anchor()[0])check(std::abs(x-.1f)<2e-6f,"contaminated window excluded");
        check(p.variability()[0]<.01f,"discarded window excluded from variability");
        const auto safe=p.anchor();const auto safeVariability=p.variability();
        check(p.begin(),"persistent contamination");
        for(int i=0;i<2;++i)p.observe(base);
        for(int i=0;i<frames&&p.learning();++i){
            auto v=base;
            if(i>=4*learnWindow)for(float& x:v[0])x*=10.f;
            p.observe(v);
        }
        check(!p.learning()&&!p.lastAttemptQualified()&&p.anchor()==safe&&p.variability()==safeVariability,"coverage rejection preserves trusted statistics");
        check(p.begin(),"extreme sustained contamination");
        for(int i=0;i<2;++i)p.observe(base);
        for(int i=0;i<learnWindow;++i)p.observe(base);
        auto extreme=base;for(float& x:extreme[0])x*=64.f;
        for(int i=0;i<learnWindow&&p.learning();++i)p.observe(extreme);
        check(!p.learning()&&!p.lastAttemptQualified()&&p.anchor()==safe,"extreme window veto");
        std::cout<<rate<<" Hz: window Learn acceptance, exclusion, coverage and catastrophic veto PASS\n";
        std::cout<<rate<<" Hz: drift ratio "<<ratio<<", rollback, freeze and bounds PASS\n";
    }
}
