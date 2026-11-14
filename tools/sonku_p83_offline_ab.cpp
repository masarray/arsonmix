// SPDX-License-Identifier: AGPL-3.0-only
// Exact SonKuSpatialCore research audition; OFFLINE ONLY, no JUCE device,
// ASIO, second mixer graph, WebAudio or proprietary music/HRIR content.
// Stereo RIFF WAV PCM16/24/32, float32, and DAW WAVEFORMATEXTENSIBLE, 44.1/48k.
// Each output uses the SAME source input, trim, position and room settings.
// Raw render levels are preserved: use independent loudness matching in FAT.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs=std::filesystem;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::Settings;
namespace p83=armix::sonku::p83;

struct WaveInfo {
    int rate=0,format=0,bits=0,align=0;
    std::uint64_t offset=0,frames=0;
};
static std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]|(std::uint16_t(p[1])<<8));
}
static std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
           (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
static void putLe(std::ostream& o,std::uint32_t value,int bytes) {
    for(int i=0;i<bytes;++i)o.put(static_cast<char>(value>>(8*i)));
}
static WaveInfo inspect(std::ifstream& f) {
    f.seekg(0,std::ios::end);
    const auto end=f.tellg();
    if(end<44)throw std::runtime_error("Input too small for RIFF WAV");
    const auto size=static_cast<std::uint64_t>(end);
    f.seekg(0,std::ios::beg);
    std::array<char,12> riff{};
    f.read(riff.data(),riff.size());
    if(!f||std::memcmp(riff.data(),"RIFF",4)!=0 ||
       std::memcmp(riff.data()+8,"WAVE",4)!=0)
        throw std::runtime_error("Requires uncompressed RIFF/WAVE, not W64/RF64");
    WaveInfo w{};
    std::uint64_t dataBytes=0;
    bool hasFmt=false,hasData=false;
    for(std::uint64_t at=12;at+8<=size;){
        f.seekg(static_cast<std::streamoff>(at),std::ios::beg);
        std::array<std::uint8_t,8> head{};
        f.read(reinterpret_cast<char*>(head.data()),head.size());
        if(!f)throw std::runtime_error("Truncated WAV chunk header");
        const auto length=static_cast<std::uint64_t>(le32(head.data()+4));
        const auto body=at+8;
        if(length>size-body)throw std::runtime_error("Truncated WAV chunk");
        if(std::memcmp(head.data(),"fmt ",4)==0) {
            if(hasFmt||length<16)throw std::runtime_error("Invalid fmt chunk");
            std::array<std::uint8_t,40> fmt{};
            const auto count=static_cast<std::streamsize>(length>=40?40:16);
            f.read(reinterpret_cast<char*>(fmt.data()),count);
            if(!f)throw std::runtime_error("Truncated PCM format");
            w.format=le16(fmt.data());
            const int channels=le16(fmt.data()+2);
            w.rate=static_cast<int>(le32(fmt.data()+4));
            w.align=le16(fmt.data()+12);
            w.bits=le16(fmt.data()+14);
            if(w.format==0xfffe) {
                // Microsoft KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT.
                // Reject unknown vendor GUID rather than guess sample data.
                if(length<40 || le16(fmt.data()+16)<22)
                    throw std::runtime_error("Malformed WAVEFORMATEXTENSIBLE");
                constexpr std::array<std::uint8_t,12> guidTail{{
                    0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
                if(!std::equal(guidTail.begin(),guidTail.end(),fmt.data()+28))
                    throw std::runtime_error("Unsupported extensible WAV GUID");
                const auto sub=le32(fmt.data()+24);
                const auto valid=le16(fmt.data()+18);
                if((sub!=1&&sub!=3) || (valid!=0&&valid!=w.bits))
                    throw std::runtime_error("Unsupported WAV subtype/validBits");
                w.format=static_cast<int>(sub);
            }
            if(channels!=2 || (w.rate!=44100&&w.rate!=48000) ||
               (w.format!=1&&w.format!=3) ||
               !(w.format==1?(w.bits==16||w.bits==24||w.bits==32):
                                  (w.bits==32)) ||
               w.align!=2*(w.bits/8))
                throw std::runtime_error("Supports stereo PCM16/24/32 or float32 at 44.1/48k");
            hasFmt=true;
        }else if(std::memcmp(head.data(),"data",4)==0) {
            if(hasData)throw std::runtime_error("Multiple WAV data chunks");
            w.offset=body;
            dataBytes=length;
            hasData=true;
        }
        at=body+length+(length&1u);
    }
    if(!hasFmt||!hasData||dataBytes==0 ||
       dataBytes%static_cast<std::uint64_t>(w.align)!=0)
        throw std::runtime_error("Missing/misaligned PCM data");
    w.frames=dataBytes/static_cast<std::uint64_t>(w.align);
    if(w.frames>(0xFFFFFFFFull-48ull)/8ull)
        throw std::runtime_error("Output exceeds standard 4GB RIFF limit");
    return w;
}
static float decode(const std::uint8_t* p,int format,int bits){
    if(format==3){
        const auto raw=le32(p);
        float x=0.f;
        std::memcpy(&x,&raw,sizeof(x));
        if(!std::isfinite(x)||std::abs(x)>1.f)
            throw std::runtime_error("Nonfinite/clipped input float32 WAV");
        return x;
    }
    if(bits==16) {
        const auto raw=static_cast<int>(le16(p));
        return static_cast<float>(raw>=32768?raw-65536:raw)/32768.f;
    }
    if(bits==24) {
        const auto raw=static_cast<int>(p[0]|(int(p[1])<<8)|(int(p[2])<<16));
        return static_cast<float>(raw>=0x800000?raw-0x1000000:raw)/8388608.f;
    }
    const auto raw=le32(p);
    const auto sample=raw>=0x80000000u
        ?static_cast<std::int64_t>(raw)-0x100000000ll
        :static_cast<std::int64_t>(raw);
    return static_cast<float>(static_cast<double>(sample)/2147483648.);
}
static void beginFloatWav(std::ofstream& f,int rate,std::uint64_t frames){
    const auto n=static_cast<std::uint32_t>(frames*8);
    f.write("RIFF",4);putLe(f,48+n,4);f.write("WAVE",4);
    f.write("fmt ",4);putLe(f,16,4);putLe(f,3,2);putLe(f,2,2);
    putLe(f,static_cast<std::uint32_t>(rate),4);
    putLe(f,static_cast<std::uint32_t>(rate*8),4);
    putLe(f,8,2);putLe(f,32,2);
    // IEEE_FLOAT WAV should have a fact sample-count chunk.
    f.write("fact",4);putLe(f,4,4);putLe(f,static_cast<std::uint32_t>(frames),4);
    f.write("data",4);putLe(f,n,4);
}
static void pushFloat(std::uint8_t* p,float x) {
    if(!std::isfinite(x))throw std::runtime_error("Nonfinite native SonKu output");
    std::uint32_t bits{};
    std::memcpy(&bits,&x,sizeof(bits));
    for(int i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(bits>>(8*i));
}
struct Result {double rms=0.,peak=0.;};
static Result render(const fs::path& input,const WaveInfo& w,
                     const fs::path& output,p83::Mode mode,
                     bool musicLf,bool withRoom){
    auto core=std::make_unique<SonKuSpatialCore>();
    if(!core->prepare(w.rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch))
        throw std::runtime_error("Native SonKu prepare failed");
    Settings s{};
    s.moving=false;s.startAzimuthDegrees=0.f;
    s.stereoSpreadDegrees=60.f;
    s.spatialMix=1.f;s.outputTrim=.75f;
    s.spacePreset=withRoom?1:0;s.sourceDistanceMeters=1.6f;
    s.roomAmount=.55f;s.ambience=.30f;s.directElevationDegrees=0.f;
    if(!core->setSettings(s)||!core->setResearchRenderMode(mode)||
       !core->setResearchSpeakerMusicLowFrequency(musicLf))
        throw std::runtime_error("Native research scene admission failed");
    std::ifstream f(input,std::ios::binary);
    std::ofstream o(output,std::ios::binary);
    if(!f||!o)throw std::runtime_error("Cannot open input/output WAV");
    beginFloatWav(o,w.rate,w.frames);
    constexpr std::uint64_t block=128;
    std::array<float,block> l{},r{};
    std::array<std::uint8_t,block*8> encoded{};
    std::array<std::uint8_t,block*8> decoded{};
    double sum=0.,peak=0.;
    for(std::uint64_t frame=0;frame<w.frames;frame+=block) {
        const auto n=static_cast<int>(std::min(block,w.frames-frame));
        const auto bytes=static_cast<std::streamsize>(n*w.align);
        f.seekg(static_cast<std::streamoff>(
            w.offset+frame*static_cast<std::uint64_t>(w.align)),std::ios::beg);
        f.read(reinterpret_cast<char*>(encoded.data()),bytes);
        if(!f||f.gcount()!=bytes)throw std::runtime_error("Short WAV read");
        for(int i=0;i<n;++i) {
            const auto* src=encoded.data()+i*w.align;
            l[i]=decode(src,w.format,w.bits);
            r[i]=decode(src+w.bits/8,w.format,w.bits);
        }
        core->process(l.data(),r.data(),n);
        for(int i=0;i<n;++i) {
            const auto a=static_cast<double>(l[i]),b=static_cast<double>(r[i]);
            sum+=a*a+b*b;
            peak=std::max(peak,std::max(std::abs(a),std::abs(b)));
            pushFloat(decoded.data()+i*8,l[i]);
            pushFloat(decoded.data()+i*8+4,r[i]);
        }
        o.write(reinterpret_cast<const char*>(decoded.data()),
                static_cast<std::streamsize>(n*8));
        if(!o)throw std::runtime_error("Short WAV write");
    }
    return {std::sqrt(sum/(2.*static_cast<double>(w.frames))),peak};
}
int main(int argc,char** argv){
    try {
        if(argc<3 || argc>4)
            throw std::runtime_error("Usage: sonku-p83-ab input_stereo.wav output_directory [--room=off|--room=on]");
        const auto input=fs::path(argv[1]);
        const auto output=fs::path(argv[2]);
        bool room=false;
        if(argc==4) {
            const std::string option(argv[3]);
            if(option=="--room=on")room=true;
            else if(option!="--room=off")throw std::runtime_error("Unknown room option");
        }
        std::ifstream stream(input,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot open input WAV");
        const auto w=inspect(stream);
        fs::create_directories(output);
        std::ofstream csv(output/"sonku_p83_ab_metrics.csv");
        if(!csv)throw std::runtime_error("Cannot open A/B metrics");
        csv<<"variant,rate_hz,frames,room,cutoff_hz,output_rms,output_peak\n";
        struct Variant {const char* name;p83::Mode mode;bool safe;};
        constexpr std::array<Variant,3> options{{
            {"P82_MusicBed",p83::Mode::MusicBedP82,false},
            {"P83_SpeakerPhysical",p83::Mode::SpeakerMonitor,false},
            {"P83_SpeakerMusicLF480",p83::Mode::SpeakerMonitor,true}
        }};
        std::cout<<std::fixed<<std::setprecision(6);
        for(const auto& variant:options){
            const auto file=output/(std::string(variant.name)+".wav");
            const auto metrics=render(input,w,file,variant.mode,variant.safe,room);
            csv<<variant.name<<','<<w.rate<<','<<w.frames<<','
               <<(room?"on":"off")<<','<<(variant.safe?480:0)<<','
               <<metrics.rms<<','<<metrics.peak<<'\n';
            std::cout<<variant.name<<" RMS="<<metrics.rms
                     <<" peak="<<metrics.peak<<" -> "<<file.string()<<'\n';
        }
        std::cout<<"PASS SonKu P8.3 offline A/B: 3 identical-input native PCM variants; "
                 <<"raw levels NOT loudness-matched; headphone externalization NOT proven\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"SonKu P8.3 A/B error: "<<e.what()<<'\n';
        return 1;
    }
}
