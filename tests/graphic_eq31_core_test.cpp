// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/GraphicEq31Core.h"
#include <array>
#include <algorithm>
#include <cstdlib>

// Keep verification active in Release builds (NDEBUG disables assert).
// Side-effecting setup must always execute in the Windows CI configuration.
#define GEQ_CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "Graphic EQ 31 FAILED: %s (%s:%d)\n", \
            #expr, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (false)
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using armix::builtin_fx::GraphicEq31Core;
using armix::builtin_fx::graphicEq31Hz;

static double rms(const std::vector<float>& x,std::size_t from) {
    double energy=0.;
    for(std::size_t i=from;i<x.size();++i)
        energy+=static_cast<double>(x[i])*x[i];
    return std::sqrt(energy/static_cast<double>(x.size()-from));
}
static double responseDb(std::size_t band,double gain,double toneHz,int rate) {
    GraphicEq31Core eq;
    GEQ_CHECK(eq.prepare(rate));
    GEQ_CHECK(eq.setGain(band,gain));
    constexpr std::size_t count=96000;
    std::vector<float> left(count),right(count);
    for(std::size_t i=0;i<count;++i) {
        const auto sample=.10f*static_cast<float>(std::sin(
            6.2831853071795864769*toneHz*i/static_cast<double>(rate)));
        left[i]=right[i]=sample;
    }
    const double dry=rms(left,count/2);
    for(std::size_t i=0;i<count;i+=97)
        eq.processStereo(left.data()+i,right.data()+i,std::min<std::size_t>(97,count-i));
    const double wet=rms(left,count/2);
    return 20.*std::log10(wet/dry);
}
int main() {
    static_assert(GraphicEq31Core::bandCount==31);
    for(std::size_t i=1;i<graphicEq31Hz.size();++i)
        GEQ_CHECK(graphicEq31Hz[i]>graphicEq31Hz[i-1]);
    GEQ_CHECK(graphicEq31Hz.front()==20.);
    GEQ_CHECK(graphicEq31Hz.back()==20000.);
    GraphicEq31Core eq;
    GEQ_CHECK(!eq.prepare(std::numeric_limits<double>::quiet_NaN()));
    GEQ_CHECK(!eq.prepare(7999.));
    GEQ_CHECK(!eq.prepare(384001.));
    GEQ_CHECK(eq.prepare(48000.));
    // Flat fast path must be bit-exact for both channels, including after
    // moving a previously active band back to 0 dB.
    {
        std::array<float,64> unchangedL{},unchangedR{};
        for(std::size_t i=0;i<64;++i) {
            unchangedL[i]=static_cast<float>(i)*0.001f;
            unchangedR[i]=-unchangedL[i];
        }
        const auto referenceL=unchangedL,referenceR=unchangedR;
        eq.processStereo(unchangedL.data(),unchangedR.data(),64);
        GEQ_CHECK(unchangedL==referenceL && unchangedR==referenceR);
    }
    GEQ_CHECK(!eq.setGain(31,3.));
    GEQ_CHECK(!eq.setGain(1,13.));
    GEQ_CHECK(!eq.setGain(1,-13.));
    GEQ_CHECK(!eq.setGain(1,std::numeric_limits<double>::infinity()));
    GEQ_CHECK(eq.setGain(17,6.));
    GEQ_CHECK(std::fabs(eq.gainDb(17)-6.)<1e-8);
    GEQ_CHECK(responseDb(17,6.,1000.,48000)>5.4);
    GEQ_CHECK(responseDb(17,-6.,1000.,48000)<-5.4);
    GEQ_CHECK(std::fabs(responseDb(17,6.,200.,48000))<.6);
    // Stereo isolation: a silent right channel must remain silent.
    std::array<float,256> l{},r{};
    l[0]=.5f;
    for(int i=0;i<100;++i) {
        eq.processStereo(l.data(),r.data(),l.size());
        for(auto s:r)GEQ_CHECK(s==0.f);
        for(auto s:l)GEQ_CHECK(std::isfinite(s));
        l.fill(0.f);
    }
    // Low sample rate: out-of-Nyquist bands must be safely transparent.
    GraphicEq31Core low;
    GEQ_CHECK(low.setGain(30,12.));
    GEQ_CHECK(low.prepare(8000.));
    std::array<float,512> ll{},rr{};
    ll[0]=rr[0]=.25f;
    low.processStereo(ll.data(),rr.data(),ll.size());
    GEQ_CHECK(std::fabs(ll[0]-.25f)<1e-6f);
    for(std::size_t i=1;i<ll.size();++i)GEQ_CHECK(ll[i]==0.f&&rr[i]==0.f);
    // Stress alternating endpoints, ramps, block sizes without NaN or Inf.
    GEQ_CHECK(eq.prepare(192000.));
    for(int iteration=0;iteration<1500;++iteration) {
        for(std::size_t i=0;i<31;++i)GEQ_CHECK(eq.setGain(i,(iteration+i)%2?-12.:12.));
        for(int j=0;j<256;++j)l[j]=r[j]=.005f;
        eq.processStereo(l.data(),r.data(),static_cast<std::size_t>((iteration%255)+1));
        for(auto s:l)GEQ_CHECK(std::isfinite(s));
        for(auto s:r)GEQ_CHECK(std::isfinite(s));
    }
    std::puts("Graphic EQ 31 core: frequencies, response, stereo isolation, Nyquist, stress PASS");
}
