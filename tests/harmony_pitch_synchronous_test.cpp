#include "../engine/builtin_fx/HarmonyPitchSynchronous.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using armix::harmony::PitchSynchronousVoice;
using Shifter=PitchSynchronousVoice<16384,6>;
static void need(bool yes,const char* reason){
    if(!yes){std::cerr<<reason<<"\n";std::exit(2);}
}
static std::vector<float> render(double rate,double f0,double ratio,int block,
                                 double amplitude=1.,double polarity=1.){
    auto p=Shifter{};
    std::array<float,16384> ring{};
    const int n=static_cast<int>(rate*.65);
    std::vector<float> out(static_cast<std::size_t>(n));
    for(int pos=0;pos<n;pos+=block)for(int j=0;j<std::min(block,n-pos);++j){
        const int t=pos+j;
        const double phase=6.283185307179586*f0*t/rate;
        ring[static_cast<std::size_t>(t)&16383]=static_cast<float>(
            amplitude*polarity*(.19*std::sin(phase)+
            .052*std::sin(2*phase+.31)+.029*std::sin(3*phase+.17)));
        const auto sample=p.process(0,t,static_cast<std::size_t>(t)&16383,ring,
            static_cast<float>(rate/f0),static_cast<float>(ratio),
            static_cast<float>(rate*.032));
        out[static_cast<std::size_t>(t)]=sample.sample;
        need(std::isfinite(sample.sample)&&std::abs(sample.sample)<2.f,
             "synchronous output nonfinite/unbounded");
    }
    return out;
}
static double measuredHz(const std::vector<float>& signal,double rate,double wanted){
    const int begin=static_cast<int>(rate*.27);
    const int length=static_cast<int>(rate*.23);
    const int low=static_cast<int>(rate/(wanted*1.05));
    const int high=static_cast<int>(rate/(wanted*.95));
    double best=-100.;int lag=0;
    for(int d=low;d<=high;++d){
        double numerator=0,energy=0;
        for(int t=begin;t<begin+length;t+=2){
            numerator+=signal[static_cast<std::size_t>(t)]*
                       signal[static_cast<std::size_t>(t-d)];
            energy+=signal[static_cast<std::size_t>(t)]*
                    signal[static_cast<std::size_t>(t)];
        }
        if(numerator/(energy+1.e-20)>best){
            best=numerator/(energy+1.e-20);lag=d;
        }
    }
    need(best>.6&&lag>0,"synchronous source lost periodicity");
    return rate/lag;
}
// A periodic damped-resonator pulse train with independent fundamental
// and spectral resonance. Measure harmonic-envelope centroid rather than
// claiming that a sine-wave pitch test measures formants.
static std::vector<float> resonanceFixture(double rate,float scale,int block){
    Shifter p{};
    std::array<float,16384> ring{};
    const int n=static_cast<int>(rate*.65);
    std::vector<float> out(n);
    const double f0=110.,period=rate/f0;
    for(int pos=0;pos<n;pos+=block)for(int j=0;j<std::min(block,n-pos);++j){
        const int at=pos+j;
        const double age=std::fmod(at,period)/rate;
        const double phase=6.283185307179586*f0*at/rate;
        ring[static_cast<std::size_t>(at)&16383]=static_cast<float>(
            .12*std::cos(phase)+.27*std::exp(-age*750.)*
            std::cos(6.283185307179586*2200.*age));
        const auto y=p.process(0,at,static_cast<std::size_t>(at)&16383,ring,
            static_cast<float>(period),1.5f,static_cast<float>(rate*.029),scale);
        out[at]=y.sample;
        need(std::isfinite(y.sample)&&std::abs(y.sample)<1.f,"H8 nonfinite resonance");
    }
    return out;
}
static double centroid(const std::vector<float>& x,double rate){
    double weighted=0.,sum=0.;
    for(int h=8;h<=19;++h){
        const double f=h*165.;
        double c=0.,s=0.;
        for(int i=static_cast<int>(rate*.3);i<static_cast<int>(rate*.6);++i){
            const double phase=6.283185307179586*f*i/rate;
            c+=x[i]*std::cos(phase);s+=x[i]*std::sin(phase);
        }
        const double e=c*c+s*s;weighted+=f*e;sum+=e;
    }
    need(sum>1.,"H8 resonance spectrum absent");return weighted/sum;
}
static void testResonanceScaling(){
    for(double rate:{32000.,48000.,96000.,192000.}){
        const auto old=resonanceFixture(rate,1.f,127);
        const auto low=resonanceFixture(rate,.92f,127);
        const auto high=resonanceFixture(rate,1.08f,127);
        need(low==resonanceFixture(rate,.92f,64),"H8 grain block parity");
        const auto a=centroid(low,rate),b=centroid(old,rate),c=centroid(high,rate);
        std::cout<<"H8_RESONANCE rate="<<rate<<" low="<<a<<" neutral="<<b
                 <<" high="<<c<<std::endl;
        need(a<b-40.&&c>b+40.,"H8 resonance failed to move independently");
        for(const auto* x:{&low,&old,&high})
            need(std::abs(measuredHz(*x,rate,165.)-165.)<3.96,
                 "H8 resonance changed fundamental");
    }
}
static void testResumeAfterGap(){
    for(int rate:{48000,96000}){
        Shifter retained{},fresh{};
        std::array<float,16384> ring{};
        const int start=rate/10,resume=start+rate/40,end=resume+rate/10;
        for(int i=0;i<end;++i){
            ring[static_cast<std::size_t>(i)&16383]=static_cast<float>(
                .18*std::sin(6.283185307179586*440.*i/rate));
            if(i>=start&&i<resume)continue;
            const auto x=retained.process(0,i,static_cast<std::size_t>(i)&16383,
                ring,static_cast<float>(rate/440.),1.6f,rate*.036f);
            if(i>=resume){
                const auto y=fresh.process(0,i,static_cast<std::size_t>(i)&16383,
                    ring,static_cast<float>(rate/440.),1.6f,rate*.036f);
                need(x.ready==y.ready&&x.sample==y.sample,"H13 stale epoch after skipped calls");
            }
        }
    }
}
static void testFullSupport(){
    need(Shifter::overlapCoverage(.05f)==0.f&&
         Shifter::overlapCoverage(.55f)==1.f&&
         std::abs(Shifter::overlapCoverage(.30f)-.5f)<1.e-6f,
         "H14 coverage endpoints");
    float prev=0.f;
    for(int i=0;i<=1000;++i){
        const float q=Shifter::overlapCoverage(i*.001f);
        need(q>=prev&&q<=1.f,"H14 nonmonotonic coverage");
        prev=q;
    }
    need(Shifter::fullGrainSupported(4000.,3000.,500.,1.08f)&&
         !Shifter::fullGrainSupported(4000.,3990.,500.,1.08f)&&
         !Shifter::fullGrainSupported(4000.,200.,500.,.92f)&&
         !Shifter::fullGrainSupported(20000.,1000.,500.,.92f),
         "H14 full source bounds");
    for(int rate:{48000,96000}){
        Shifter a{},b{};std::array<float,16384> ring{};
        int partial=0,ready=0;double energy=0.,worst=0.;
        for(int i=0;i<rate/2;++i){
            const double ph=6.283185307179586*440.*i/rate;
            // True source holes plus changing period/ratio exercise
            // support loss, partial coverage and a fresh overlap.
            ring[i&16383]=i>rate/5&&i<rate/5+rate/100?0.f:
                static_cast<float>(.2*std::sin(ph)+.04*std::sin(2*ph));
            const float ratio=i<rate/4?1.6f:.75f;
            const auto x=a.process(0,i,i&16383,ring,rate/440.f,ratio,
                                  rate*.036f,.94f,true,true);
            const auto y=b.process(0,i,i&16383,ring,rate/440.f,ratio,
                                  rate*.036f,.94f,true,true);
            need(x.sample==y.sample&&x.coverage==y.coverage&&x.ready==y.ready,
                 "H14 deterministic support");
            need(x.coverage>=0.f&&x.coverage<=1.f&&
                 (!x.ready?x.coverage==0.f:true)&&std::isfinite(x.sample),
                 "H14 invalid support");
            if(x.ready){++ready;if(x.coverage<1.f)++partial;
                energy+=x.sample*x.sample;worst=std::max(worst,double(std::abs(x.sample)));}
        }
        std::cout<<"H14_SUPPORT rate="<<rate<<" ready="<<ready<<" partial="<<partial
                 <<" peak="<<worst<<std::endl;
        need(ready>rate/4&&partial>0&&energy>1.&&worst<.5,
             "H14 support hole fixture");
    }
}
// Two nearby source peaks exchange dominance during a held note.
 // An amplitude-only selector can jump between them despite correct F0.
