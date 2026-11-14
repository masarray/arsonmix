// SPDX-License-Identifier: AGPL-3.0-only
// P8.3 actual SonKuSpatialCore native PCM, no JS/WebAudio approximation.
// This gate proves source geometry, directional PCM and stem separability;
// the human headphone externalization verdict is deliberately NOT asserted.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
namespace p83=armix::sonku::p83;

struct Render { std::vector<float> l,r; };
enum class Input { ImpulseLeft, ImpulseRight, HardLeft, HardRight,
                   MonoMusic, AntiPhase, Partial };
static constexpr double pi=3.14159265358979323846;
static void check(bool ok,const char* reason) {
    if(!ok){std::fprintf(stderr,"FAIL P83: %s\n",reason);std::abort();}
}
static Render render(double rate,p83::Mode mode,float az,float yaw,
                     bool room,p83::Stem stem,std::uint8_t mask,Input input,
                     int chunk=128) {
    auto core=std::make_unique<SonKuSpatialCore>();
    check(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch),
          "native six-surface prepare");
    Settings s{};
    s.moving=false;s.startAzimuthDegrees=az;s.stereoSpreadDegrees=60.f;
    s.spatialMix=1.f;s.outputTrim=.25f;
    s.spacePreset=room?1:0;s.sourceDistanceMeters=1.6f;
    s.roomAmount=room?.65f:0.f;s.ambience=room?.3f:0.f;
    s.directElevationDegrees=0.f;
    check(core->setSettings(s),"native settings admission");
    check(core->setResearchRenderMode(mode),"quiescent render mode admission");
    check(core->setResearchHeadYaw(yaw),"quiescent world/head transform admission");
    check(core->setResearchEvidence(stem,mask),"quiescent stem admission");
    const int n=room?24576:8192;
    Render out{std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i){
        const float t=static_cast<float>(i/rate);
        // Deterministic musical diagnostic: LF + speech-range fundamentals
        // + pinna-sensitive upper partial. Not copyrighted source material.
        const float tone=.1f*std::sin(2*pi*83*t)+
                         .07f*std::sin(2*pi*461*t)+
                         .045f*std::sin(2*pi*1703*t)+
                         .025f*std::sin(2*pi*5387*t);
        if(input==Input::ImpulseLeft)out.l[i]=i==0?.2f:0.f;
        if(input==Input::ImpulseRight)out.r[i]=i==0?.2f:0.f;
        if(input==Input::HardLeft)out.l[i]=tone;
        if(input==Input::HardRight)out.r[i]=tone;
        if(input==Input::MonoMusic)out.l[i]=out.r[i]=tone;
        if(input==Input::AntiPhase){out.l[i]=tone;out.r[i]=-tone;}
        if(input==Input::Partial){
            out.l[i]=tone;out.r[i]=.72f*tone+.045f*std::sin(2*pi*2891*t);
        }
    }
    for(int i=0;i<n;i+=chunk)
        core->process(out.l.data()+i,out.r.data()+i,std::min(n-i,chunk));
    for(int i=0;i<n;++i)
        check(std::isfinite(out.l[i])&&std::isfinite(out.r[i])&&
              std::abs(out.l[i])<=1.f&&std::abs(out.r[i])<=1.f,
              "finite bounded native PCM");
    check(!core->setResearchHeadYaw(30.f),"reject post-PCM head mutation");
    check(!core->setResearchRenderMode(p83::Mode::MusicBedP82),
          "reject post-PCM mode mutation");
    return out;
}
static double energy(const std::vector<float>& a){
    double e=0;for(float x:a)e+=static_cast<double>(x)*x;return e;
}
static double total(const Render& a){return energy(a.l)+energy(a.r);}
static double ild(const Render& a) {
    return 10.*std::log10((energy(a.r)+1e-30)/(energy(a.l)+1e-30));
}
static double difference(const Render& a,const Render& b){
    check(a.l.size()==b.l.size(),"PCM size equality");
    double delta=0;
    for(std::size_t i=0;i<a.l.size();++i) {
        const double dl=a.l[i]-b.l[i],dr=a.r[i]-b.r[i];
        delta+=dl*dl+dr*dr;
    }
    return std::sqrt(delta);
}
static bool exact(const Render& a,const Render& b){
    return a.l.size()==b.l.size() &&
        std::memcmp(a.l.data(),b.l.data(),a.l.size()*sizeof(float))==0 &&
        std::memcmp(a.r.data(),b.r.data(),a.r.size()*sizeof(float))==0;
}
static int onset(const Render& a,float threshold=1.e-6f) {
    for(std::size_t i=0;i<a.l.size();++i)
        if(std::abs(a.l[i])+std::abs(a.r[i])>threshold)
            return static_cast<int>(i);
    return -1;
}
static double iacc(const Render& a){
    const double el=energy(a.l),er=energy(a.r);
    if(el<1e-20||er<1e-20)return 0;
    double best=-1;
    for(int lag=-40;lag<=40;++lag){
        double v=0.;
        for(int i=40;i<static_cast<int>(a.l.size())-40;++i)
            v+=static_cast<double>(a.l[i])*a.r[i+lag];
        best=std::max(best,std::abs(v)/std::sqrt(el*er));
    }
    return std::min(1.,best);
}
static void record(const char* label,double rate,const Render& a) {
    std::printf("P83_PCM,%s,%.0f,frames=%zu,energy=%.9f,ILD_right_dB=%.3f,IACCabs=%.4f,onset=%d\n",
                label,rate,a.l.size(),total(a),ild(a),iacc(a),onset(a));
}
static void writeWav(const std::filesystem::path& path,const Render& a,int rate){
    // IEEE 32-bit little-endian stereo WAV. Disk I/O in OFFLINE test only.
    std::ofstream f(path,std::ios::binary);
    check(bool(f),"open diagnostic WAV");
    const auto u16=[&](std::uint16_t v){
        for(int j=0;j<2;++j)f.put(static_cast<char>(v>>(8*j)));
    };
    const auto u32=[&](std::uint32_t v){
        for(int j=0;j<4;++j)f.put(static_cast<char>(v>>(8*j)));
    };
    const std::uint32_t bytes=static_cast<std::uint32_t>(a.l.size()*8);
    f.write("RIFF",4);u32(36+bytes);f.write("WAVE",4);
    f.write("fmt ",4);u32(16);u16(3);u16(2);u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate*8));u16(8);u16(32);
    f.write("data",4);u32(bytes);
    for(std::size_t i=0;i<a.l.size();++i)
        for(float v:{a.l[i],a.r[i]}){
            std::uint32_t bits=0;std::memcpy(&bits,&v,sizeof(bits));u32(bits);
        }
    check(bool(f),"write diagnostic WAV");
}
int main(int argc,char** argv){
    check(std::abs(p83::toHead(0.f,90.f)+90.f)<1.e-5f,
          "world FRONT to head LEFT yaw +90");
    check(std::abs(p83::toHead(-179.f,181.f))<1.e-5f,
          "world->head wrap");
    check(!p83::validYaw(400.f),"reject invalid yaw");
    const auto positions=p83::stereo(0.f,60.f,90.f);
    check(std::abs(positions.leftHead+120.f)<1.e-5f &&
          std::abs(positions.rightHead+60.f)<1.e-5f,"two world-fixed speakers");
    std::filesystem::path folder;
    if(argc==3 && std::string(argv[1])=="--wavdir"){
        folder=argv[2];std::filesystem::create_directories(folder);
    }else check(argc==1,"usage: [--wavdir DIR]");
    for(double rate:{44100.,48000.}) {
        const auto renderAt=[&](p83::Mode m,float az,float yaw,bool room,
                  p83::Stem stem,std::uint8_t mask,Input input,int chunk=128){
            return render(rate,m,az,yaw,room,stem,mask,input,chunk);
        };
        const auto before=renderAt(p83::Mode::MusicBedP82,0,0,false,
                   p83::Stem::Combined,0x3f,Input::AntiPhase);
        const auto beforeChunk=render(rate,p83::Mode::MusicBedP82,0,0,
                   false,p83::Stem::Combined,0x3f,Input::AntiPhase,256);
        check(exact(before,beforeChunk),"bit-identical legacy P8.2 chunk parity");
        record("P82_MUSIC_BED_ANTIPHASE",rate,before);
        const std::array<float,8> azimuths{0,30,90,150,180,-150,-90,-30};
        std::array<Render,8> objects{};
        for(std::size_t d=0;d<azimuths.size();++d){
            objects[d]=renderAt(p83::Mode::MonoObject,azimuths[d],0,false,
                      p83::Stem::Combined,0x3f,Input::ImpulseLeft);
            check(total(objects[d])>1e-7,"non-silent MonoObject impulse");
            record((std::string("MONO_")+std::to_string(static_cast<int>(azimuths[d]))).c_str(),
                   rate,objects[d]);
        }
        check(ild(objects[2])>1.,"RIGHT positive ILD");
        check(ild(objects[6])< -1.,"LEFT negative ILD");
        check(difference(objects[0],objects[4])>0.001,"front/rear measured spectral distinction");
        check(difference(objects[2],objects[6])>0.001,"L/R interaural distinction");
        const auto wrap=renderAt(p83::Mode::MonoObject,-180,0,false,
                    p83::Stem::Combined,0x3f,Input::ImpulseLeft);
        check(exact(objects[4],wrap),"0/360 and +/-180 wrap");
        const auto turned=renderAt(p83::Mode::MonoObject,0,90,false,
                    p83::Stem::Combined,0x3f,Input::ImpulseLeft);
        check(exact(turned,objects[6]),"world-fixed direct yaw equivalence");
        const auto ignored=renderAt(p83::Mode::MonoObject,0,0,false,
                    p83::Stem::Combined,0x3f,Input::ImpulseRight);
        check(total(ignored)<1e-20,"MonoObject explicitly takes LEFT only");
        const auto speakerL=renderAt(p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::HardLeft);
        const auto speakerR=renderAt(p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::HardRight);
        const auto speakerAnti=renderAt(p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::AntiPhase);
        const auto speakerMono=renderAt(p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::MonoMusic);
        const auto speakerPartial=renderAt(p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::Partial);
        check(ild(speakerL)< -0.5 && ild(speakerR)>0.5,
              "LEFT/RIGHT input must use independent ear transfer");
        check(total(speakerAnti)>1e-5,"antiphase cannot collapse to silence");
        check(total(speakerMono)>1e-5 && total(speakerPartial)>1e-5,
              "correlated and partially coherent music survives");
        check(difference(speakerL,speakerR)>0.1,"speaker sources not interchangeable");
        record("SPEAKER_L_ONLY",rate,speakerL);
        record("SPEAKER_R_ONLY",rate,speakerR);
        record("SPEAKER_ANTIPHASE",rate,speakerAnti);
        record("SPEAKER_MONO",rate,speakerMono);
        record("SPEAKER_PARTIAL",rate,speakerPartial);
        const auto small=render(rate,p83::Mode::SpeakerMonitor,0,0,false,
                    p83::Stem::Combined,0x3f,Input::AntiPhase,64);
        check(exact(speakerAnti,small),"stereo speakers block-size parity 64/128");
        // Isolate six-surface room with same physical MONO source and
        // same material/FDN coefficients and first-order image geometry.
        const auto direct=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Direct,0x3f,Input::ImpulseLeft);
        const auto horizontal=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Early,0x0f,Input::ImpulseLeft);
        const auto floor=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Early,0x10,Input::ImpulseLeft);
        const auto ceiling=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Early,0x20,Input::ImpulseLeft);
        const auto late=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Late,0x3f,Input::ImpulseLeft);
        const auto combined=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Combined,0x3f,Input::ImpulseLeft);
        const auto muted=renderAt(p83::Mode::MonoObject,0,0,true,
                    p83::Stem::Early,0x00,Input::ImpulseLeft);
        check(total(direct)>1e-8 && total(horizontal)>1e-10 &&
              total(floor)>1e-12 && total(ceiling)>1e-12 &&
              total(late)>1e-12 && total(combined)>1e-8,
              "all native source/room stems produce measurable energy");
        check(total(muted)<1e-20,"no hidden ER when wall mask zero");
        check(onset(horizontal)>onset(direct) && onset(floor)>onset(direct) &&
              onset(ceiling)>onset(direct) && onset(late)>onset(direct),
              "causal source/reflection/reverb arrival order");
        check(difference(floor,ceiling)>1e-5,"vertical reflection cues distinct");
        record("DIRECT_ONLY",rate,direct);
        record("HORIZONTAL_ER_ONLY",rate,horizontal);
        record("FLOOR_ONLY",rate,floor);
        record("CEILING_ONLY",rate,ceiling);
        record("LATE_ONLY",rate,late);
        record("COMBINED",rate,combined);
        std::printf("P83_ROOM,%.0f,DRR_direct_vs_early_dB=%.3f,"
                    "DRR_direct_vs_late_dB=%.3f,ceiling_vs_floor_dB=%.3f\n",
              rate,10*std::log10(total(direct)/(total(horizontal)+total(floor)+total(ceiling))),
              10*std::log10(total(direct)/total(late)),
              10*std::log10(total(ceiling)/total(floor)));
        if(!folder.empty()&&rate==48000.){
            writeWav(folder/"P82_music_antiphase_48k.wav",before,48000);
            writeWav(folder/"P83_mono_front_direct_48k.wav",objects[0],48000);
            writeWav(folder/"P83_mono_right_direct_48k.wav",objects[2],48000);
            writeWav(folder/"P83_mono_rear_direct_48k.wav",objects[4],48000);
            writeWav(folder/"P83_mono_left_direct_48k.wav",objects[6],48000);
            writeWav(folder/"P83_speaker_monophonic_stereo_48k.wav",speakerMono,48000);
            writeWav(folder/"P83_speaker_antiphase_48k.wav",speakerAnti,48000);
            writeWav(folder/"P83_direct_only_48k.wav",direct,48000);
            writeWav(folder/"P83_horizontal_only_48k.wav",horizontal,48000);
            writeWav(folder/"P83_floor_only_48k.wav",floor,48000);
            writeWav(folder/"P83_ceiling_only_48k.wav",ceiling,48000);
            writeWav(folder/"P83_late_only_48k.wav",late,48000);
            writeWav(folder/"P83_combined_48k.wav",combined,48000);
        }
    }
    std::puts("PASS P8.3 SOURCE GATES: physical mono/stereo 2x2, yaw, six stems, P8.2 preservation, direction and phase; perceptual externalization NOT proven");
}
