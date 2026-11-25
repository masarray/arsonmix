// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    for(unsigned rate:{44100u,48000u,88200u,96000u}){
        TonalNoise t;t.prepare(rate,2);
        const auto tone=[&](unsigned i,float f){return float(.05*std::sin(2.*std::numbers::pi*f*i/rate));};
        for(unsigned i=0;i<2*rate;++i)t.observe(tone(i,60),tone(i,60));
        t.commit();offline::require(t.frequency()==60.f,"stable line qualification");
        double before=0.,after=0.,otherBefore=0.,otherAfter=0.;
        for(unsigned i=0;i<2*rate;++i){
            float l=tone(i,60),r=tone(i,230);const float a=l,b=r;
            t.apply(l,r,1.f);
            if(i>=rate){before+=a*a;after+=l*l;otherBefore+=b*b;otherAfter+=r*r;}
        }
        const double cut=10*std::log10(before/after),off=10*std::log10(otherBefore/otherAfter);
        offline::require(cut>2.8&&cut<3.2&&std::abs(off)<.05,"bounded narrow line attenuation");
        // Strong same-line foreground on only one channel must release both.
        t.resetSignal();double a0=0.,a1=0.,b0=0.,b1=0.;
        for(unsigned i=0;i<2*rate;++i){
            float l=4.f*tone(i,60),r=tone(i,60);const float a=l,b=r;
            t.apply(l,r,1.f);
            if(i>=rate){a0+=a*a;a1+=l*l;b0+=b*b;b1+=r*r;}
        }
        const double linkedCut=10*std::log10(a0/a1),quietCut=10*std::log10(b0/b1);
        offline::require(std::abs(linkedCut)<.05&&std::abs(quietCut)<.05,"coincident strong line linked release");
        // Recovery after a loud line must not permanently disable suppression.
        double recoveredBefore=0.,recoveredAfter=0.;
        for(unsigned i=0;i<2*rate;++i){
            float l=tone(i,60),r=l;const float a=l;t.apply(l,r,1.f);
            if(i>=rate){recoveredBefore+=a*a;recoveredAfter+=l*l;}
        }
        offline::require(10*std::log10(recoveredBefore/recoveredAfter)>2.8,"guard recovery");
        t.begin();for(unsigned i=0;i<rate;++i)t.observe(tone(i,100),tone(i,100));
        offline::require(t.frequency()==60.f,"uncommitted Learn retains trusted line");
        t.begin();for(unsigned i=0;i<2*rate;++i)t.observe(tone(i,60),tone(i,230));
        t.commit();offline::require(t.selected()<0,"asymmetric stereo veto");
        std::mt19937 rng(42);std::uniform_real_distribution<float> white(-.05f,.05f);
        t.begin();for(unsigned i=0;i<2*rate;++i)t.observe(white(rng),white(rng));
        t.commit();offline::require(t.selected()<0,"broadband noise veto");
        t.begin();for(unsigned i=0;i<2*rate;++i){float v=i<rate?tone(i,60):tone(i,100);t.observe(v,v);}
        t.commit();offline::require(t.selected()<0,"unstable tone veto");
        for(float f:{55.f,73.f,137.5f}){
            t.begin();for(unsigned i=0;i<2*rate;++i)t.observe(tone(i,f),tone(i,f));
            t.commit();offline::require(std::abs(t.frequency()-f)<=1.25f,"off-grid learned line");
        }
        offline::Audio noise{rate,1,std::vector<float>(rate*3)},input=noise;
        for(unsigned i=0;i<3*rate;++i){noise.samples[i]=tone(i,60);input.samples[i]=tone(i,60)+tone(i,230);}
        Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=0;
        const auto a=offline::render(input,&noise,s,64),b=offline::render(input,&noise,s,127);
        offline::require(a.processed.samples==b.processed.samples,"tonal path partition exact");
        s.cleanDb=0;const auto dry=offline::render(input,&noise,s,127);
        double error=0.,ref=0.;for(unsigned i=0;i<3*rate;++i){const double x=input.samples[i],d=dry.processed.samples[i]-x;error+=d*d;ref+=x*x;}
        offline::require(error/ref<1e-10,"zero Clean remains transparent");
        std::cout<<rate<<" Hz tonal cut "<<cut<<" dB off-line "<<off<<" dB, shared-line release "<<linkedCut<<" dB, grid/stereo/broadband/unstable veto, partition and Clean0 PASS\n";
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