static void testEpochPhaseContinuity(){
    constexpr double period=200.,tau=6.283185307179586;
    Shifter old{},now{};std::array<float,16384> ring{};
    double lastOld=-1.,lastNow=-1.;int jumpsOld=0,jumpsNow=0;
    for(int i=0;i<48000;++i){
        const double p=std::remainder(i/period,1.);
        const double q=std::remainder(p-.20,1.);
        const double secondary=.22+.10*std::sin(tau*3.*i/48000.);
        ring[i&16383]=static_cast<float>(.22*std::exp(-p*p/.0016)+
            secondary*std::exp(-q*q/.0016)-.03);
        const auto a=old.process(0,i,i&16383,ring,period,1.5f,1536.f,1.f,true,true,false);
        const auto b=now.process(0,i,i&16383,ring,period,1.5f,1536.f,1.f,true,true,true);
        need(std::isfinite(a.sample)&&std::isfinite(b.sample),"H15 finite PCM");
        auto count=[&](double epoch,double& prev,int& jumps){
            if(epoch<0.||epoch==prev)return;
            if(i>4800&&prev>=0.&&std::abs(std::remainder(epoch-prev,period))>period*.10)
                ++jumps;
            prev=epoch;
        };
        count(old.lastEpochForOfflineTests(0),lastOld,jumpsOld);
        count(now.lastEpochForOfflineTests(0),lastNow,jumpsNow);
    }
    std::cout<<"H15_EPOCH old_jumps="<<jumpsOld<<" new_jumps="<<jumpsNow<<std::endl;
    need(jumpsOld>0&&jumpsNow<jumpsOld,"H15 predicted landmark did not improve continuity");
}
// A short loss of admission permission must not erase safe grains.
// A long loss drains to zero and subsequently matches a fresh schedule.
static void testCausalAdmissionDrain(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
        Shifter live{},fresh{};std::array<float,16384> ring{};
        const int first=rate/10,brief=std::max(1,rate/1000),second=rate/5;
        const int resume=second+rate/25,end=resume+rate/10;
        int briefReady=0,tailReady=0,tailSilent=0;
        for(int i=0;i<end;++i){
            ring[i&16383]=static_cast<float>(.2*std::sin(tau*220.*i/rate));
            const bool admit=!(i>=first&&i<first+brief)&&!(i>=second&&i<resume);
            const auto x=live.process(0,i,i&16383,ring,rate/220.f,1.4f,rate*.024f,
                                      1.f,true,true,true,admit);
            need(std::isfinite(x.sample)&&std::abs(x.sample)<.5f,"H18 invalid grain drain");
            if(i>=first&&i<first+brief&&x.ready)++briefReady;
            if(i>=second&&i<resume){
                if(x.ready)++tailReady;
                if(i>second+rate/50&&!x.ready)++tailSilent;
                if(i>second+rate/50)need(!x.ready,"H18 unbounded old grain tail");
            }
            if(i>=resume){
                const auto y=fresh.process(0,i,i&16383,ring,rate/220.f,1.4f,rate*.024f);
                need(x.ready==y.ready&&x.sample==y.sample&&x.coverage==y.coverage,
                     "H18 stale schedule after drain");
            }
        }
        std::cout<<"H18_DRAIN rate="<<rate<<" brief_ready="<<briefReady<<"/"<<brief
                 <<" tail_ready="<<tailReady<<" tail_silent="<<tailSilent<<std::endl;
        need(briefReady>brief/2&&tailReady>0&&tailSilent>0,"H18 did not preserve existing grains");
    }
}
// A detector that briefly reports the previous note while the delayed
// source has a different period. Isolate this from F0 estimation and mix.
static void testDelayedEpochRecovery(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
        Shifter old{},now{},reference{};std::array<float,16384> ring{};
        int aMissing=0,bMissing=0,aPartial=0,bPartial=0,rescues=0;
        std::uint64_t oldSites=0,newSites=0;
        double phase=0.,peak=0.;
        for(int i=0;i<rate/2;++i){
            const double t=double(i)/rate;
            const double hz=t<.15?330.:t<.32?110.:330.;
            phase+=tau*hz/rate;
            ring[i&16383]=static_cast<float>(.18*std::sin(phase)+.035*std::sin(2*phase+.3));
            // Deliberately stale F0 in the 110Hz passage; source history
            // stays genuine and no synthetic silence is inserted.
            const float period=rate/(t<.24?330.f:t<.36?110.f:330.f);
            const auto a=old.process(0,i,i&16383,ring,period,1.3f,rate*.032f,1.f,true,true,true,true,false);
            const auto b=now.process(0,i,i&16383,ring,period,1.3f,rate*.032f,1.f,true,true,true,true,true);
            const auto c=reference.process(0,i,i&16383,ring,period,1.3f,rate*.032f,1.f,true,true,true,true,true,false);
            need(b.sample==c.sample&&b.ready==c.ready&&b.coverage==c.coverage&&
                 b.recovered==c.recovered&&now.lastEpochForOfflineTests(0)==reference.lastEpochForOfflineTests(0),
                 "H21 recovery search changed PCM/epoch");
            oldSites+=c.recoverySites;newSites+=b.recoverySites;
            need(std::isfinite(b.sample)&&std::abs(b.sample)<.4f,"H20 bounded epoch recovery");
            if(i>rate/10){
                aMissing+=!a.ready;bMissing+=!b.ready;
                aPartial+=a.ready&&a.coverage<1.f;bPartial+=b.ready&&b.coverage<1.f;
                rescues+=b.recovered;
            }
            peak=std::max(peak,double(std::abs(b.sample)));
        }
        std::cout<<"H20_RECOVERY rate="<<rate<<" old_missing="<<aMissing<<" new_missing="<<bMissing
                 <<" old_partial="<<aPartial<<" new_partial="<<bPartial<<" rescues="<<rescues
                 <<" peak="<<peak<<std::endl;
        need(aMissing>0&&rescues>0&&bMissing<aMissing&&bPartial+bMissing<aPartial+aMissing,
             "H20 delayed epoch recovery did not reduce unsupported samples");
        std::cout<<"H21_SCAN rate="<<rate<<" old_sites="<<oldSites<<" new_sites="<<newSites<<std::endl;
        need(oldSites>0&&newSites<oldSites*3/4,"H21 redundant search not eliminated");
    }
    // Recovery cannot fabricate epochs in silent source.
    Shifter silence{};std::array<float,16384> zero{};
    for(int i=0;i<4800;++i){
        const auto y=silence.process(0,i,i&16383,zero,200.f,1.3f,1536.f,1.f,true,true,true,true,true);
        need(!y.ready&&!y.recovered,"H20 fabricated silent epoch");
    }
}
// H28B stresses period disagreement beyond the original rescue radius.
// This is an oracle of valid PCM/readiness, not a claim of vowel quality.
static void testAdaptiveEpochRescue(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
        Shifter old{},now{},local{};Shifter::AdmissionTrace trace{},localTrace{};
        std::array<float,16384> ring{};
        double phase=0.;int missingOld=0,missingNew=0,missingLocal=0;
        for(int i=0;i<rate;i++){
            const double sec=double(i)/rate;
            const double hz=sec<.2?440.:sec<.6?82.4:440.;
            phase+=tau*hz/rate;
            const float x=sec>.8?0.f:float(.15*std::sin(phase)+.025*std::sin(2*phase+.3));
            ring[i&16383]=x;
            const float period=rate/(sec<.38?440.f:sec<.65?82.4f:440.f);
            const auto a=old.process(0,i,i&16383,ring,period,1.4f,rate*.036f,1.f,true,true,true,true,true);
            const auto b=now.process(0,i,i&16383,ring,period,1.4f,rate*.036f,1.f,true,true,true,true,true,true,false,&trace,true);
            const auto c=local.process(0,i,i&16383,ring,period,1.4f,rate*.036f,1.f,true,true,true,true,true,true,false,&localTrace,false,true);
            need(std::isfinite(b.sample)&&std::abs(b.sample)<.4f&&std::isfinite(c.sample)&&std::abs(c.sample)<.4f,"H28B/C unsafe sample");
            if(sec>.23&&sec<.4){missingOld+=!a.ready;missingNew+=!b.ready;missingLocal+=!c.ready;}
            if(sec>.95)need(!b.ready&&!b.recovered&&!c.ready&&!c.recovered,"H28B/C fabricated silent epoch");
        }
        need(trace.attempts==trace.admitted+trace.missingPeak+trace.poolBlocked+trace.supportRejected&&
             trace.adaptiveAccepted<=trace.adaptiveAttempts&&trace.adaptiveSites<=trace.adaptiveAttempts*1028,
             "H28B bounded work/accounting");
        std::cout<<"H28B_RESCUE rate="<<rate<<" missing_old="<<missingOld<<" missing_new="<<missingNew
                 <<" attempts="<<trace.adaptiveAttempts<<" accepted="<<trace.adaptiveAccepted<<" sites="<<trace.adaptiveSites<<std::endl;
        need(localTrace.localAccepted<=localTrace.localChecks&&localTrace.adaptiveSites<=localTrace.adaptiveAttempts*1028,"H28C local recovery accounting");
        std::cout<<"H28C_LOCAL rate="<<rate<<" missing_old="<<missingOld<<" missing_local="<<missingLocal<<" checks="<<localTrace.localChecks<<" accepted="<<localTrace.localAccepted<<std::endl;
    }
}
// Independent analytic fractional-tap reference across vocal harmonics.
static void testFractionalBandwidth(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
        double linear=0.,cubic=0.;
        for(double hz:{1500.,3000.,6000.,9000.})for(int n=0;n<1024;++n){
            const double w=tau*hz/rate,phase=tau*n/1024.;
            const float a=static_cast<float>(std::sin(phase-w)),b=static_cast<float>(std::sin(phase));
            const float c=static_cast<float>(std::sin(phase+w)),d=static_cast<float>(std::sin(phase+2*w));
            for(float f:{.125f,.25f,.5f,.75f,.875f}){
                const double target=std::sin(phase+w*f);
                const double l=b*(1.f-f)+c*f-target,q=Shifter::cubicTap(a,b,c,d,f)-target;
                linear+=l*l;cubic+=q*q;
            }
            need(Shifter::cubicTap(a,b,c,d,0.f)==b,"H22 integer tap");
        }
        std::cout<<"H22_TAP rate="<<rate<<" linear_sse="<<linear<<" cubic_sse="<<cubic
                 <<" ratio="<<cubic/linear<<std::endl;
        need(linear>0.&&cubic<linear*.60,"H22 fractional bandwidth");
    }
    // Actual PSOLA grains with fractional epochs, narrowband source
    // resonances and changing pitch. Compare readiness and scheduler exactly.
    for(int rate:{32000,48000,96000,192000}){
        Shifter linear{},cubic{};std::array<float,16384> ring{};
        double phase=0.,ea=0.,eb=0.,diff=0.;int ready=0;
        for(int i=0;i<rate/2;++i){
            const double hz=220.+20.*std::sin(tau*4*i/rate);
            phase+=tau*hz/rate;
            ring[i&16383]=static_cast<float>(.13*std::sin(phase)+.06*std::sin(8*phase+.3)+.035*std::sin(19*phase+.7));
            const float period=static_cast<float>(rate/hz);
            const auto a=linear.process(0,i,i&16383,ring,period,1.5f,rate*.032f,.94f,true,true,true,true,true,true,false);
            const auto b=cubic.process(0,i,i&16383,ring,period,1.5f,rate*.032f,.94f,true,true,true,true,true,true,true);
            need(a.ready==b.ready&&a.coverage==b.coverage&&a.recovered==b.recovered&&
                 linear.lastEpochForOfflineTests(0)==cubic.lastEpochForOfflineTests(0),"H22 scheduler changed");
            need(std::isfinite(b.sample)&&std::abs(b.sample)<.4f,"H22 bounded grain");
            if(i>rate/10&&a.ready){++ready;ea+=a.sample*a.sample;eb+=b.sample*b.sample;
                const double q=a.sample-b.sample;diff+=q*q;}
        }
        std::cout<<"H22_GRAIN rate="<<rate<<" ready="<<ready<<" delta_sse="<<diff
                 <<" energy_ratio="<<eb/ea<<std::endl;
        need(ready>rate/4&&diff>0.&&ea>0.&&eb/ea>.8&&eb/ea<1.3,"H22 grain audition candidate");
    }
}
int main(){
    testFractionalBandwidth();
    testDelayedEpochRecovery();
    testAdaptiveEpochRescue();
    testCausalAdmissionDrain();
    testEpochPhaseContinuity();
    testFullSupport();
    testResumeAfterGap();
    testResonanceScaling();
    // Actual source-period pitch shifting, not just a sine-wave finite test.
    // Three original harmonics approximate one voiced source; FFT/formant and
    // actual adult singer evidence are still necessary before quality claims.
    for(const auto& v:std::array<std::array<double,3>,5>{{
        {{48000.,220.,1.5}},{{48000.,220.,.667}},
        {{48000.,82.40689,1.2}},{{96000.,440.,.75}},
        {{192000.,880.,1.4}}
    }}){
        const auto a=render(v[0],v[1],v[2],64);
        const auto b=render(v[0],v[1],v[2],257);
        need(a.size()==b.size(),"different source length");
        for(std::size_t i=0;i<a.size();++i)
            need(a[i]==b[i],"callback partition changed PSOLA PCM");
        const auto result=measuredHz(a,v[0],v[1]*v[2]);
        need(std::abs(result-v[1]*v[2])/(v[1]*v[2])<.024,
             "synchronous target pitch mismatch");
    }
    // Previously a hard-coded +0.003V positive epoch threshold made
    // genuine soft high-register vowels leave the PSOLA output at zero.
    // Explicitly exercise low-amplitude *negative*-polarity waveforms at
    // ASIO-shaped block boundaries. These tests measure basic continuity,
    // not perceived naturalness in an actual sung phrase.
    for(const double polarity:{-1.,1.}){
        const auto soft64=render(48000.,1046.502261,1.4,64,.004,polarity);
        const auto soft257=render(48000.,1046.502261,1.4,257,.004,polarity);
        need(soft64==soft257,"soft soprano changed with callback partitioning");
        double energy=0.;int audible=0;
        for(int i=14400;i<26400;++i){
            const double x=soft64[static_cast<std::size_t>(i)];
            energy+=x*x;
            if(std::abs(x)>1.e-5)++audible;
        }
        need(energy>1.e-5&&audible>6000,
             "soft high vowel lost epochs or became a rapid gated voice");
    }
    // Invalid or truncated source must return cleanly without allocations or
    // producing undefined samples, even after a partially scheduled phrase.
    Shifter bad;
    std::array<float,16384> silence{};
    need(!bad.process(0,0,0,silence,0.f,1.f,1536.f).ready,
         "invalid F0 accepted");
    need(!bad.process(0,100,100,silence,3200.f,1.f,1536.f).ready,
         "insufficient host latency accepted");
    std::cout<<"bounded source windows and block parity passed\n";
}
