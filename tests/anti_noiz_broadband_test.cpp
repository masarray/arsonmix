// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizBroadband.h"
#include <cstdlib>
#include <iostream>
using armix::builtin_fx::anti_noiz::BroadbandNoise;
static void require(bool b,const char* message){if(!b){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}){
        const int fft=rate>48000.?512:256,bins=fft/2+1;
        const int first=int(std::ceil(3000.*fft/rate)),last=int(std::ceil(12000.*fft/rate));
        BroadbandNoise<257> g;g.prepare(rate,fft/2,fft,2);
        BroadbandNoise<257>::Powers anchor{},p{};
        for(int c=0;c<2;++c)for(int k=0;k<bins;++k)anchor[c][k]=k>=first&&k<last?1.f:.001f;
        g.configure(anchor);require(g.eligible(),"learned broad band eligible");
        p=anchor;for(auto& channel:p)for(float& v:channel)v*=2.f;
        for(int i=0;i<100;++i)g.update(p,anchor,true,true);
        const int k=first+8;
        require(g.cap(k,p,.063f)<.35f,"scaled noise capped");
        require(g.residualIdle(),"qualified steady scaled noise closes residual");
        // Residual authority uses independent stereo foreground, not just
        // the dominant profile band. A brief uncertain frame is not an onset.
        p[0][1]=.045f;g.update(p,anchor,true,true);
        require(g.residualIdle(),"isolated uncertain frame preserves hold");
        p[0][1]=2.f;g.update(p,anchor,true,true);
        require(!g.residualIdle(),"low foreground opens immediately");
        p=anchor;for(auto& channel:p)for(float& v:channel)v*=2.f;
        const int hold=int(std::ceil(.075*rate/(fft/2)));
        for(int i=0;i<hold-1;++i)g.update(p,anchor,true,true);
        require(!g.residualIdle(),"foreground requires new hold");
        for(int i=0;i<100;++i)g.update(p,anchor,true,true);
        require(g.residualIdle(),"rearm after fresh evidence");
        p[1][first+8]=200.f;g.update(p,anchor,true,true);
        require(!g.residualIdle(),"high-band transient opens immediately");
        p=anchor;for(auto& channel:p)for(float& v:channel)v*=2.f;
        for(int i=0;i<100;++i)g.update(p,anchor,true,false);
        require(!g.residualIdle(),"Silence zero disables residual authority");
        require(g.cap(k,p,.063f)<.35f,"Silence zero preserves spectral estimator");
        require(g.cap(1,p,.063f)==1.f&&g.cap(last,p,.063f)==1.f,"low and high out of scope");
        require(g.cap(k,p,1.f)==1.f,"zero Clean unity");
        p[1][k]=100.f;require(g.cap(k,p,.063f)==1.f,"independent stereo onset immediately opens");
        g.update(p,anchor,false,true);require(g.cap(k,p,.063f)==1.f&&!g.residualIdle(),"disabled resets");
        p=anchor;for(auto& channel:p)for(float& v:channel)v*=.01f;
        for(int i=0;i<100;++i)g.update(p,anchor,true,true);
        require(g.cap(k,p,.063f)==1.f,"missing noise evidence returns unity");
        // Same input/profile, different Silence controls: residual evidence
        // must not change spectral gain. Verify both histories after fresh Learn.
        BroadbandNoise<257> spectral,residual;
        spectral.prepare(rate,fft/2,fft,2);residual.prepare(rate,fft/2,fft,2);
        spectral.configure(anchor);residual.configure(anchor);
        for(int i=0;i<200;++i){
            p=anchor;
            for(int c=0;c<2;++c)for(int j=first;j<last;++j)
                p[c][j]*=1.f+.125f*float((i+j*3+c)%17);
            if(i%31==30)p[1][k]=100.f;
            spectral.update(p,anchor,true,false);residual.update(p,anchor,true,true);
            for(int j=0;j<bins;++j)require(std::abs(spectral.cap(j,p,.063f)-residual.cap(j,p,.063f))<1e-6f,"Silence must not change spectral cap");
        }
        require(spectral.restoreOutsideNoise(first,.25f,32.f)==.25f,"no restore inside noise band");
        require(spectral.restoreOutsideNoise(1,.25f,2.f)==.25f,"no restore without excess");
        require(spectral.restoreOutsideNoise(1,.25f,32.f)>.90f,"restore strong independent foreground");
        require(spectral.restoreOutsideNoise(1,1.f,32.f)==1.f,"no amplification");
        spectral.reset();require(spectral.cap(k,p,.063f)==1.f,"reset clears slow confidence");
        for(auto& c:anchor)c.fill(1.f);
        g.configure(anchor);require(!g.eligible(),"flat profile excluded");
        for(int i=0;i<100;++i)g.update(p,anchor,true,true);
        require(!g.residualIdle()&&g.cap(k,p,.063f)==1.f,"ineligible profile cannot grant authority");
    }
    std::cout<<"Broadband profile, rate, stereo, onset, lifecycle and scope PASS\n";
}
