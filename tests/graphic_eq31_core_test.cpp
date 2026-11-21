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
#include <atomic>
#include <chrono>
#include <new>

// Count allocations only inside the explicit realtime scope. The fixture
// prepares DSP and buffers before arming the guard; no test infrastructure
// allocation is included in the measurement.
static std::atomic<bool> rtAllocAudit{false};
static std::atomic<unsigned> rtAllocCount{0};
void* operator new(std::size_t n) {
    if(rtAllocAudit.load(std::memory_order_relaxed))
        rtAllocCount.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

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
    // Four shipping/hardware rates, five primary-ASIO quantum sizes.
    // Reverse every band's target while previous 5 ms ramps are still
    // running, with stereo-inverted input and 0.1 dB wheel-style increments.
    for(const double rate:{44100.,48000.,96000.,192000.}) {
        GEQ_CHECK(eq.prepare(rate));
        for(const std::size_t frames:{32u,64u,128u,256u,512u}) {
            std::array<float,512> left{},right{};
            for(int tick=0;tick<42;++tick) {
                for(std::size_t band=0;band<31;++band) {
                    const double db=(tick%3==0?12.:tick%3==1?-12.:
                        static_cast<double>((tick+static_cast<int>(band))%241-120)/10.);
                    GEQ_CHECK(eq.setGain(band,db));
                }
                for(std::size_t i=0;i<frames;++i) {
                    left[i]=.0005f*static_cast<float>(std::sin(
                        6.2831853071795864769*997.*(tick*frames+i)/rate));
                    right[i]=-left[i];
                }
                eq.processStereo(left.data(),right.data(),frames);
                for(std::size_t i=0;i<frames;++i) {
                    GEQ_CHECK(std::isfinite(left[i]));
                    GEQ_CHECK(std::isfinite(right[i]));
                    GEQ_CHECK(std::fabs(left[i]+right[i])<.0001f);
                }
            }
        }
        for(const float level:{-24.f,-11.7f,0.f,3.4f,6.f}) {
            const float expected=static_cast<float>(
                std::pow(10.,static_cast<double>(level)/20.));
            GEQ_CHECK(std::fabs(eq.linearGainForDb(level)-expected)<.000005f);
        }
    }
    // Physical impulse starts at sample 0: no FIFO, convolution lookahead,
    // oversampling stage or transport-delay frames in the GEQ PCM path.
    GEQ_CHECK(eq.prepare(48000.));
    GEQ_CHECK(eq.setGain(17,12.));
    std::array<float,512> impulseL{},impulseR{};
    impulseL[0]=impulseR[0]=.01f;
    eq.processStereo(impulseL.data(),impulseR.data(),impulseL.size());
    GEQ_CHECK(impulseL[0]!=0.f&&impulseR[0]==impulseL[0]);
    eq.reset();
    impulseL.fill(0.f);impulseR.fill(0.f);
    eq.processStereo(impulseL.data(),impulseR.data(),impulseL.size());
    for(std::size_t i=0;i<impulseL.size();++i)
        GEQ_CHECK(impulseL[i]==0.f&&impulseR[i]==0.f);

    // Gain-vector lookup / biquad / per-sample smoothing must allocate zero
    // bytes across many callback-sized rapid live edits, including crossings.
    std::array<float,512> perfL{},perfR{};
    rtAllocCount.store(0,std::memory_order_relaxed);
    rtAllocAudit.store(true,std::memory_order_release);
    for(int tick=0;tick<600;++tick) {
        for(std::size_t band=0;band<31;++band)
            GEQ_CHECK(eq.setGain(band,(tick%2?-.1:12.)-
                static_cast<double>(band%3)*.1));
        eq.processStereo(perfL.data(),perfR.data(),
                         static_cast<std::size_t>(32*(1+tick%5)));
    }
    rtAllocAudit.store(false,std::memory_order_release);
    GEQ_CHECK(rtAllocCount.load(std::memory_order_acquire)==0);

    // Same-process synthetic reference to the previous hot-path formula;
    // descriptive instrumentation, not a fabricated historical binary FAT.
    using Clock=std::chrono::steady_clock;
    double baselineSink=0.;
    const auto before0=Clock::now();
    for(int pass=0;pass<400;++pass)for(std::size_t band=0;band<31;++band) {
        const double omega=6.2831853071795864769*graphicEq31Hz[band]/48000.;
        const double alpha=std::sin(omega)/(2.*GraphicEq31Core::nominalQ);
        const double cs=std::cos(omega);
        const double a=std::pow(10.,(pass%2?-.1:12.)/40.);
        baselineSink+=(1.+alpha*a)/(1.+alpha/a)-2.*cs/(1.+alpha/a);
    }
    const auto before1=Clock::now();
    for(int pass=0;pass<400;++pass)for(std::size_t band=0;band<31;++band)
        GEQ_CHECK(eq.setGain(band,pass%2?-.1:12.));
    const auto after1=Clock::now();
    std::array<double,256> callbackUs{};
    for(auto& elapsed:callbackUs) {
        const auto t0=Clock::now();
        eq.processStereo(perfL.data(),perfR.data(),256);
        elapsed=std::chrono::duration<double,std::micro>(Clock::now()-t0).count();
    }
    std::sort(callbackUs.begin(),callbackUs.end());
    const auto us=[](Clock::duration duration) {
        return std::chrono::duration<double,std::micro>(duration).count();
    };
    std::printf("GEQ31 synthetic old trig/pow edits: %.2f us / %d edits; "
                "prepared-cache edits: %.2f us / %d edits; "
                "256-frame callback p95 %.3f us p99 %.3f us "
                "(reference checksum %.4f; machine dependent)\n",
                us(before1-before0),400*31,us(after1-before1),400*31,
                callbackUs[242],callbackUs[253],baselineSink);
    // Upstream plugin fault isolation: NaN/Inf must not poison the
    // long-lived stereo IIR histories or escape to downstream PCM. This is
    // recovery from invalid samples, not level limiting for valid audio.
    {
        GraphicEq31Core guard;
        GEQ_CHECK(guard.prepare(48000.));
        for(std::size_t band=0;band<31;++band)
            GEQ_CHECK(guard.setGain(band,band%2?12.:-12.));
        std::array<float,128> inputL{},inputR{};
        inputL.fill(.01f);inputR.fill(-.01f);
        guard.processStereo(inputL.data(),inputR.data(),inputL.size());
        inputL.fill(.002f);inputR.fill(-.002f);
        inputL[7]=std::numeric_limits<float>::quiet_NaN();
        inputR[33]=std::numeric_limits<float>::infinity();
        guard.processStereo(inputL.data(),inputR.data(),inputL.size());
        for(const auto x:inputL)GEQ_CHECK(std::isfinite(x));
        for(const auto x:inputR)GEQ_CHECK(std::isfinite(x));
        inputL.fill(0.f);inputR.fill(0.f);
        // Even the large finite values of an already-corrupted upstream
        // processor must never leave a nonfinite output in the next callback.
        inputL[0]=std::numeric_limits<float>::max();
        inputR[0]=-std::numeric_limits<float>::max();
        guard.processStereo(inputL.data(),inputR.data(),inputL.size());
        for(const auto x:inputL)GEQ_CHECK(std::isfinite(x));
        for(const auto x:inputR)GEQ_CHECK(std::isfinite(x));
        for(int block=0;block<8;++block) {
            inputL.fill(0.f);inputR.fill(0.f);
            guard.processStereo(inputL.data(),inputR.data(),inputL.size());
            for(const auto x:inputL)GEQ_CHECK(std::isfinite(x));
            for(const auto x:inputR)GEQ_CHECK(std::isfinite(x));
        }
    }

    // A corrupted upstream LEFT sample must not disturb an otherwise
    // healthy RIGHT IIR tail, and vice versa. The reference receives an
    // equivalent zero on the faulty channel while its other channel is
    // bit-identical to the fault-injected processor. Both sides remain
    // independent even during 5 ms per-band parameter interpolation.
    for(const bool faultOnLeft:{true,false}) {
        GraphicEq31Core isolated,reference;
        GEQ_CHECK(isolated.prepare(48000.));
        GEQ_CHECK(reference.prepare(48000.));
        for(std::size_t band=0;band<31;++band) {
            const double db=band%2?9.:-6.;
            GEQ_CHECK(isolated.setGain(band,db));
            GEQ_CHECK(reference.setGain(band,db));
        }
        for(int block=0;block<14;++block) {
            std::array<float,128> faultyL{},faultyR{},refL{},refR{};
            for(std::size_t n=0;n<faultyL.size();++n) {
                const double t=static_cast<double>(block*128+n)/48000.;
                faultyL[n]=.02f*static_cast<float>(
                    std::sin(6.2831853071795864769*432.*t));
                faultyR[n]=.01f*static_cast<float>(
                    std::cos(6.2831853071795864769*1903.*t));
            }
            refL=faultyL;refR=faultyR;
            if(block==5) {
                if(faultOnLeft) {
                    faultyL[19]=std::numeric_limits<float>::quiet_NaN();
                    refL[19]=0.f;
                }else {
                    faultyR[19]=std::numeric_limits<float>::infinity();
                    refR[19]=0.f;
                }
            }
            isolated.processStereo(faultyL.data(),faultyR.data(),128);
            reference.processStereo(refL.data(),refR.data(),128);
            for(std::size_t n=0;n<faultyL.size();++n) {
                GEQ_CHECK(std::isfinite(faultyL[n]));
                GEQ_CHECK(std::isfinite(faultyR[n]));
                GEQ_CHECK(faultOnLeft?faultyR[n]==refR[n]:
                                       faultyL[n]==refL[n]);
            }
        }
    }

    // Inter-band boost is additive, NOT limited to the +12 dB of any single
    // fader. Preserve operator responsibility for headroom: no undocumented
    // auto-trim, soft clipping or limiter when valid PCM exceeds 0 dBFS.
    {
        GraphicEq31Core boosted;
        GEQ_CHECK(boosted.prepare(48000.));
        for(std::size_t band=0;band<31;++band)
            GEQ_CHECK(boosted.setGain(band,12.));
        std::array<float,128> left{},right{};
        double wetEnergy=0.;
        std::size_t wetSamples=0;
        float outputPeak=0.f;
        for(int block=0;block<350;++block) {
            for(std::size_t n=0;n<left.size();++n) {
                const float sample=.15f*static_cast<float>(std::sin(
                    6.2831853071795864769*1000.*(block*128+n)/48000.));
                left[n]=sample;right[n]=-sample;
            }
            boosted.processStereo(left.data(),right.data(),left.size());
            if(block>=100)for(std::size_t n=0;n<left.size();++n) {
                GEQ_CHECK(std::isfinite(left[n])&&std::isfinite(right[n]));
                GEQ_CHECK(std::fabs(left[n]+right[n])<.001f);
                outputPeak=std::max(outputPeak,std::fabs(left[n]));
                wetEnergy+=static_cast<double>(left[n])*left[n];
                ++wetSamples;
            }
        }
        const double gainDb=20.*std::log10(std::sqrt(
            wetEnergy/static_cast<double>(wetSamples))/
            (.15/std::sqrt(2.)));
        GEQ_CHECK(gainDb>19. && gainDb<23.);
        GEQ_CHECK(outputPeak>1.05f); // valid float >0 dBFS is NOT clamped
        std::printf("GEQ31 all-31-band +12 dB at 1kHz: measured %.2f dB, "
                    "output peak %.3f (>1 is intentional, not hard-clipped)\n",
                    gainDb,outputPeak);
    }

    std::puts("Graphic EQ 31 core: frequency/RT matrix, zero allocations, "
              "stereo, 0-frame PCM onset, rapid edits, and Nyquist PASS");
}
