// SPDX-License-Identifier: AGPL-3.0-only
// Fixed physical speaker stage candidate. C++ PCM tests do NOT certify
// musical quality on real headphones. No FFT, stem extraction or dynamic EQ.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) || !ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
#error "The fixed stage test must compile the exact opt-in candidate"
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include "../engine/builtin_fx/sonku/SonKuP90StereoDecoder.h"
#include "../engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::p90::StereoDecoder;
using armix::sonku::p98::SceneVoicing;
constexpr double pi=3.14159265358979323846;
using Stereo=std::array<std::vector<float>,2>;

static void physicalStageKernelTest(int rate) {
    StereoDecoder stage;
    stage.useFixedFrontStage(true); // same ordering as JUCE opt-in
    assert(stage.prepare(rate));
    std::array<std::array<float,8>,2> center{}, left{}, right{};
    for(int ear=0;ear<2;++ear)
        for(int k=0;k<8;++k) {
            center[ear][k]=.03f*(ear+1)*(k+1);
            left[ear][k]=.025f*(ear+1)*(k+1);
            right[ear][k]=-.011f*(ear+1)*(k+1);
        }
    const auto originalLeft=left,originalRight=right;
    for(int i=0;i<rate;++i) {
        const float l=.12f*std::sin(2*pi*285.*i/rate);
        const float r=(i<rate/2?l:-l);
        stage.observe(l,r); // correlation changes sign halfway through
        if((i&15)==0)assert(stage.centerBlend()==.58f);
    }
    stage.compose(center,left,right);
    for(int ear=0;ear<2;++ear)for(int k=0;k<8;++k){
        const float mid=.5f*(originalLeft[ear][k]+originalRight[ear][k]);
        const float correction=.58f*(center[ear][k]-mid);
        assert(std::abs(left[ear][k]-(originalLeft[ear][k]+correction))<1.e-7f);
        assert(std::abs(right[ear][k]-(originalRight[ear][k]+correction))<1.e-7f);
        assert(std::abs((left[ear][k]-right[ear][k])-
                        (originalLeft[ear][k]-originalRight[ear][k]))<1.e-7f);
    } // stage MID fixed, differential SIDE unchanged
    stage.reset();
    assert(stage.centerBlend()==.58f);
    // Published SonKu remains the original correlation-based decoder.
    StereoDecoder published;
    assert(published.prepare(rate));
    published.observe(.1f,.1f);
    assert(published.centerBlend()>0.99f);
}

static float sourceSineGain(int rate,int scene,float hz) {
    SceneVoicing stage;
    stage.enableFixedStageWarmth(true);
    assert(stage.prepare(rate));
    double xx=0.,xy=0.;
    for(int i=0;i<rate;++i) {
        const float x=.09f*std::sin(2.*pi*hz*i/rate);
        const auto out=stage.source(x,0.f,scene,true);
        assert(std::isfinite(out.left) && out.right==0.f);
        if(i>rate/2){xx+=double(x)*x;xy+=double(x)*out.left;}
    }
    return static_cast<float>(xy/xx);
}
static void panAndSpectralTest(int rate) {
    // Warmth comes from a low-Q prepared BPF on ORIGINAL L and R:
    // no injected right speaker from a hard-left guitar.
    for(int scene:{SceneVoicing::ImmerSIBDetail,SceneVoicing::ImmerSIBImpact}) {
        const auto body=sourceSineGain(rate,scene,285.f);
        const auto voice=sourceSineGain(rate,scene,2050.f);
        const auto edge=sourceSineGain(rate,scene,3450.f);
        const auto treble=sourceSineGain(rate,scene,5500.f);
        assert(body>1.35f && body<1.53f);
        assert(voice>.98f && voice<1.12f);
        assert(edge>.96f && edge<1.06f);
        assert(treble>.98f && treble<1.06f);
        const auto intelligibility=sourceSineGain(rate,scene,1100.f);
        assert(intelligibility>1.08f && intelligibility<1.30f);
        // The whole voice band is not dynamically ducked to unmask vocals.
        SceneVoicing v;
        v.enableFixedStageWarmth(true);
        assert(v.prepare(rate));
        for(int i=0;i<rate;++i) {
            const float x=.08f*std::sin(2*pi*2050*i/rate);
            const auto s=v.source(x,-x,scene,true);
            assert(std::abs(s.left+s.right)<1.e-6f);
            if(i>rate/2) {
                const auto e=v.early(s.left,s.right);
                const auto f=v.finish(e.left,e.right);
                assert(e.left==s.left && e.right==s.right);
                assert(f.left==e.left && f.right==e.right);
            }
        }
    }
    // Other SonKu scene voicings are untouched even if this module is compiled.
    for(int scene:{0,1,2,3}) {
        SceneVoicing original,candidate;
        candidate.enableFixedStageWarmth(true);
        assert(original.prepare(rate) && candidate.prepare(rate));
        for(int i=0;i<rate/3;++i) {
            const float l=.07f*std::sin(2*pi*997*i/rate);
            const float r=.03f*std::sin(2*pi*3491*i/rate);
            const auto a=original.source(l,r,scene,true);
            const auto b=candidate.source(l,r,scene,true);
            assert(a.left==b.left && a.right==b.right);
            const auto ea=original.early(l,r),eb=candidate.early(l,r);
            assert(ea.left==eb.left && ea.right==eb.right);
            const auto fa=original.finish(l,r),fb=candidate.finish(l,r);
            assert(fa.left==fb.left && fa.right==fb.right);
        }
    }
}

