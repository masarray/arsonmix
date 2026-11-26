// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/GraphicEq31Core.h"
#include "fixtures/GraphicEq31Reference20261126.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <limits>
#include <cstdint>
#include <chrono>
using armix::builtin_fx::GraphicEq31Core;
using Reference=armix::test::geq_reference::GraphicEq31Core;
static std::uint32_t bits(float x){return std::bit_cast<std::uint32_t>(x);}
int main(){
    unsigned count=0,unity=0;
    std::uint32_t rng=0x5eeda123u;
    const auto random=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){
        GraphicEq31Core actual;Reference ref;
        assert(actual.prepare(rate)&&ref.prepare(rate));
        std::array<float,257> l{},r{},a{},b{};
        for(int block=0;block<3000;++block){
            // Long flat intervals, sparse curves, and reversals precisely
            // at block boundaries, including ramp completion/re-enable.
            if(block%127==0)for(unsigned i=0;i<31;++i){
                assert(actual.setGain(i,0.)&&ref.setGain(i,0.));
            }
            if(block%127>=48&&block%127<108&&block%3==0){
                const auto band=random()%31;
                const double db=block%5==0?0.:static_cast<double>(static_cast<int>(random()%241)-120)/10.;
                assert(actual.setGain(band,db)&&ref.setGain(band,db));
            }
            const auto n=1u+random()%257;
            for(unsigned i=0;i<n;++i){
                l[i]=i%13==0?-0.f:static_cast<float>(static_cast<int>(random()%2049)-1024)/2048.f;
                r[i]=i%17==0?0.f:l[i]*-.75f;
            }
            if(block%199==0)l[0]=std::numeric_limits<float>::infinity();
            if(block%211==0)r[0]=std::numeric_limits<float>::quiet_NaN();
            if(block%223==0)l[0]=std::bit_cast<float>(1u);
            a=l;b=r;
            float peak=-1.f;
            if(actual.tryUnityPeak(a.data(),b.data(),n,peak)){
                ++unity;
                float expected=0.f;
                for(unsigned i=0;i<n;++i)expected=std::max(expected,std::max(std::abs(l[i]),std::abs(r[i])));
                assert(bits(peak)==bits(expected));
            }else actual.processStereo(a.data(),b.data(),n);
            ref.processStereo(l.data(),r.data(),n);
            for(unsigned i=0;i<n;++i)assert(bits(a[i])==bits(l[i])&&bits(b[i])==bits(r[i]));
            ++count;
        }
    }
    assert(count==18000&&unity>1000);
    std::printf("PASS: %u GEQ sparse/flat/automation blocks bit-exact vs frozen prior core; %u unity shortcuts\n",count,unity);
    // Descriptive same-runner core A/B only. No timing gate: OS scheduling,
    // compiler and synthetic PCM are not an ASIO field measurement.
    const auto bench=[](auto& core,int bands){
        assert(core.prepare(48000.));
        for(int i=0;i<bands;++i)assert(core.setGain(static_cast<unsigned>(5+i*7),3.));
        std::array<float,64> l{},r{};
        double checksum=0.;
        const auto start=std::chrono::steady_clock::now();
        for(int block=0;block<100000;++block){
            for(unsigned i=0;i<64;++i){l[i]=static_cast<float>((i+block)%63)/128.f;r[i]=-l[i];}
            core.processStereo(l.data(),r.data(),l.size());
            checksum+=l[static_cast<unsigned>(block)%64];
        }
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count();
        return std::pair{ns,checksum};
    };
    for(int bands:{0,3}){
        GraphicEq31Core fast;Reference old;
        const auto baseline=bench(old,bands),optimized=bench(fast,bands);
        assert(baseline.second==optimized.second);
        std::printf("GEQ CORE A/B bands=%d old_ns/block=%.1f new_ns/block=%.1f (non-gating)\n",
            bands,baseline.first/100000.,optimized.first/100000.);
    }
}
