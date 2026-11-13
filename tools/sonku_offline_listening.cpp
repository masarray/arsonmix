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
constexpr int orbitSecondsPerTurn=10;
constexpr int orbitTurns=2;
constexpr int orbitFrames=rate*orbitSecondsPerTurn*orbitTurns;
static_assert(orbitFrames==960000);
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
std::vector<std::uint8_t> staticProbes(const std::vector<int>& azimuths,float roomAmount=0.f) {
    SonKuSpatialCore dsp;Noise noise;
    if(!dsp.prepare(rate))throw std::runtime_error("No 48k HRTF");
    if(!dsp.setEarlyReflectionsAmount(roomAmount))
        throw std::runtime_error("Invalid early room amount");
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
std::vector<std::uint8_t> orbit(float roomAmount=0.f,
    armix::sonku::SonKuRoomShape shape=armix::sonku::SonKuRoomShape::Rectangular,
    bool clockwise=true,float lateAmount=0.f,
    armix::sonku::SonKuReverbPreset latePreset=
        armix::sonku::SonKuReverbPreset::StudioGlue){
    SonKuSpatialCore dsp;Noise noise;
    if(!dsp.prepare(rate))throw std::runtime_error("No 48k HRTF");
    if(!dsp.setEarlyReflectionsShape(shape)||
       !dsp.setMusicalReverbPreset(latePreset)||
       !dsp.setEarlyReflectionsAmount(roomAmount)||
       !dsp.setMusicalReverbAmount(lateAmount))
        throw std::runtime_error("Invalid orbit acoustic configuration");
    Settings s=fixed(0.f);s.moving=true;s.clockwise=clockwise;s.revolutionSeconds=10.f;
    dsp.reset();if(!dsp.setSettings(s))throw std::runtime_error("No 360 orbit");
    const int frames=orbitFrames; // two complete turns on ONE continuous audio clock
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
    const double finish=dsp.azimuthDegrees();
    if(std::min(std::abs(finish),std::abs(360.-finish))>.01)
        throw std::runtime_error("Two turns do not close acoustic orbit");
    return result;
}
// Original 20-second plucked-tone motif (20 half-second events, repeated on
// both revolutions). Synthesized here rather than using protected recordings.
// This deliberately exposes metallic late-tail coloration hidden by noise tests.
// It uses the EXACT same SonKuSpatialCore and accepted square early room.
std::vector<std::uint8_t> musicalOrbit(float lateAmount,
                                       armix::sonku::SonKuReverbPreset preset){
    SonKuSpatialCore dsp;
    if(!dsp.prepare(rate)||
       !dsp.setEarlyReflectionsShape(armix::sonku::SonKuRoomShape::Square)||
       !dsp.setMusicalReverbPreset(preset)||
       !dsp.setEarlyReflectionsAmount(.55f)||
       !dsp.setMusicalReverbAmount(lateAmount))
        throw std::runtime_error("Invalid original musical test configuration");
    Settings s=fixed(0.f);s.moving=true;s.clockwise=true;s.revolutionSeconds=10.f;
    dsp.reset();if(!dsp.setSettings(s))throw std::runtime_error("Invalid musical orbit");
    constexpr std::array<int,20> notes{
        57,64,60,69,62,65,69,60,64,67,
        71,67,65,72,69,64,60,67,64,62};
    std::array<double,notes.size()> frequencies{};
    for(std::size_t i=0;i<notes.size();++i)
        frequencies[i]=440.*std::pow(2.,(notes[i]-69.)/12.);
    constexpr int noteFrames=rate/2;
    constexpr double pi=3.14159265358979323846;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(orbitFrames)*4);
    for(int base=0;base<orbitFrames;base+=framesPerBlock){
        const int n=std::min(framesPerBlock,orbitFrames-base);
        std::array<float,framesPerBlock> l{},r{};
        for(int i=0;i<n;++i){
            const int at=base+i;
            const int note=(at/noteFrames)%static_cast<int>(notes.size());
            const int position=at%noteFrames;
            const float t=static_cast<float>(position)/noteFrames;
            const float attack=std::clamp(position/(rate*.009f),0.f,1.f);
            const float release=std::clamp(
                (noteFrames-1-position)/(rate*.030f),0.f,1.f);
            const float envelope=attack*release*std::exp(-3.2f*t);
            const double theta=2.*pi*frequencies[static_cast<std::size_t>(note)]*
                               static_cast<double>(position)/rate;
            const float tonal=static_cast<float>(
                std::sin(theta)+.27*std::sin(2.*theta)+
                .31*std::sin(theta*1.5));
            const float sample=.115f*envelope*tonal;
            l[static_cast<std::size_t>(i)]=sample;
            r[static_cast<std::size_t>(i)]=sample;
        }
        dsp.process(l.data(),r.data(),n);
        for(int i=0;i<n;++i)appendStereo(bytes,l[i],r[i]);
    }
    const double last=dsp.azimuthDegrees();
    if(std::min(std::abs(last),std::abs(360.-last))>.01)
        throw std::runtime_error("Musical double orbit did not close");
    return bytes;
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
      <<"sonku-orbit-cw-2x.wav: 20s TWO seamless continuous 360deg\n"
      <<"clockwise turns, 10s each, FRONT at 0/10/20s. No pause at 10s.\n"
      <<"At 2.5/12.5s RIGHT, 5/15s REAR, 7.5/17.5s LEFT.\n"
      <<"Acoustic RIGHT is on listener's right headphone.\n\n"
      <<"If front/rear is ambiguous or internalized, note headphones and listener.\n"
      <<"Baseline SonKu files have no room reflections (V1).\n"
      <<"V2 room candidate adds four 1st-order image-source reflections\n"
      <<"per emitter (6.4x8m fixed room) with bounded ear delays and level.\n"
      <<"sonku-v2-room-four.wav = A/B against sonku-reference-four.wav.\n"
      <<"sonku-v2-room-orbit-cw-2x.wav = 6.4 x 8m rectangular room.\n"
      <<"sonku-v2-square-orbit-cw-2x.wav = 7.2 x 7.2m symmetric room.\n"
      <<"Both candidates reuse the SAME direct HRTF/noise and 20s clock.\n"
      <<"Square geometry is not a guarantee of circular perception.\n"
      <<"CCW versions contain FRONT>LEFT>REAR>RIGHT>FRONT each 10s.\n"
      <<"Compare direction-reversed tracks to isolate turn-order effects.\n"
      <<"sonku-v3-square-glue-cw-2x.wav = approved square ROOM plus\n"
      <<"  subdued stereo 8-line FDN, Studio Glue RT60 ~0.78s.\n"
      <<"sonku-v3-square-airy-cw-2x.wav = same square ROOM with\n"
      <<"  more spacious FDN Airy Chamber RT60 ~1.28s.\n"
      <<"V2 square and V3 candidates share the exact 20s original source,\n"
      <<"HRTF, four-wall ER, angle clock and output trim.\n"
      <<"FDN candidate is wet-only and OFF in native plugin by default.\n"
      <<"sonku-musical-square-base-2x.wav = original synthetic plucks,\n"
      <<"  approved square ER with NO late reverb.\n"
      <<"sonku-musical-square-glue-2x.wav = identical plucks with Studio Glue.\n"
      <<"sonku-musical-square-airy-2x.wav = identical plucks with Airy Chamber.\n"
      <<"The plucks are ORIGINAL test tones (no copyrighted composition).\n"
      <<"Use them to judge vocal-like reverb coloration/tail musicality.\n"
      <<"Both comparisons reuse exactly the same source noise and direct HRIR.\n"
      <<"Do blind-level and externalization evaluation before enabling V2 live.\n"
      <<"This is NOT full HRIR-convolved reflections, FDN or personalized HRTF.\n";
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
        writeWav(dir/"sonku-orbit-cw-2x.wav",orbit());
        // Only the bounded early-reflection stage changes between paired
        // files. Source, directions, KEMAR HRIR, loudness trim and clock are
        // identical. Blind front/back listening is the decision gate.
        writeWav(dir/"sonku-v2-room-four.wav",
            staticProbes({reference.begin(),reference.end()},.55f));
        writeWav(dir/"sonku-v2-room-orbit-cw-2x.wav",orbit(.55f));
        writeWav(dir/"sonku-v2-square-orbit-cw-2x.wav",
                 orbit(.55f,armix::sonku::SonKuRoomShape::Square));
        writeWav(dir/"sonku-orbit-ccw-2x.wav",
                 orbit(0.f,armix::sonku::SonKuRoomShape::Rectangular,false));
        writeWav(dir/"sonku-v2-room-orbit-ccw-2x.wav",
                 orbit(.55f,armix::sonku::SonKuRoomShape::Rectangular,false));
        writeWav(dir/"sonku-v2-square-orbit-ccw-2x.wav",
                 orbit(.55f,armix::sonku::SonKuRoomShape::Square,false));
        writeWav(dir/"sonku-v3-square-glue-cw-2x.wav",
                 orbit(.55f,armix::sonku::SonKuRoomShape::Square,true,.42f,
                       armix::sonku::SonKuReverbPreset::StudioGlue));
        writeWav(dir/"sonku-v3-square-airy-cw-2x.wav",
                 orbit(.55f,armix::sonku::SonKuRoomShape::Square,true,.42f,
                       armix::sonku::SonKuReverbPreset::AiryChamber));
        writeWav(dir/"sonku-musical-square-base-2x.wav",
                 musicalOrbit(0.f,armix::sonku::SonKuReverbPreset::StudioGlue));
        writeWav(dir/"sonku-musical-square-glue-2x.wav",
                 musicalOrbit(.42f,armix::sonku::SonKuReverbPreset::StudioGlue));
        writeWav(dir/"sonku-musical-square-airy-2x.wav",
                 musicalOrbit(.42f,armix::sonku::SonKuReverbPreset::AiryChamber));
        notes(dir,blind);
        std::cout<<"SonKu listening package PASS: measured native HRTF, "
                 <<"4 labeled + 8 blind directions + two uninterrupted 360 turns\n";
    }catch(const std::exception& error){
        std::cerr<<"SonKu offline render FAILED: "<<error.what()<<"\n";
        return 1;
    }
}