static Stereo runFull(int rate,int scene,int material,bool stage,int block) {
    constexpr int frames=8192; // >100ms: first images and early late FDN
    auto core=std::make_unique<SonKuSpatialCore>();
    if(stage) core->optInImmerSIBStageMonitor();
    else core->optInImmerSIBVocalRoom(); // exact published branded path
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=armix::builtin_fx::imersipP92Factory(scene);
    assert(core->setSettings(s));
    Stereo audio{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i) {
        const double t=double(i)/rate;
        const float voice=float(.040*std::sin(2*pi*285*t)
                              +.027*std::sin(2*pi*1050*t)
                              +.014*std::sin(2*pi*4800*t));
        const float guitar=float(.033*std::sin(2*pi*2050*t)
                               +.025*std::sin(2*pi*3500*t)
                               +.016*std::sin(2*pi*5500*t));
        const float bass=float(.053*std::sin(2*pi*86*t));
        if(material==0){audio[0][i]=voice+guitar+bass;
                        audio[1][i]=voice+.23f*guitar+bass;}
        if(material==1){audio[0][i]=voice+bass;audio[1][i]=voice+bass;}
        if(material==2){audio[0][i]=voice+guitar;audio[1][i]=0.f;}
        if(material==3){audio[0][i]=guitar;audio[1][i]=-guitar;}
    }
    for(int i=0;i<frames;i+=block)
        core->process(audio[0].data()+i,audio[1].data()+i,
                      std::min(block,frames-i));
    for(int ear=0;ear<2;++ear)for(float x:audio[ear]) {
        assert(std::isfinite(x));
        assert(std::abs(x)<=.981f);
    }
    return audio;
}
static double energy(const Stereo& a){
    double e=0.;
    for(const auto& ear:a)for(float x:ear)e+=double(x)*x;
    return e;
}
static double error(const Stereo& a,const Stereo& b){
    double e=0.;
    for(int ear=0;ear<2;++ear)
        for(std::size_t i=0;i<a[ear].size();++i) {
            const double d=double(a[ear][i])-b[ear][i];
            e+=d*d;
        }
    return e;
}
int main() {
    for(int rate:{44100,48000}){
        physicalStageKernelTest(rate);
        panAndSpectralTest(rate);
        for(int scene:{2,3,4,5,6,7,8})
            for(int material:{0,1,2,3}) {
                const auto baseline=runFull(rate,scene,material,false,128);
                const auto candidate=runFull(rate,scene,material,true,128);
                const auto otherBlock=runFull(rate,scene,material,true,256);
                assert(error(candidate,otherBlock)==0.);
                const double a=energy(baseline),b=energy(candidate);
                assert(a>1.e-8 && b>1.e-8);
                assert(b/a>.10 && b/a<10.);
                if(scene==2 && material==0) assert(error(baseline,candidate)>1.e-8);
            }
        std::printf("IMMERSIB_FIXED_STAGE_NO_MASK_FULL_PCM,%d,PASS\n",rate);
    }
    std::puts("PASS: deterministic fixed PA, source L/R no cross-leak, 7 scenes, 44.1/48k and 128/256 parity");
}
