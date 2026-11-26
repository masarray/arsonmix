// SPDX-License-Identifier: AGPL-3.0-only
// Offline audition only. Uses the shipping core and factory bank, no JUCE/device.
// Input: optional mono/stereo integer PCM16 RIFF WAV, 8..192 kHz, <=60 seconds.
#include "../../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <cmath>
using namespace armix::builtin_fx::vocal_delay;
namespace fs=std::filesystem;
struct Audio {unsigned rate=48000;std::vector<float> l,r;};
static std::uint32_t get(const std::vector<unsigned char>& v,std::size_t p,unsigned n){
    if(p>v.size()||n>v.size()-p)throw std::runtime_error("truncated WAV");
    std::uint32_t x=0;for(unsigned i=0;i<n;++i)x|=std::uint32_t(v[p+i])<<(8*i);return x;
}
static bool tag(const std::vector<unsigned char>& v,std::size_t p,const char* s){
    return p<=v.size()&&v.size()-p>=4&&std::equal(s,s+4,v.begin()+p);
}
static Audio readWave(const fs::path& path){
    const auto size=fs::file_size(path);
    if(size<44||size>48'000'000)throw std::runtime_error("unsupported WAV size");
    std::ifstream in(path,std::ios::binary);
    std::vector<unsigned char> v(static_cast<std::size_t>(size));
    if(!in.read(reinterpret_cast<char*>(v.data()),std::streamsize(v.size())))throw std::runtime_error("cannot read input");
    if(!tag(v,0,"RIFF")||!tag(v,8,"WAVE")||get(v,4,4)>v.size()-8)throw std::runtime_error("expected RIFF WAVE");
    const std::size_t end=std::size_t(get(v,4,4))+8;
    unsigned rate=0,channels=0;std::size_t data=0,bytes=0;
    for(std::size_t p=12;p+8<=end;){
        const auto n=std::size_t(get(v,p+4,4));const auto start=p+8;
        if(n>end-start)throw std::runtime_error("invalid chunk");
        if(tag(v,p,"fmt ")){
            if(n<16||get(v,start,2)!=1||get(v,start+14,2)!=16)throw std::runtime_error("export integer PCM16 WAV");
            channels=get(v,start+2,2);rate=get(v,start+4,4);
            if((channels!=1&&channels!=2)||!validRate(rate)||get(v,start+12,2)!=channels*2)
                throw std::runtime_error("unsupported channel count/rate");
        }else if(tag(v,p,"data")){data=start;bytes=n;}
        p=start+n+(n&1u);
    }
    if(!rate||!channels||!data||!bytes||bytes%(channels*2))throw std::runtime_error("missing/invalid PCM");
    const auto frames=bytes/(channels*2);
    if(frames>std::size_t(rate)*60)throw std::runtime_error("maximum input duration 60 seconds");
    Audio a;a.rate=rate;a.l.resize(frames);a.r.resize(frames);
    const auto sample=[&](std::size_t p){const auto u=get(v,p,2);return float(u>=32768?int(u)-65536:int(u))/32768.f;};
    for(std::size_t i=0;i<frames;++i){a.l[i]=sample(data+i*channels*2);a.r[i]=channels==2?sample(data+i*4+2):a.l[i];}
    return a;
}
static Audio fixture(){
    Audio a;const auto n=std::size_t(a.rate)*6;a.l.resize(n);a.r.resize(n);
    // Synthetic harmonic excitation, NOT a human singer or genre simulation.
    // Loud -> quiet re-entry, quick syllables, short breaths, long ending.
    struct Phrase{double start,end,amp,hz;};
    constexpr Phrase phrases[]={{.2,1.1,.55,190},{1.6,2.3,.035,230},{2.37,2.60,.18,245},
        {2.65,2.87,.20,220},{2.92,3.15,.16,196},{3.45,4.85,.32,174}};
    constexpr double pi=3.141592653589793;
    for(std::size_t i=0;i<n;++i){
        const double time=double(i)/a.rate;
        double v=0;for(const auto& p:phrases)if(time>=p.start&&time<p.end){
            const double dt=time-p.start;
            const double envelope=std::min(1.,std::min(dt,p.end-time)/.025);
            const double phase=2*pi*p.hz*dt+.15*std::sin(2*pi*5.1*dt);
            v=p.amp*envelope*(.73*std::sin(phase)+.20*std::sin(2*phase)+.07*std::sin(3*phase));
        }
        a.l[i]=a.r[i]=float(v);
    }
    return a;
}
static Audio concertFixture(){
    Audio a;const auto n=std::size_t(a.rate)*6;a.l.resize(n);a.r.resize(n);
    for(std::size_t i=0;i<n;++i){
        const double t=double(i)/a.rate;float amp=0;
        if(t<1.2&&std::fmod(t,.24)<.18)amp=.07f;
        if(t>=1.65&&t<2.10)amp=.075f;
        if(t>=2.10&&t<2.55)amp=.34f;
        if(t>=4.2&&t<4.8)amp=.06f;
        a.l[i]=a.r[i]=amp*float(std::sin(6.283185307179586*190*t));
    }
    a.l[std::size_t(a.rate*1.4)]=a.r[std::size_t(a.rate*1.4)]=.8f;
    return a;
}
static void put(std::ofstream& out,std::uint32_t v,unsigned bytes){
    for(unsigned i=0;i<bytes;++i)out.put(char((v>>(8*i))&255u));
}
static void writeWave(const fs::path& path,const Audio& a){
    std::ofstream out(path,std::ios::binary);
    const auto bytes=std::uint32_t(a.l.size()*4);
    out.write("RIFF",4);put(out,36+bytes,4);out.write("WAVEfmt ",8);put(out,16,4);
    put(out,1,2);put(out,2,2);put(out,a.rate,4);put(out,a.rate*4,4);put(out,4,2);put(out,16,2);
    out.write("data",4);put(out,bytes,4);
    const auto write=[&](float x){const auto q=std::lround(std::clamp(x,-1.f,32767.f/32768.f)*32768.f);
        put(out,std::uint16_t(q),2);};
    for(std::size_t i=0;i<a.l.size();++i){write(a.l[i]);write(a.r[i]);}
    if(!out)throw std::runtime_error("cannot write output WAV");
}
static void render(const fs::path& dir,const Audio& dry,const std::string& name,const Parameters& p,std::ofstream& csv,bool synthetic){
    Audio a=dry;ContinuousDelayCore core;if(!core.prepare(a.rate,p))throw std::runtime_error("prepare failed");
    for(std::size_t i=0;i<a.l.size();){const auto n=std::min<std::size_t>(128,a.l.size()-i);
        core.processStereo(a.l.data()+i,a.r.data()+i,n);i+=n;}
    double wet=0,side=0,quiet=0,tail=0;std::size_t quietN=0,tailN=0;float peak=0;std::size_t clips=0;
    for(std::size_t i=0;i<a.l.size();++i){
        if(!std::isfinite(a.l[i])||!std::isfinite(a.r[i]))throw std::runtime_error("nonfinite render");
        const float l=a.l[i]-dry.l[i],r=a.r[i]-dry.r[i];
        const double energy=double(l)*l+double(r)*r;
        wet+=energy;side+=double(l-r)*(l-r);
        if(synthetic){
            if(i>=std::size_t(a.rate*1.7)&&i<std::size_t(a.rate*2.3)){quiet+=energy;++quietN;}
            if(i>=std::size_t(a.rate*4.95)&&i<std::size_t(a.rate*5.65)){tail+=energy;++tailN;}
        }
        peak=std::max({peak,std::abs(a.l[i]),std::abs(a.r[i])});
        if(std::abs(a.l[i])>1.000001f||std::abs(a.r[i])>1.000001f)++clips;
    }
    csv<<name<<','<<peak<<','<<std::sqrt(wet/(2*a.l.size()))<<','<<std::sqrt(side/(4*a.l.size()))<<','<<clips<<',';
    if(synthetic)csv<<std::sqrt(quiet/(2*quietN))<<','<<std::sqrt(tail/(2*tailN));
    else csv<<',';
    csv<<'\n';
    writeWave(dir/(name+".wav"),a);
}
int main(int argc,char** argv){
    try{
        if(argc!=2&&argc!=3)throw std::runtime_error("usage: vd-audition OUTPUT_DIR [dry-voice-pcm16.wav]");
        const fs::path dir=argv[1];fs::create_directories(dir);
        const bool concert=argc==3&&std::string(argv[2])=="--concert";
        Audio dry=concert?concertFixture():argc==3?readWave(argv[2]):fixture();
        dry.l.resize(dry.l.size()+std::size_t(dry.rate)*3,0.f);dry.r.resize(dry.l.size(),0.f);
        writeWave(dir/"00 Dry reference.wav",dry);
        std::ofstream csv(dir/"metrics.csv");csv<<std::fixed<<std::setprecision(7)<<"preset,output_peak,wet_rms,wet_side_rms,over_full_scale_frames,fixture_quiet_wet_rms,fixture_tail_wet_rms\n";
        render(dir,dry,"01 Classic Live Vocal",presetSettings(stagePresetCount),csv,argc==2);
        for(std::size_t i=0;i<stagePresetCount;++i){
            const auto prefix=std::to_string(i+2);
            render(dir,dry,(prefix.size()==1?"0":"")+prefix+" "+singerPresets[i].name,presetSettings(i),csv,argc==2);
        }
        for(std::size_t i=0;i<concertPresetCount;++i)
            render(dir,dry,std::to_string(i+14)+" "+singerPresets[concertPresetStart+i].name,presetSettings(concertPresetStart+i),csv,argc==2);
        if(!csv)throw std::runtime_error("cannot write metrics");
        std::ofstream notes(dir/"README.txt");
        notes<<(concert?"Synthetic concert climax fixture, NOT a human singer.":argc==3?"User supplied PCM16 dry voice.":"Synthetic harmonic excitation, NOT a human singer.")
             <<"\nSame input, sample rate and dry gain; no output normalization. Stereo PCM16 export, three seconds of tail. Metrics measured in float before PCM16 quantization; over-full-scale samples may clip at export. Fixture-only quiet/tail columns measure 1.7–2.3 s and 4.95–5.65 s; blank for supplied audio. No auto tempo: free-time fallback. Compare at safe monitoring volume.\n";
        if(!notes)throw std::runtime_error("cannot write notes");
        std::cout<<"VD9_AUDITION PASS: dry + classic + "<<stagePresetCount<<" stage + "<<concertPresetCount<<" concert renders, rate="<<dry.rate<<"\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
