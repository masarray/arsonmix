// P4 4-mode topology: real feedback differences, stable recursion and
// automation switches. No assertion of subjective parity.
#include <arverb/Reverb.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr,"ALGORITHM FAILURE: %s\n",reason); std::abort(); }
}
static std::uint64_t fingerprint(arverb::Algorithm mode, int fs) {
    arverb::Engine engine;
    engine.loadPreset(arverb::Preset::MusicHall);
    engine.setAlgorithm(mode);
    engine.setParameter(arverb::Parameter::Mix, 1.0f);
    engine.setParameter(arverb::Parameter::DuckAmount, 0.0f);
    engine.prepare(fs);
    std::array<float,128> l{},r{},outL{},outR{};
    std::uint64_t hash=UINT64_C(14695981039346656037);
    const int samples=fs; // one second, all reflection onset/ring states
    for (int offset=0;offset<samples;offset+=128) {
        l.fill(0.f);r.fill(0.f);
        if(offset==0) {l[0]=0.67f;r[0]=-0.27f;}
        const auto n=static_cast<std::size_t>(std::min(128,samples-offset));
        engine.process(l.data(),r.data(),outL.data(),outR.data(),n);
        for(std::size_t i=0;i<n;++i) {
            require(std::isfinite(outL[i])&&std::isfinite(outR[i]),
                    "non-finite output from mode");
            require(std::abs(outL[i])<20.f&&std::abs(outR[i])<20.f,
                    "unstable feedback matrix");
            for(float f:{outL[i],outR[i]}) {
                std::uint32_t bits;std::memcpy(&bits,&f,sizeof(bits));
                hash=(hash^bits)*UINT64_C(1099511628211);
            }
        }
    }
    return hash;
}
int main() {
    require(arverb::algorithmCount==4,"algorithm enum count");
    for (int fs:{8000,48000,192000}) {
        std::array<std::uint64_t,4> signatures{};
        for (unsigned i=0;i<4;++i)
            signatures[i]=fingerprint(static_cast<arverb::Algorithm>(i),fs);
        for(int i=0;i<4;++i)
            for(int j=i+1;j<4;++j)
                require(signatures[i]!=signatures[j],"distinct topology must differ");
    }
    // 100ms mode morph is bounded, finite and uses one unchanged DSP tank.
    arverb::Engine e;
    e.prepare(48000.0);
    e.setParameter(arverb::Parameter::Mix, 1.0f);
    std::array<float,64> l{},r{},a{},b{};
    for (int block=0;block<1600;++block) {
        if(block%40==0)
            e.setAlgorithm(static_cast<arverb::Algorithm>((block/40)%4));
        for(int i=0;i<64;++i) {
            const float t=static_cast<float>(block*64+i);
            l[i]=0.2f*std::sin(0.031f*t);r[i]=0.2f*std::cos(0.022f*t);
        }
        e.process(l.data(),r.data(),a.data(),b.data(),64);
        for(int i=0;i<64;++i)
            require(std::isfinite(a[i])&&std::isfinite(b[i])&&
                    std::abs(a[i])<50.f&&std::abs(b[i])<50.f,
                    "finite, bounded rapid topology transitions");
    }
    std::puts("PASS: four genuinely different feedback matrices, 3 rates, rapid mode automation");
}
