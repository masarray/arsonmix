// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#ifdef _MSC_VER
#include <malloc.h>
#endif
using namespace armix::builtin_fx::de_ezzer;
namespace {
thread_local bool watching=false;
thread_local unsigned allocs=0,frees=0;
void* allocate(std::size_t n){if(watching)++allocs;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void release(void* p) noexcept {if(p&&watching)++frees;std::free(p);}
void* alignedAllocate(std::size_t n,std::size_t a){
    if(watching)++allocs;
    if(n>std::numeric_limits<std::size_t>::max()-a)throw std::bad_alloc{};
#ifdef _MSC_VER
    auto* p=_aligned_malloc(n?n:1,a);
#else
    auto* p=std::aligned_alloc(a,((n?n:1)+a-1)/a*a);
#endif
    if(p)return p;
    throw std::bad_alloc{};
}
void alignedRelease(void* p) noexcept {if(p&&watching)++frees;
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void check(bool ok,const char* why){if(!ok){watching=false;std::cerr<<why<<'\n';std::exit(1);}}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p) noexcept {release(p);}
void operator delete[](void* p) noexcept {release(p);}
void operator delete(void* p,std::size_t) noexcept {release(p);}
void operator delete[](void* p,std::size_t) noexcept {release(p);}
void* operator new(std::size_t n,std::align_val_t a){return alignedAllocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return alignedAllocate(n,static_cast<std::size_t>(a));}
void operator delete(void* p,std::align_val_t) noexcept {alignedRelease(p);}
void operator delete[](void* p,std::align_val_t) noexcept {alignedRelease(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {alignedRelease(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {alignedRelease(p);}
int main(){
    constexpr int lanes=16,blocks=1000,maxFrames=257;
    using Buffer=std::array<float,maxFrames>;
    unsigned long long calls=0,frames=0;
    // Exercise the interception explicitly (not an optimizable new/delete pair).
    watching=true;auto* probe=::operator new(32);::operator delete(probe);auto* aligned=::operator new(64,std::align_val_t{64});::operator delete(aligned,std::align_val_t{64});watching=false;
    check(allocs==2&&frees==2,"allocation counter self test");allocs=frees=0;
    for(double rate:{44100.,48000.,88200.,96000.}){
        std::array<Core,lanes> a{},b{};
        for(int k=0;k<lanes;++k){check(a[k].prepare(rate,1+k%2)&&b[k].prepare(rate,1+k%2),"prepare");}
        std::uint32_t seed=901;
        for(int j=0;j<blocks;++j){
            const int n=1+(j*73)%maxFrames;
            for(int k=0;k<lanes;++k){
                Buffer l{},r{},x{},y{};Settings s;
                s.mode=(j/13+k)%2?Mode::Voice:Mode::Auto;s.shape=(j/17+k/2)%2?Shape::Focused:Shape::Shelf;
                s.amount=static_cast<float>((j+k)%11)/10;s.sensitivity=static_cast<float>((j/3+k)%11)/10;s.rangeDb=static_cast<float>((j+k)%19);s.enabled=j%23!=0;
                for(int i=0;i<n;++i){seed=1664525u*seed+1013904223u;const float v=static_cast<float>(static_cast<double>(seed)/4294967296.*2-1);l[i]=.7f*v;r[i]=-.4f*v;}
                if(j%29==0){l[0]=std::numeric_limits<float>::quiet_NaN();r[0]=std::numeric_limits<float>::infinity();}
                if(j%31==0){l[n-1]=std::numeric_limits<float>::max();r[n-1]=-std::numeric_limits<float>::max();}
                if(j%37==0){l.fill(std::numeric_limits<float>::denorm_min());r.fill(0);}
                x=l;y=r;float* rp=k%2?r.data():nullptr;float* yp=k%2?y.data():nullptr;
                watching=true;
                check(a[k].setSettings(s)&&b[k].setSettings(s),"automation");
                if(j%19==0){Settings bad=s;bad.sensitivity=std::numeric_limits<float>::quiet_NaN();check(!a[k].setSettings(bad),"invalid automation");}
                if(j%43==0){check(!a[k].prepare(0,1),"invalid prepare preserves state");}
                a[k].process(l.data(),rp,n);
                for(int i=0;i<n;){const int count=std::min(n-i,1+(i%31));b[k].process(x.data()+i,yp?yp+i:nullptr,count);i+=count;}
                const auto gr=a[k].bandReductionDb(),w=a[k].focusLowerWeight();
                watching=false;
                check(l==x&&(!rp||r==y),"automation partition parity");
                for(int i=0;i<n;++i)check(std::isfinite(l[i])&&(!rp||std::isfinite(r[i])),"finite PCM");
                check(std::isfinite(gr)&&gr>=0&&gr<=18.001f&&w>=0&&w<=1,"bounded telemetry");
                ++calls;frames+=static_cast<unsigned>(n);
            }
        }
        // Recover from overload, automation and prior attenuation; settled bypass must be exact dry.
        for(auto& c:a){Settings s;s.enabled=false;watching=true;check(c.setSettings(s),"disable");Buffer z{},r{};for(int i=0;i<static_cast<int>(rate)/maxFrames+2;++i){z.fill(0);r.fill(0);c.process(z.data(),r.data(),maxFrames);}z.fill(.125f);r.fill(-.125f);c.process(z.data(),r.data(),maxFrames);watching=false;check(c.bandReductionDb()==0,"unity GR recovery");for(float v:z)check(v==.125f,"settled bypass exact");}
    }
    check(allocs==0&&frees==0,"C++ heap activity on audio owner");
    std::cout<<"PASS: "<<calls<<" primary blocks, "<<frames<<" frames; allocation/free "<<allocs<<'/'<<frees<<"; independent partition replay and automation\n";
}
