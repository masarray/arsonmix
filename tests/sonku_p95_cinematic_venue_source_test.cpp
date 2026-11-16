// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P95_CINEMATIC_RESEARCH) || !SONKU_P95_CINEMATIC_RESEARCH
#error P9.5 test must compile the actual CINEMATIC native DSP
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP95VenuePrograms.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
struct Measurement {double source=0.,processed=0.;double absSum=0.;double peak=0.;};
static Measurement run(int sampleRate,int scene,int framesPerBlock,bool anti=false){
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(sampleRate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    const auto& v=p95::byId(scene);
    Settings s{};
    s.renderMode=9;s.moving=false;s.clockwise=true;
    s.startAzimuthDegrees=0.f;s.stereoSpreadDegrees=96.f;
    s.spacePreset=scene;s.sourceDistanceMeters=scene==4?3.f:
          scene==5?8.2f:scene==6?9.8f:10.f;
    s.roomAmount=scene==4?.75f:scene==5?.95f:1.f;
    s.ambience=scene==4?.32f:scene==5?.62f:scene==6?.8f:.87f;
    s.spatialMix=1.f;
    s.outputTrim=scene==4?1.584893f:scene==5?1.778279f:
                 scene==6?1.995262f:2.238721f;
    assert(v.width>=8.f&&v.depth>=8.f&&v.height>=4.f);
    assert(s.sourceDistanceMeters<=v.maxDistance);
    const bool validScene=valid(s);
    const bool applied=validScene&&core->setSettings(s);
    if(!applied)
        std::fprintf(stderr,
            "P95_ADMISSION_FAILURE,rate=%d,scene=%d,dist=%.3f,valid=%d,roomGeometryValid=%d,applied=%d\\n",
            sampleRate,scene,s.sourceDistanceMeters,int(validScene),
            int(SonKuEarlyReflections::validGeometry({
                v.width,v.depth,s.sourceDistanceMeters})),int(applied));
    assert(validScene&&applied);
    const int count=sampleRate; // sustained audio, > modal/FDN settling time
    std::vector<float> l(count),r(count);
    constexpr float pi=3.14159265358979323846f;
    Measurement m{};
    for(int i=0;i<count;++i){
        const float t=float(i)/sampleRate;
        const float kick=.17f*std::sin(2*pi*70*t);
        const float bass=.10f*std::sin(2*pi*116*t);
        const float voice=.07f*std::sin(2*pi*960*t);
        const float air=.045f*std::sin(2*pi*5200*t);
        l[i]=kick+bass+voice+air;
        r[i]=anti?-(kick+bass+voice+air):(kick+bass+.93f*voice-.8f*air);
    }
    for(int i=0;i<count;i+=framesPerBlock)
        core->process(l.data()+i,r.data()+i,std::min(framesPerBlock,count-i));
    for(int i=count/3;i<count;++i){
        const float t=float(i)/sampleRate;
        const float kick=.17f*std::sin(2*pi*70*t);
        const float bass=.10f*std::sin(2*pi*116*t);
        const float voice=.07f*std::sin(2*pi*960*t);
        const float air=.045f*std::sin(2*pi*5200*t);
        const double inputL=kick+bass+voice+air;
        const double inputR=anti?-inputL:kick+bass+.93*voice-.8*air;
        m.source+=inputL*inputL+inputR*inputR;
        m.processed+=double(l[i])*l[i]+double(r[i])*r[i];
        m.absSum+=std::abs(l[i])+std::abs(r[i]);
        m.peak=std::max(m.peak,double(std::max(std::abs(l[i]),std::abs(r[i]))));
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(m.peak<=.981);
    }
    m.source=std::sqrt(m.source/(2.*(count-count/3)));
    m.processed=std::sqrt(m.processed/(2.*(count-count/3)));
    m.absSum/=(2.*(count-count/3));
    return m;
}
int main(){
    for(int rate:{44100,48000}){
        std::array<double,8> outputs{};
        for(int scene=4;scene<=7;++scene){
            const auto x=run(rate,scene,64);
            const auto y=run(rate,scene,256);
            const auto anti=run(rate,scene,128,true);
            assert(x.processed>.002 && anti.processed>.002);
            assert(std::abs(x.processed-y.processed)<1.e-8);
            assert(std::abs(x.absSum-y.absSum)<1.e-8);
            outputs[scene]=x.processed;
            const double db=20.*std::log10(x.processed/x.source);
            std::printf("SONKU_P95_LEVEL,%d,scene=%d,input=%.7f,output=%.7f,ratio_dB=%.4f,peak=%.6f\n",
                rate,scene,x.source,x.processed,db,x.peak);
            // The preset must not be a silent "distant" soundtrack.
            assert(db>-6. && db<12.0);
        }
        assert(std::abs(outputs[4]-outputs[5])>1.e-4);
        assert(std::abs(outputs[5]-outputs[6])>1.e-4);
        assert(std::abs(outputs[6]-outputs[7])>1.e-4);
    }
    std::puts("PASS P9.5 real venue audio 44.1/48k: physical scenes, LF modes, anti-phase, 64/256 block-parity, linked peak guard");
}
