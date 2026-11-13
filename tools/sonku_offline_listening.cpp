// SPDX-License-Identifier: AGPL-3.0-only
// Deterministic offline headphone evidence from the EXACT shipped native
// 8D-SonKu measured HRTF core. No external music, WebAudio or second engine.
// Generates lossless stereo WAVs for listening and blind front/rear evaluation.
// Audio is NONPERSONALIZED MIT KEMAR, so perceptual externalization is UNPROVEN.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t rate=48000;
constexpr int framesPerBlock=128;
constexpr int activeFrames=rate*2;
constexpr int silenceFrames=rate/2;
constexpr int rampFrames=rate/50; // 20ms, no clicks between labeled probes
using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
struct Noise {
    std::uint32_t x=0x9a762b41u;
    float low=0.f,body=0.f;
    float sample() noexcept {
        x^=x<<13;x^=x>>17;x^=x<<5;
        const float white=(static_cast<float>(x&0xffffffu)/8388608.f)-1.f;
        low+=.085f*(white-low);
        body+=.013f*(white-body);
        return .16f*(.44f*white+.37f*low+.19f*body);
    }
};
void appendSample(std::vector<std::uint8_t>& bytes,float x){
    if(!std::isfinite(x))throw std::runtime_error("Nonfinite SonKu PCM");
    if(std::abs(x)>.95f)throw std::runtime_error("SonKu clipping: reduce offline source");
    const auto sample=static_cast<std::int16_t>(std::lround(x*32767.f));
    const auto bits=static_cast<std::uint16_t>(sample);
    bytes.push_back(static_cast<std::uint8_t>(bits&255u));
    bytes.push_back(static_cast<std::uint8_t>((bits>>8)&255u));
}
void appendStereo(std::vector<std::uint8_t>& bytes,float l,float r){
    appendSample(bytes,l);appendSample(bytes,r);
}
void le(std::ostream& out,std::uint32_t x,int width){
    for(int i=0;i<width;++i)out.put(static_cast<char>((x>>(8*i))&255u));
}
void writeWav(const std::filesystem::path& path,const std::vector<std::uint8_t>& pcm){
    if(pcm.size()>0xFFFFFF00u||pcm.size()%4)throw std::runtime_error("Invalid PCM length");
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Cannot open "+path.string());
    out.write("RIFF",4);le(out,static_cast<std::uint32_t>(pcm.size()+36),4);
    out.write("WAVEfmt ",8);le(out,16,4);le(out,1,2);le(out,2,2);
    le(out,rate,4);le(out,rate*4,4);le(out,4,2);le(out,16,2);
    out.write("data",4);le(out,static_cast<std::uint32_t>(pcm.size()),4);
    out.write(reinterpret_cast<const char*>(pcm.data()),
              static_cast<std::streamsize>(pcm.size()));
    if(!out)throw std::runtime_error("Failed writing "+path.string());
}
Settings fixed(float azimuth){
    Settings s{};
    s.moving=false;s.startAzimuthDegrees=azimuth;
    s.stereoSpreadDegrees=0.f;s.spatialMix=1.f;s.outputTrim=.72f;
    return s;
}
std::vector<std::uint8_t> staticProbes(const std::vector<int>& azimuths) {
    SonKuSpatialCore dsp;Noise noise;
    if(!dsp.prepare(rate))throw std::runtime_error("No 48k HRTF");
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(azimuths.size())*
        (activeFrames+silenceFrames)*4);
    for(const int azimuth:azimuths){
        dsp.reset();if(!dsp.setSettings(fixed(static_cast<float>(azimuth))))
            throw std::runtime_error("Invalid static direction");
        for(int base=0;base<activeFrames+silenceFrames;base+=framesPerBlock){
            const int n=std::min(framesPerBlock,activeFrames+silenceFrames-base);
            std::array<float,framesPerBlock> l{},r{};
            for(int i=0;i<n;++i){
                const int pos=base+i;
                float v=0.f;
                if(pos<activeFrames){
                    const float gain=std::clamp(
                        std::min(pos,activeFrames-1-pos)/static_cast<float>(rampFrames),
                        0.f,1.f);
                    v=noise.sample()*gain;
                }
                l[static_cast<std::size_t>(i)]=v;
                r[static_cast<std::size_t>(i)]=v;
            }
            dsp.process(l.data(),r.data(),n);
            for(int i=0;i<n;++i)appendStereo(result,l[i],r[i]);
        }
    }
    return result;
}
std::vector<std::uint8_t> orbit(){
    SonKuSpatialCore dsp;Noise noise;
    if(!dsp.prepare(rate))throw std::runtime_error("No 48k HRTF");
    Settings s=fixed(0.f);s.moving=true;s.clockwise=true;s.revolutionSeconds=10.f;
    dsp.reset();if(!dsp.setSettings(s))throw std::runtime_error("No 360 orbit");
    const int frames=rate*10;
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(frames)*4);
    for(int base=0;base<frames;base+=framesPerBlock){
        const int n=std::min(framesPerBlock,frames-base);
        std::array<float,framesPerBlock> l{},r{};
        for(int i=0;i<n;++i){
            const int pos=base+i;
            const float gain=std::clamp(
                std::min(pos,frames-1-pos)/static_cast<float>(rampFrames),0.f,1.f);
            const float v=noise.sample()*gain;
            l[static_cast<std::size_t>(i)]=v;
            r[static_cast<std::size_t>(i)]=v;
        }
        dsp.process(l.data(),r.data(),n);
        for(int i=0;i<n;++i)appendStereo(result,l[i],r[i]);
    }
    return result;
}
const char* direction(int deg) {
    switch(deg){case 0:return "FRONT";case 90:return "RIGHT";case 180:return "REAR";
        case -90:return "LEFT";default:return "INVALID";}
}
void notes(const std::filesystem::path& directory,
           const std::vector<int>& blind) {
    std::ofstream f(directory/"LISTENING-GUIDE.txt");
    if(!f)throw std::runtime_error("Cannot create guide");
    f<<"8D-SonKu measured-HRTF headphone listening qualification\n"
      <<"SOURCE: EXACT engine/builtin_fx/sonku/SonKuSpatialCore.h in this SHA.\n"
      <<"Audio: 48kHz, 16-bit stereo PCM, nonpersonalized MIT KEMAR, 0deg elevation.\n"
      <<"Generated original broadband test noise; NO licensed music or speech.\n\n"
      <<"HEADPHONES ONLY. Disable OS spatializer/enhancement, avoid speakers.\n"
      <<"Start at LOW volume. Do not treat this as a certified perceptual pass.\n\n"
      <<"sonku-reference-four.wav: 4 x 2.5 seconds; each position has 2s noise\n"
      <<"then 0.5s quiet. Order: FRONT, RIGHT, REAR, LEFT.\n"
      <<"sonku-blind-eight.wav: 8 x 2.5 seconds, balanced two times each.\n"
      <<"Write down FRONT/RIGHT/REAR/LEFT for trials 1..8 before opening KEY.\n"
      <<"sonku-orbit-cw.wav: 10s one continuous 360deg clockwise orbit,\n"
      <<"starting at FRONT, acoustic RIGHT on listener's right headphone.\n\n"
      <<"If front/rear is ambiguous or internalized, note headphones and listener.\n"
      <<"These are not room reflections, personalized HRTFs or VST3 binaries.\n";
    std::ofstream key(directory/"BLIND-ANSWER-KEY.txt");
    if(!key)throw std::runtime_error("Cannot write blind answer key");
    key<<"OPEN ONLY AFTER LISTENING. Exact 2.5-second trial windows:\n";
    for(std::size_t i=0;i<blind.size();++i)
        key<<"Trial "<<i+1<<": "<<direction(blind[i])<<"\n";
}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("Usage: sonku-offline-demo OUTPUT_DIRECTORY");
        const std::filesystem::path dir(argv[1]);
        std::filesystem::create_directories(dir);
        constexpr std::array<int,4> reference{0,90,180,-90};
        const std::vector<int> blind{180,0,-90,90,90,180,0,-90};
        writeWav(dir/"sonku-reference-four.wav",
            staticProbes({reference.begin(),reference.end()}));
        writeWav(dir/"sonku-blind-eight.wav",staticProbes(blind));
        writeWav(dir/"sonku-orbit-cw.wav",orbit());
        notes(dir,blind);
        std::cout<<"SonKu listening package PASS: measured native HRTF, "
                 <<"4 labeled + 8 blind directions + one 360 orbit\n";
    }catch(const std::exception& error){
        std::cerr<<"SonKu offline render FAILED: "<<error.what()<<"\n";
        return 1;
    }
}
