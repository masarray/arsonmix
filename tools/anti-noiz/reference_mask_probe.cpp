// SPDX-License-Identifier: AGPL-3.0-only
// Offline diagnostic ONLY: clean reference is unavailable to the live DSP.
#include "OfflineAudio.h"
#include <iostream>
#include <iomanip>
#include <string_view>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
namespace {
struct Scores {double raw=0,error=0,clean=0,projection=0;};
// Same WOLA window, hop and real-valued common stereo gain as Core.
// Per-coefficient constrained LS; NOT a global time-domain optimum.
offline::Audio referenceMask(const offline::Audio& clean,const offline::Audio& noise,float floor){
    offline::require(clean.rate==noise.rate&&clean.channels==noise.channels&&clean.samples.size()==noise.samples.size(),"mismatched stems");
    offline::Audio output=clean;std::fill(output.samples.begin(),output.samples.end(),0.f);
    const int n=clean.rate>48000?512:256,hop=n/2,channels=int(clean.channels);
    const auto frames=static_cast<std::int64_t>(clean.samples.size()/clean.channels);
    FftPlan fft;fft.prepare(n);std::array<float,FftPlan::maxN> win{};
    for(int i=0;i<n;++i)win[i]=std::sqrt(.5f-.5f*std::cos(2.f*std::numbers::pi_v<float>*float(i)/float(n)));
    std::array<std::array<FftPlan::Complex,FftPlan::maxN>,2> desired{},mixed{};
    for(std::int64_t start=-hop;start<frames;start+=hop){
        for(int c=0;c<channels;++c){
            for(int i=0;i<n;++i){const auto t=start+i;float s=0,y=0;
                if(t>=0&&t<frames){const auto j=std::size_t(t)*clean.channels+std::size_t(c);s=clean.samples[j];y=s+noise.samples[j];}
                desired[c][i]={s*win[i],0};mixed[c][i]={y*win[i],0};}
            fft.transform(desired[c],false);fft.transform(mixed[c],false);
        }
        for(int k=0;k<=hop;++k){double dot=0,power=0;
            for(int c=0;c<channels;++c){const auto y=mixed[c][k],s=desired[c][k];dot+=double(s.real())*y.real()+double(s.imag())*y.imag();power+=double(y.real())*y.real()+double(y.imag())*y.imag();}
            const float g=power>1e-30?float(std::clamp(dot/power,double(floor),1.)):1.f;
            for(int c=0;c<channels;++c){mixed[c][k]*=g;if(k>0&&k<hop)mixed[c][n-k]*=g;}
        }
        for(int c=0;c<channels;++c){fft.transform(mixed[c],true);for(int i=0;i<n;++i){const auto t=start+i;
            if(t>=0&&t<frames)output.samples[std::size_t(t)*clean.channels+std::size_t(c)]+=mixed[c][i].real()*win[i];}}
    }
    return output;
}
Scores score(const offline::Audio& s,const offline::Audio& n,const offline::Audio& y,std::size_t begin,std::size_t end){
    offline::require(s.rate==y.rate&&s.channels==y.channels&&s.samples.size()==y.samples.size()&&n.samples.size()==s.samples.size()&&begin<end&&end<=s.samples.size()/s.channels,"mismatched score timeline");
    Scores m;
    for(std::size_t i=begin*s.channels;i<end*s.channels;++i){const double x=s.samples[i],v=n.samples[i],z=y.samples[i];m.raw+=v*v;m.error+=(z-x)*(z-x);m.clean+=x*x;m.projection+=x*z;}
    return m;
}
void print(const char* label,const Scores& m){
    std::cout<<label<<','<<10.*std::log10(std::max(m.raw,1e-24)/std::max(m.error,1e-24))<<',';
    if(m.clean>1e-24)std::cout<<m.projection/m.clean;else std::cout<<"NA";
    std::cout<<'\n';
}
void selfTest(){
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned ch:{1u,2u}){
        offline::Audio s{rate,ch,std::vector<float>(8192*ch)},n=s;
        for(std::size_t i=0;i<s.samples.size();++i){s.samples[i]=.1f*std::sin(float(i/ch)*.071f);n.samples[i]=.03f*std::cos(float(i/ch)*.81f);}
        const auto unity=referenceMask(s,n,1.f);double error=0;
        for(std::size_t i=0;i<s.samples.size();++i){const double d=unity.samples[i]-s.samples[i]-n.samples[i];error=std::max(error,std::abs(d));}
        offline::require(error<2e-6,"WOLA unity reconstruction");
        const auto ideal=referenceMask(s,n,.063095734f);const auto m=score(s,n,ideal,0,8192);
        offline::require(m.error<m.raw&&m.projection/m.clean>.95,"separated-tone reference mask");
        auto zero=s;std::fill(zero.samples.begin(),zero.samples.end(),0.f);const auto idle=referenceMask(zero,n,.063095734f);
        const auto cut=score(zero,n,idle,0,8192);offline::require(std::abs(10.*std::log10(cut.raw/cut.error)-24.)<.001,"floor-only noise");
    }
    std::cout<<"REFERENCE_MASK_SELFTEST PASS\n";
}
void rumbleProbe(){
    // Independent seed, two weak/strong low-register levels; synthetic
    // turbulence proxy only, not a microphone recording.
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned ch:{1u,2u})for(float amp:{.0015f,.012f}){
        offline::Audio clean{rate,ch,std::vector<float>(4*rate*ch)},noise=clean,learn{rate,ch,std::vector<float>(3*rate*ch)};
        std::mt19937 rng(82731);std::uniform_real_distribution<float> u(-1.f,1.f);std::array<float,2> state{};
        const float a=float(1.-std::exp(-2.*std::numbers::pi*65./rate));
        auto sample=[&](unsigned c){state[c]+=a*(u(rng)-state[c]);return .06f*state[c]+.00025f*u(rng);};
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<ch;++c)learn.samples[i*ch+c]=sample(c);
        for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<ch;++c){const auto j=i*ch+c;const double t=double(i)/rate;
            noise.samples[j]=float(1.6+.6*std::sin(2.*std::numbers::pi*3.*t))*sample(c);
            if(i>=rate&&i<3*rate&&(ch==1||c==1)){
                const double phase=2.*std::numbers::pi*110.*t;
                const float env=std::min(1.f,float(i-rate)/(rate*.008f))*std::min(1.f,float(3*rate-i)/(rate*.018f));
                clean.samples[j]=env*amp*float(std::sin(phase)+.30*std::sin(2.*phase)+.20*std::sin(3.*phase));
            }
        }
        auto mixed=clean;for(std::size_t j=0;j<mixed.samples.size();++j)mixed.samples[j]+=noise.samples[j];
        Core::Settings settings;settings.mode=Core::Mode::Voice;settings.cleanDb=24;settings.silenceDb=48;
        const auto actual=offline::render(mixed,&learn,settings,127);
        const auto oracle=referenceMask(clean,noise,.063095734f);
        for(const auto* name:{"reference_known_24dB","candidate"}){
            const auto& audio=std::string_view(name)=="candidate"?actual.processed:oracle;
            const auto active=score(clean,noise,audio,rate,3*rate),idle=score(clean,noise,audio,rate/2,rate);
            std::cout<<rate<<','<<ch<<','<<amp<<','<<name<<','<<10.*std::log10(active.raw/std::max(active.error,1e-24))<<','<<active.projection/active.clean<<','<<10.*std::log10(idle.raw/std::max(idle.error,1e-24))<<'\n';
        }
    }
}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--self-test"){selfTest();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--rumble-probe"){
        std::cout<<std::setprecision(10)<<"rate,channels,amplitude,lane,active_error_db,projection,idle_db\n";rumbleProbe();return 0;
    }
    offline::require(argc==5||argc==6,"usage: reference_mask_probe clean.wav noise.wav begin end [processed.wav]");
    const auto s=offline::read(argv[1]),n=offline::read(argv[2]);const auto begin=std::stoull(argv[3]),end=std::stoull(argv[4]);
    std::cout<<std::setprecision(10)<<"lane,error_improvement_db,desired_projection\n";
    print("reference_known_24dB",score(s,n,referenceMask(s,n,.063095734f),begin,end));
    if(argc==6)print("candidate",score(s,n,offline::read(argv[5]),begin,end));
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
