// SPDX-License-Identifier: AGPL-3.0-only
// DMX3 real Reverb-Sis DSP: immutable wet-only prepare, not host admission.
#include <arverb/Reverb.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void check(bool ok,const char* what){
    if(!ok){std::fprintf(stderr,"DMX WET FAIL: %s\n",what);std::abort();}
}
static void test(double rate,arverb::Algorithm algorithm,unsigned profile){
    arverb::Engine wet, reference;
    for(auto* e:{&wet,&reference}){
        e->setAlgorithm(algorithm);e->setVoicing(profile);
        e->setParameter(arverb::Parameter::PreDelayMs,30.f);
    }
    wet.setParameter(arverb::Parameter::Mix,.19f);
    reference.setParameter(arverb::Parameter::Mix,1.f);
    wet.prepareWetReturn(rate,true);
    reference.prepare(rate,true);
    std::array<float,128> l{},r{},a{},b{},dryL{},dryR{};
    double energy=0;
    for(int block=0;block<400;++block){
        const int n=(block%2)?128:31;
        // Mix changes must be entirely inaudible in the wet-only return.
        if(block%17==0)wet.setParameter(arverb::Parameter::Mix,
            block%34==0?0.f:.37f);
        if(block==200){wet.reset();reference.reset();}
        for(int i=0;i<n;++i){
            const float phase=float(block*128+i);
            l[i]=block<100?.2f*std::sin(phase*.03f):0.f;
            r[i]=block<100?.1f*std::cos(phase*.07f):0.f;
        }
        if(block==0){l[0]=.3f;r[0]=-.17f;}
        dryL=l;dryR=r;
        reference.process(l.data(),r.data(),a.data(),b.data(),n);
        wet.process(l.data(),r.data(),l.data(),r.data(),n); // real in-place path
        for(int i=0;i<n;++i){
            check(std::isfinite(l[i])&&std::isfinite(r[i]),"finite output");
            check(l[i]==a[i]&&r[i]==b[i],"no dry leak or Mix automation change");
            if(block==0&&i==0)check(l[i]==0.f&&r[i]==0.f,"first sample pure wet");
            if(block>=100&&block<200)energy+=double(l[i])*l[i]+double(r[i])*r[i];
        }
        if(block==0)check(dryL[0]==.3f&&dryR[0]==-.17f,"nonzero excitation");
    }
    check(energy>1e-9,"real tail, not muted output");
    // Returning to old insert mode is an explicit stopped prepare, not a
    // live state mutation. Saved Mix remains user-owned.
    check(wet.getParameter(arverb::Parameter::Mix)<1.f,"Mix state untouched");
    wet.setParameter(arverb::Parameter::Mix,0.f);
    wet.prepare(rate,true);
    l.fill(.2f);r.fill(-.1f);
    wet.process(l.data(),r.data(),l.data(),r.data(),128);
    check(std::abs(l[0]-.2f)<1e-6f&&std::abs(r[0]+.1f)<1e-6f,"legacy insert restored");
}
int main(){
    for(double rate:{8000.,44100.,48000.,96000.,192000.})
        for(unsigned a=0;a<arverb::algorithmCount;++a)
            for(unsigned profile:{0u,5u})
                test(rate,static_cast<arverb::Algorithm>(a),profile);
    std::puts("DMX_REVERB_WET_ONLY_PASS 40 real DSP cases; exact calibrated wet endpoint, Mix automation immunity, reset, legacy insert restore");
}
