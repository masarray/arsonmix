// SPDX-License-Identifier: AGPL-3.0-only
// P8.0 objective comparisons across exactly the real SonKuSpatialCore,
// compiled twice from the SAME commit (P7.5 baseline vs measured P8.0).
// Writes deterministic numbers only: no external audio, proprietary IR or
// fabricated subjective dearVR parity. Read with sonku_p80_evidence.py.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
using namespace armix::sonku;
constexpr double pi=3.141592653589793238462643;
static constexpr int fs=48000;
static constexpr float inputLevel=.095f;
#if defined(SONKU_P80_MEASURED_TIMBRE_RESEARCH) && SONKU_P80_MEASURED_TIMBRE_RESEARCH
static constexpr const char* mode="P80";
#else
static constexpr const char* mode="P75";
#endif

static std::unique_ptr<SonKuSpatialCore> create(Settings s) {
    auto core=std::make_unique<SonKuSpatialCore>();
    if(!core->prepare(fs,SonKuReflectionProfile::SixSurfaceKemarResearch) ||
       !core->setSettings(s))return {};
    core->reset();
    return core;
}
struct StereoTone {
    double l=0.,r=0.,m=0.,s=0.;
};
static double magnitude(double sinPart,double cosPart,int frames) {
    return 2.*std::hypot(sinPart,cosPart)/frames;
}
static StereoTone tone(const Settings& s,double hz,int kind) {
    auto core=create(s);
    if(!core)std::abort();
    constexpr int warm=4096,measure=8192,frames=warm+measure;
    std::array<double,4> ss{},cc{};
    std::array<float,128> left{},right{};
    for(int start=0;start<frames;start+=128){
        for(int j=0;j<128;++j){
            const int at=start+j;
            const float v=inputLevel*static_cast<float>(std::sin(2*pi*hz*at/fs));
            left[j]=kind==1?v:(kind==2?0.f:v);
            right[j]=kind==1?-v:(kind==2?v:v);
        }
        core->process(left.data(),right.data(),128);
        for(int j=0;j<128;++j){
            const int at=start+j;
            if(at<warm)continue;
            const double phase=2*pi*hz*at/fs;
            const double si=std::sin(phase),co=std::cos(phase);
            const std::array<double,4> signals{
                left[j],right[j],(left[j]+right[j])*.5,
                (left[j]-right[j])*.5};
            for(int k=0;k<4;++k){
                ss[k]+=signals[k]*si;cc[k]+=signals[k]*co;
            }
        }
    }
    return {magnitude(ss[0],cc[0],measure),
            magnitude(ss[1],cc[1],measure),
            magnitude(ss[2],cc[2],measure),
            magnitude(ss[3],cc[3],measure)};
}
struct Energies{double direct=0.,early=0.,tail=0.;};
static Energies impulse(const Settings& s) {
    auto core=create(s);if(!core)std::abort();
    constexpr int frames=48000,chunk=128;
    std::array<float,chunk> l{},r{};
    Energies e{};
    for(int start=0;start<frames;start+=chunk){
        l.fill(0.f);r.fill(0.f);
        if(start==0){l[0]=r[0]=inputLevel;}
        core->process(l.data(),r.data(),chunk);
        for(int j=0;j<chunk;++j){
            const int at=start+j;
            const double energy=l[j]*l[j]+r[j]*r[j];
            if(at<960)e.direct+=energy;          // first 20ms
            else if(at<4800)e.early+=energy;       // 20-100ms
            else e.tail+=energy;                   // 100-1000ms
        }
    }
    return e;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path out=argv[1];
    std::filesystem::create_directories(out);
    auto csv=std::ofstream(out/(std::string("sonku_")+mode+"_tone.csv"));
    if(!csv)return 3;
    csv<<"mode,fs,source,room,azimuth,frequency_hz,left_amp,right_amp,mid_amp,side_amp\n";
    csv<<std::setprecision(10);
    Settings s{};s.moving=false;s.spatialMix=1.f;s.outputTrim=.65f;
    s.stereoSpreadDegrees=60.f;s.spacePreset=0;
    const std::array<double,8> freqs{80.,125.,250.,500.,1000.,2000.,4000.,8000.};
    for(const char* scene:{"FRONT","REAR"}) {
        s.startAzimuthDegrees=std::string(scene)=="FRONT"?0.f:180.f;
        for(int kind=0;kind<3;++kind) {
            for(const auto hz:freqs){
                const auto result=tone(s,hz,kind);
                csv<<mode<<","<<fs<<","<<(kind==0?"MONO":kind==1?"ANTIPHASE":"RIGHT_ONLY")
                   <<",OFF,"<<s.startAzimuthDegrees<<","<<hz<<","
                   <<result.l<<","<<result.r<<","
                   <<result.m<<","<<result.s<<"\n";
            }
        }
    }
    auto ir=std::ofstream(out/(std::string("sonku_")+mode+"_room.csv"));
    if(!ir)return 4;
    ir<<"mode,scene,first20_energy,early20_100_energy,late100_1000_energy\n";
    ir<<std::setprecision(12);
    s.startAzimuthDegrees=35.f;s.spacePreset=1;s.sourceDistanceMeters=1.6f;
    const std::array<std::array<float,2>,4> rooms{{
        {{0.f,0.f}},{{.65f,0.f}},{{0.f,.45f}},{{.65f,.45f}}
    }};
    const std::array<const char*,4> labels{
        "SCENE_DRY","SCENE_ER_ONLY","SCENE_LATE_ONLY","SCENE_BOTH"
    };
    for(std::size_t n=0;n<rooms.size();++n){
        s.roomAmount=rooms[n][0];s.ambience=rooms[n][1];
        const auto e=impulse(s);
        ir<<mode<<","<<labels[n]<<","<<e.direct<<","<<e.early
          <<","<<e.tail<<"\n";
    }
    std::printf("SONKU_P80_ACOUSTIC_EVIDENCE=%s,output=%s\n",mode,out.string().c_str());
    return 0;
}
