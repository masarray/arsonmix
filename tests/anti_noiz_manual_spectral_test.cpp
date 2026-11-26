// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include "../engine/builtin_fx/anti_noiz/AntiNoizNativeState.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
void check(bool x,const char* why){if(!x){std::cerr<<why<<'\n';std::abort();}}
// Independent double DFT oracle on the ordinary compiler configuration.
// Cancellation can make a single bin arbitrarily small: use the input
// vector norm for forward error, Parseval for aggregate error and the
// original time-domain peak for inverse reconstruction. Exact zero remains
// exact; tiny input is tested relatively rather than against a fixed floor.
void checkFftOracle(){
 using D=std::complex<double>;FftPlan fft;std::mt19937 rng(29);
 std::uniform_real_distribution<float> random(-64.f,64.f);
 double worst=0,roundWorst=0;
 for(int n:{256,512}){
  fft.prepare(n);std::array<D,FftPlan::maxN> roots{};
  for(int k=0;k<n;++k){const long double angle=-2.L*std::numbers::pi_v<long double>*k/n;roots[k]={double(std::cos(angle)),double(std::sin(angle))};}
  for(int trial=0;trial<96;++trial){
   std::array<FftPlan::Complex,FftPlan::maxN> input{},ref{},packed{};
   const float scale=trial%3==0?1.f:trial%3==1?1e-12f:0.f;
   double norm2=0,peak=0;
   for(int i=0;i<n;++i){
    input[i]={random(rng)*scale,trial%4==0?0.f:random(rng)*scale};
    norm2+=std::norm(D(input[i]));peak=std::max(peak,std::abs(D(input[i])));
   }
   ref=packed=input;fft.transform(ref,false);fft.transform(packed,false);
   const double norm=std::sqrt(norm2);double err2[2]{};
   for(int k=0;k<n;++k){
    // Independent scalar extended-precision accumulation.
    long double re=0,im=0;for(int i=0;i<n;++i){const auto w=roots[(k*i)%n];
     const long double x=input[i].real(),y=input[i].imag();
     re+=x*w.real()-y*w.imag();im+=x*w.imag()+y*w.real();
    }
    const D exact{double(re),double(im)};
    for(int j=0;j<2;++j){
     const double err=std::abs(D(j?packed[k]:ref[k])-exact);
     if(!(err<=2e-6*norm)){
      std::cerr<<"FFT_ORACLE_FAIL n="<<n<<" trial="<<trial<<" bin="<<k<<" packed="<<j
       <<" input_norm="<<norm<<" error="<<err<<" expected="<<exact
       <<" actual="<<(j?packed[k]:ref[k])<<'\n';
      check(false,"FFT forward double oracle");
     }
     err2[j]+=err*err;if(norm>0)worst=std::max(worst,err/norm);
    }
   }
   for(double e:err2)check(e<=4e-12*n*norm2,"FFT Parseval relative error");
   fft.transform(ref,true);fft.transform(packed,true);
   for(int i=0;i<n;++i)for(auto val:{ref[i],packed[i]}){
    const double err=std::abs(D(val)-D(input[i]));
    check(err<=2e-6*peak,"FFT independent inverse reconstruction");
    if(peak>0)roundWorst=std::max(roundWorst,err/peak);
   }
  }
 }
 std::cout<<"FFT_ORACLE_PASS cases=192 max_forward_norm_error="<<worst<<" max_roundtrip_peak_error="<<roundWorst<<'\n';
}
int main(){
 checkFftOracle();
 for(float e:{.1f,1.f,2.f,5.f,100.f}){
  float prior=1;
  for(float floor:{1.f,.5f,.1f,.01f}){const float x=ManualSpectral::target(e,1,floor,.5f);check(x<=prior&&x>=floor-1e-6f,"reduction monotonic");prior=x;}
  check(ManualSpectral::target(e,.1f,.01f,0)<=ManualSpectral::target(e,10.f,.01f,0),"threshold monotonic");
  check(ManualSpectral::target(e,1,.01f,1)>=ManualSpectral::target(e,1,.01f,0),"protection monotonic");
 }
 check(ManualSpectral::smooth(.1f,1,1,1)>.1f,"artifact smoothing");
 for(int rate:{44100,48000,88200,96000})for(int channels:{1,2}){
  Core c;Core::Settings s;s.mode=Core::Mode::Voice;s.spectralManual=true;s.cleanDb=24;s.silenceDb=0;s.artifactControl=0;s.vocalProtection=0;
  check(c.prepare(rate,channels)&&c.setSettings(s)&&!c.audition()&&c.beginLearn(),"prepare/learn");
  std::mt19937 rng(19);std::uniform_real_distribution<float> rnd(-.01f,.01f);
  std::vector<float> l(rate*3),r(l.size());for(auto& x:l)x=rnd(rng);r=l;
  c.process(l.data(),channels==2?r.data():nullptr,int(l.size()));check(c.hasProfile(),"learned profile");
  c.resetStreamKeepProfile();
  const auto anchor=c.spectrum().noise;
  Core ref=c;
  std::vector<float> x(rate*3),y(x.size());for(auto& v:x)v=rnd(rng);y=x;
  auto dry=x,wet=x,wetR=y;
  check(c.audition(),"audition admission");
  c.process(x.data(),channels==2?y.data():nullptr,int(x.size()));
  ref.process(wet.data(),channels==2?wetR.data():nullptr,int(wet.size()));
  const int d=c.latencySamples();double err=0,energy=0;
  for(int i=rate/2;i<rate;++i){const double delta=x[i]+wet[i]-dry[i-d];err+=delta*delta;energy+=double(dry[i-d])*dry[i-d];}
  check(err/energy<1e-10,"aligned complement");
  check(!c.auditionActive(),"audition auto expiry");
  for(int i=rate*5/2;i<rate*3;++i)check(x[i]==wet[i],"post audition exact return");
  check(c.spectrum().noise==anchor,"immutable learned anchor");
  // The manual expander owns residual gain, irrespective of the old Silence
  // knob. Auto context must not consume CPU or leak authority into this path.
  Core manualA=c,manualB=c;auto q=s;q.manual.enabled=true;q.manual.rangeDb=0;q.silenceDb=0;
  check(manualA.setSettings(q),"manual authority A");q.silenceDb=48;
  check(manualB.setSettings(q),"manual authority B");auto aL=dry,aR=dry,bL=dry,bR=dry;
  manualA.process(aL.data(),channels==2?aR.data():nullptr,int(aL.size()));
  manualB.process(bL.data(),channels==2?bR.data():nullptr,int(bL.size()));
  check(aL==bL&&aR==bR,"legacy Silence cannot affect manual expander PCM");
  q.manual.enabled=false;check(manualB.setSettings(q),"return automatic dynamics");
  std::fill(bL.begin(),bL.end(),0.f);std::fill(bR.begin(),bR.end(),0.f);
  manualB.process(bL.data(),channels==2?bR.data():nullptr,int(bL.size()));
  check(manualB.residualGain()<.1f&&manualB.spectrum().noise==anchor,"fresh automatic idle evidence and immutable profile");
  check(c.clearProfile()&&!c.hasProfile()&&!c.audition()&&!c.spectrum().noiseValid,"clear");
  check(!c.setSettings([](Core::Settings q){q.artifactControl=NAN;return q;}(s)),"invalid reject");
  s.spectralEnabled=false;check(c.setSettings(s),"bypass");
  auto state=NativeState::encode(s);Core::Settings restored;check(NativeState::decode(state,restored)&&!restored.spectralEnabled&&restored.artifactControl==0,"state");
  std::cout<<rate<<" "<<channels<<"ch spectral complement/expiry/profile/state PASS\n";
 }
}
