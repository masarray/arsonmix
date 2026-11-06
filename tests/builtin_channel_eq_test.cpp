// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/ChannelEq.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
std::atomic<bool> noHeap{false};
[[noreturn]] void fail(const char* test) noexcept {
    std::fprintf(stderr, "FAIL: %s\n", test);
    std::abort();
}
void check(bool ok, const char* test) noexcept { if (!ok) fail(test); }
bool near(double actual, double expected, double tolerance) noexcept {
    return std::abs(actual-expected) <= tolerance;
}
} // namespace

// Optional hard tripwire: processing/configuration must never request heap.
void* operator new(std::size_t n) {
    if (noHeap.load(std::memory_order_relaxed)) fail("heap allocation in built-in DSP");
    if (auto* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (noHeap.load(std::memory_order_relaxed)) fail("heap allocation in built-in DSP[]");
    if (auto* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace armix::builtin_fx;
static Band onBand(ChannelEq& eq, std::size_t slot, float freq, float gain=0.0f,
                   float q=1.0f, Slope slope=Slope::db12) {
    Band b=eq.band(slot);
    b.enabled=true; b.frequencyHz=freq; b.gainDb=gain; b.q=q; b.slope=slope;
    check(eq.setBand(slot,b),"set band accepted");
    return b;
}
static void runUntilSettled(ChannelEq& eq) {
    std::array<float,512> l{},r{};
    while(eq.transitioning())eq.processStereo(l.data(),r.data(),l.size());
}
static void testCatalogAndDefault() {
    check(channelEqId=="armix.builtin.channel-eq.v1","stable built-in ID");
    check(ChannelEq::bandCount==8,"eight slots exactly");
    check(channelEqSlots[0].kind==FilterKind::highPass,"fixed HPF slot");
    check(channelEqSlots[7].kind==FilterKind::lowPass,"fixed LPF slot");
    for(int rate : {44100,48000,96000}) {
        ChannelEq eq;
        check(eq.prepare(rate),"prepare sample rate");
        check(eq.activeBands()==0,"all filters off on first launch");
        check(eq.addedLatencySamples()==0,"no buffering/lookahead");
        std::array<float,128> l{},r{};
        for (std::size_t i=0;i<l.size();++i) {
            l[i]=static_cast<float>(i)*0.001f - 0.05f;
            r[i]=1.0f-l[i];
        }
        const auto unchangedL=l,unchangedR=r;
        eq.processStereo(l.data(),r.data(),l.size());
        check(l==unchangedL&&r==unchangedR,"exact transparent disabled stereo");
        eq.processMono(l.data(),l.size());
        check(l==unchangedL,"exact transparent disabled mono");
        eq.processStereo(nullptr,nullptr,128);
        eq.processMono(nullptr,128);
    }
    ChannelEq lowSample;
    check(lowSample.prepare(8000),"8k rate supported");
    check(lowSample.band(7).frequencyHz<=3600.f,"disabled default clamps below Nyquist");
    check(!lowSample.prepare(0)&&!lowSample.prepare(std::numeric_limits<double>::infinity()),"invalid sample rate rejected");
}
static void testResponse() {
    ChannelEq eq;
    check(eq.prepare(48000),"prepare");
    check(near(eq.responseDb(1000),0,1e-10),"off response flat");
    onBand(eq,2,1000,6,1);
    check(near(eq.responseDb(1000),6,0.07),"PEQ +6 dB center");
    check(std::abs(eq.responseDb(30))<1,"bell does not affect DC region much");
    onBand(eq,1,160,5,1);
    check(eq.responseDb(30)>4,"low shelf +5dB low frequency");
    onBand(eq,6,8000,-5,1);
    check(eq.responseDb(15000)<-3,"high shelf attenuates high frequencies");
    ChannelEq cuts;
    check(cuts.prepare(48000),"prepare cuts");
    onBand(cuts,0,100,0,.707f,Slope::db12);
    const double hpf12=cuts.responseDb(25);
    check(hpf12<-22 && hpf12>-29,"HPF 12 dB/oct falloff");
    onBand(cuts,0,100,0,.707f,Slope::db24);
    check(cuts.responseDb(25)<hpf12-18,"HPF 24 dB/oct steeper");
    cuts.resetToFlat();
    onBand(cuts,7,8000,0,.707f,Slope::db12);
    const double lpf12=cuts.responseDb(18000);
    check(lpf12 < -16,"LPF 12 dB/oct attenuates Nyquist region");
    onBand(cuts,7,8000,0,.707f,Slope::db24);
    check(cuts.responseDb(18000) < lpf12-12,"LPF 24 dB/oct steeper");
}
static void testValidationAndReset() {
    ChannelEq eq;
    check(!eq.setBand(2,Band{}),"must prepare before control");
    check(eq.prepare(44100),"prepare");
    const auto base=eq.band(3);
    for(const float invalid : {0.f,-20.f,44000.f,
                               std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        auto b=base;b.enabled=true;b.frequencyHz=invalid;
        check(!eq.setBand(3,b),"invalid frequency rejected");
    }
    auto b=base;b.enabled=true;b.gainDb=21.f;
    check(!eq.setBand(3,b),"invalid gain rejected");
    b=base;b.enabled=true;b.q=0.0f;
    check(!eq.setBand(3,b),"invalid q rejected");
    b=base;b.enabled=true;b.slope=static_cast<Slope>(99);
    check(!eq.setBand(3,b),"invalid slope rejected");
    check(!eq.setBand(8,base),"invalid slot rejected");
    check(eq.activeBands()==0,"failed writes leave existing state unchanged");
    onBand(eq,2,1000,9);
    runUntilSettled(eq);
    const auto before=eq.band(2);
    auto wrong=before;wrong.frequencyHz=30000;
    check(!eq.setBand(2,wrong),"failed update leaves active filter untouched");
    check(eq.band(2).frequencyHz==before.frequencyHz,"readback unchanged on rejected edit");
    check(eq.resetToFlat(),"reset all in one transition");
    runUntilSettled(eq);
    check(eq.activeBands()==0 && near(eq.responseDb(1000),0,0.0001),"flat reset");
    check(!eq.transitioning(),"reset fade completed");
}
static void testAudibleProcessingAndIndependence() {
    ChannelEq eq;
    check(eq.prepare(48000),"prepare");
    onBand(eq,2,1000,12,0.7f);
    runUntilSettled(eq);
    constexpr int n=128;
    std::array<float,n> left{}, right{};
    left[0]=1.0f;
    eq.processStereo(left.data(),right.data(),left.size());
    check(left[0]>0.0f,"first sample delivered, no initial buffer/latency");
    check(std::all_of(right.begin(),right.end(),[](float v){return v==0.f;}),"no L->R leakage");
    ChannelEq mono;
    check(mono.prepare(48000),"mono prepare");
    onBand(mono,2,1000,12,0.7f);
    runUntilSettled(mono);
    std::array<float,n> x{};x[0]=1;
    mono.processStereo(x.data(),x.data(),x.size()); // alias -> once only
    check(near(x[0],left[0],1e-6),"alias-safe stereo uses mono path once");

    ChannelEq gate;
    check(gate.prepare(48000),"gate prepare");
    onBand(gate,1,160,6,1);
    runUntilSettled(gate);
    std::array<float,512> held{};held.fill(1.f);
    for(int i=0;i<16;++i) { held.fill(1.f); gate.processMono(held.data(),held.size()); }
    check(held.back()>1.7,"actual LF shelf modifies PCM");
    const float previousWet = held.back();
    check(gate.resetToFlat(),"bypass request");
    held.fill(1.f);
    gate.processMono(held.data(),held.size());
    check(std::abs(held[0]-previousWet)<0.1f,"start bypass transition follows last wet output");
    check(near(held.back(),1.0,1e-6),"finished bypass returns exact dry PCM");
    check(!gate.transitioning(),"bypass ramp completed");
}
static void testFourteenIndependentChannels() {
    // 10 CH + 4 AUX. Test per-channel filter/history isolation, not 14 EQs
    // accidentally chained into a single PCM buffer.
    std::array<ChannelEq,14> lanes{};
    for(auto& eq:lanes)check(eq.prepare(48000),"14 independent preparations");
    onBand(lanes[0],2,1000,9,0.7f);
    runUntilSettled(lanes[0]);
    onBand(lanes[13],0,120,0,0.707f,Slope::db24);
    runUntilSettled(lanes[13]);
    std::array<std::array<float,128>,14> left{},right{};
    std::array<float,128> reference1000{},reference60{};
    for(unsigned repeat=0;repeat<64;++repeat) {
        for(std::size_t i=0;i<reference1000.size();++i) {
            const double t=static_cast<double>(repeat*reference1000.size()+i)/48000.0;
            reference1000[i]=0.1f*std::sin(6.283185307179586*t*1000.0);
            reference60[i]=0.1f*std::sin(6.283185307179586*t*60.0);
        }
        for(std::size_t lane=0;lane<lanes.size();++lane) {
            left[lane]=lane==13?reference60:reference1000;
            for(std::size_t i=0;i<right[lane].size();++i)
                right[lane][i]=-left[lane][i];
        }
        noHeap.store(true,std::memory_order_release);
        for(std::size_t lane=0;lane<lanes.size();++lane)
            lanes[lane].processStereo(left[lane].data(),right[lane].data(),128);
        noHeap.store(false,std::memory_order_release);
        check(left[1]==reference1000&&left[12]==reference1000,
              "disabled EQ on unrelated channels is bitwise transparent");
    }
    double dry1000=0,boost1000=0,dry60=0,cut60=0;
    for(std::size_t i=0;i<128;++i) {
        dry1000+=static_cast<double>(reference1000[i])*reference1000[i];
        boost1000+=static_cast<double>(left[0][i])*left[0][i];
        dry60+=static_cast<double>(reference60[i])*reference60[i];
        cut60+=static_cast<double>(left[13][i])*left[13][i];
    }
    check(boost1000>dry1000*4,"CH01 independent PEQ boost is audible PCM");
    check(cut60<dry60*0.02,"AUX04 independent HPF attenuation is audible PCM");
    check(lanes[1].activeBands()==0 && lanes[0].activeBands()==1 &&
          lanes[13].activeBands()==1,"stable per-channel configuration state");
}
static void testPrecomputedControlToAudioBoundary() {
    ChannelEq eq;
    check(eq.prepare(48000),"precompute destination prepared");
    auto band=eq.band(4);
    band.enabled=true;band.frequencyHz=2000;band.gainDb=7.0f;band.q=1.2f;
    ChannelEq::PreparedBandEdit packet{};
    check(ChannelEq::prepareBandEdit(4,band,48000,packet),"precompute control packet");
    check(packet.valid&&packet.sections==1,"precomputed one PEQ biquad");
    std::array<float,128> l{},r{};
    l[0]=1.f;
    noHeap.store(true,std::memory_order_release);
    const bool accepted=eq.applyPreparedBand(packet);
    if(accepted)eq.processStereo(l.data(),r.data(),l.size());
    noHeap.store(false,std::memory_order_release);
    check(accepted,"audio owner applies precomputed packet");
    check(eq.activeBands()==1&&eq.band(4).gainDb==7.0f,"accepted DSP readback");
    check(l[0]>0.0f,"precomputed audio sample delivered");

    ChannelEq changedRate;
    check(changedRate.prepare(44100),"changed rate instance");
    check(!changedRate.applyPreparedBand(packet),"stale sample rate packet rejected");
    check(changedRate.activeBands()==0,"stale packet no partial application");
    ChannelEq::PreparedBandEdit invalidPacket=packet;
    invalidPacket.sections=99;
    check(!eq.applyPreparedBand(invalidPacket),"untrusted section count rejected");
    invalidPacket=packet;
    invalidPacket.coefficients[0][0]=std::numeric_limits<double>::quiet_NaN();
    // Force meaningful change so duplicate same-state shortcut cannot conceal
    // a malformed coefficient payload.
    invalidPacket.band.gainDb=8.0f;
    check(!eq.applyPreparedBand(invalidPacket),"malformed filter coefficient rejected");
    check(eq.band(4).gainDb==7.0f,"rejected packet does not replace DSP state");
}
static void testNoHeapAndVariableQuantum() {
    ChannelEq eq;
    check(eq.prepare(48000),"prepare");
    std::array<float,512> l{},r{};
    for (auto count : {32u,64u,128u,512u}) {
        l.fill(0.12f);r.fill(-0.07f);
        auto hp=eq.band(0);hp.enabled=true;hp.frequencyHz=90+static_cast<float>(count)/10.f;
        hp.slope=Slope::db24;
        ChannelEq::PreparedBandEdit prepared{};
        check(ChannelEq::prepareBandEdit(0,hp,48000,prepared),"control precomputes HPF");
        noHeap.store(true,std::memory_order_release);
        const bool ok=eq.applyPreparedBand(prepared);
        if(ok)eq.processStereo(l.data(),r.data(),count);
        noHeap.store(false,std::memory_order_release);
        check(ok,"parameter writes without allocation");
        check(std::isfinite(l[0])&&std::isfinite(l[count-1])&&
              std::isfinite(r[count-1]),"variable quantum finite stereo");
    }
}
int main() {
    testCatalogAndDefault();
    testResponse();
    testValidationAndReset();
    testAudibleProcessingAndIndependence();
    testPrecomputedControlToAudioBoundary();
    testNoHeapAndVariableQuantum();
    testFourteenIndependentChannels();
    std::puts("PASS: ArSonMix built-in 8-slot native Channel EQ; no ArSonEQ/VST3 dependency");
}
