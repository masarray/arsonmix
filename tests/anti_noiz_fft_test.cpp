// SPDX-License-Identifier: AGPL-3.0-only
// Independent scalar reference for the production FFT plan, including staged twiddles.
#include "../engine/builtin_fx/anti_noiz/AntiNoizFft.h"
#include <cstdlib>
#include <iostream>
#include <random>
using armix::builtin_fx::anti_noiz::FftPlan;
namespace {
using C=FftPlan::Complex;
using Buffer=std::array<C,FftPlan::maxN>;
void require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
void reference(Buffer& a,int n,bool inverse){
    std::array<C,FftPlan::maxN/2> w{};
    for(int i=0;i<n/2;++i){const float phase=-2.f*std::numbers::pi_v<float>*float(i)/float(n);w[i]={std::cos(phase),std::sin(phase)};}
    for(int i=0;i<n;++i){unsigned x=unsigned(i),r=0;for(int j=n;j>1;j>>=1){r=(r<<1)|(x&1u);x>>=1;}if(i<int(r))std::swap(a[i],a[r]);}
    for(int len=2;len<=n;len*=2){const int half=len/2,step=n/len;
        for(int base=0;base<n;base+=len)for(int j=0;j<half;++j){const auto u=a[base+j],v=a[base+j+half]*(inverse?std::conj(w[j*step]):w[j*step]);a[base+j]=u+v;a[base+j+half]=u-v;}}
    if(inverse)for(int i=0;i<n;++i)a[i]/=float(n);
}
}
int main(){
    std::mt19937 rng(751);std::uniform_real_distribution<float> noise(-64.f,64.f);
    for(int n:{256,512}){FftPlan f;f.prepare(n);double peak=0;
        for(int k=0;k<64;++k){Buffer a{};for(int i=0;i<n;++i){if(k==1)a[i]={i==7?1.f:0.f,0.f};else if(k==2)a[i]={64.f,-64.f};else if(k>=3)a[i]={noise(rng),noise(rng)};}
            auto b=a,original=a;
            for(bool inverse:{false,true}){f.transform(a,inverse);reference(b,n,inverse);
                for(int i=0;i<n;++i){const double error=std::abs(a[i]-b[i]);peak=std::max(peak,error);require(std::isfinite(error)&&error<=2e-6*std::max(1.,double(std::abs(b[i]))),"scalar FFT comparison");}}
            for(int i=0;i<n;++i)require(std::abs(a[i]-original[i])<1e-4f,"FFT roundtrip");
        }
        std::cout<<"FFT_SCALAR_PARITY,"<<n<<",cases=64,peak="<<peak<<'\n';
    }
}
