// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace armix::builtin_fx::anti_noiz::offline {
// Offline only. Strict little-endian RIFF, bounded memory, no dependencies.
struct Audio { unsigned rate=0, channels=0; std::vector<float> samples; };
inline constexpr std::size_t maxBytes=128u*1024u*1024u;
inline void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
inline std::uint32_t u(const unsigned char* p,unsigned bytes){
    std::uint32_t v=0;for(unsigned i=0;i<bytes;++i)v|=std::uint32_t(p[i])<<(8*i);return v;
}
inline bool tag(const unsigned char* p,const char* s){return std::equal(p,p+4,s);}
inline Audio read(const std::filesystem::path& path){
    const auto size=std::filesystem::file_size(path);require(size>=12&&size<=maxBytes,"WAV size outside supported bounds");
    std::vector<unsigned char> b(static_cast<std::size_t>(size));std::ifstream in(path,std::ios::binary);
    require(bool(in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()))),"WAV read failed");
    require(tag(b.data(),"RIFF")&&tag(b.data()+8,"WAVE")&&std::uint64_t(u(b.data()+4,4))+8==size,"invalid RIFF/WAVE length");
    const unsigned char *fmt=nullptr,*data=nullptr;std::size_t fmtSize=0,dataSize=0;
    for(std::size_t p=12;p<b.size();){
        require(b.size()-p>=8,"truncated chunk header");const auto n=std::size_t(u(b.data()+p+4,4));
        require(n<=b.size()-p-8&&n+(n&1u)<=b.size()-p-8,"truncated chunk");
        if(tag(b.data()+p,"fmt ")){require(!fmt,"duplicate fmt");fmt=b.data()+p+8;fmtSize=n;}
        if(tag(b.data()+p,"data")){require(!data,"duplicate data");data=b.data()+p+8;dataSize=n;}
        p+=8+n+(n&1u);
    }
    require(fmt&&fmtSize>=16&&data,"missing WAV chunks");
    const auto type=u(fmt,2),channels=u(fmt+2,2),rate=u(fmt+4,4),align=u(fmt+12,2),bits=u(fmt+14,2);
    require((channels==1||channels==2)&&rate>=44100&&rate<=96000,"unsupported WAV format/rate");
    require((type==1&&(bits==16||bits==24||bits==32))||(type==3&&bits==32),"use PCM16/24/32 or IEEE float32 WAV");
    const auto bytes=bits/8;require(align==channels*bytes&&u(fmt+8,4)==rate*align&&dataSize>0&&dataSize%align==0,"invalid WAV alignment");
    require(dataSize/bytes<=maxBytes/sizeof(float),"decoded WAV exceeds memory cap");
    Audio a{rate,channels,{}};a.samples.reserve(dataSize/bytes);
    for(std::size_t i=0;i<dataSize;i+=bytes){
        const auto raw=u(data+i,bytes);float x;
        if(type==3)x=std::bit_cast<float>(raw);
        else {const std::uint64_t sign=std::uint64_t(1)<<(bits-1);const auto signedValue=std::int64_t(raw)-(raw&sign?std::int64_t(sign*2):0);x=float(double(signedValue)/double(sign));}
        require(std::isfinite(x)&&std::abs(x)<=64.f,"invalid or out-of-range PCM");a.samples.push_back(x);
    }return a;
}
inline void put(std::ostream& o,std::uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)o.put(static_cast<char>((v>>(8*i))&255u));}
inline void write(const std::filesystem::path& path,const Audio& a){
    require((a.channels==1||a.channels==2)&&a.rate>=44100&&a.rate<=96000&&!a.samples.empty()&&a.samples.size()%a.channels==0&&a.samples.size()<=maxBytes/4,"invalid output WAV");
    for(float x:a.samples)require(std::isfinite(x),"nonfinite output PCM");
    require(!std::filesystem::exists(path),"refusing to overwrite WAV");std::ofstream o(path,std::ios::binary);
    const auto n=static_cast<std::uint32_t>(a.samples.size()*4);
    o.write("RIFF",4);put(o,n+48,4);o.write("WAVEfmt ",8);put(o,16,4);put(o,3,2);put(o,a.channels,2);put(o,a.rate,4);put(o,a.rate*a.channels*4,4);put(o,a.channels*4,2);put(o,32,2);
    o.write("fact",4);put(o,4,4);put(o,static_cast<std::uint32_t>(a.samples.size()/a.channels),4);
    o.write("data",4);put(o,n,4);for(float x:a.samples)put(o,std::bit_cast<std::uint32_t>(x),4);
    o.close();require(bool(o),"WAV write failed");
}
struct Result { Audio processed,delta; double inputRms=0,outputRms=0,deltaRms=0; int latency=0; };
inline Result render(const Audio& input,const Audio* noise,Core::Settings settings,int block=128,Core::SourceRole source=Core::SourceRole::Unknown){
    Core core;require(core.prepare(input.rate,static_cast<int>(input.channels),source)&&core.setSettings(settings),"invalid DSP format/settings");
    require(!input.samples.empty()&&input.samples.size()%input.channels==0&&block>=1&&block<=1024,"invalid input/block");
    std::array<float,1024> l{},r{};
    auto fill=[&](const Audio& a,std::size_t start,int n){for(int i=0;i<n;++i){auto j=(start+std::size_t(i))*a.channels;l[i]=j<a.samples.size()?a.samples[j]:0;r[i]=a.channels==2&&j+1<a.samples.size()?a.samples[j+1]:0;}};
    if(noise){
        require(noise->rate==input.rate&&noise->channels==input.channels&&noise->samples.size()%input.channels==0,"noise profile format mismatch");
        require(core.beginLearn(),"cannot begin learning");
        const auto frames=noise->samples.size()/noise->channels;
        for(std::size_t p=0;p<frames&&core.learning();){int n=static_cast<int>(std::min<std::size_t>(block,frames-p));fill(*noise,p,n);core.process(l.data(),r.data(),n);p+=std::size_t(n);}
        require(core.hasProfile()&&!core.learning(),"noise learning incomplete or rejected; provide >2s noise only");
    }else require(core.effectiveMode()==Core::Mode::Music||!settings.enabled,"noise reference required for active modes");
    core.resetStreamKeepProfile();
    Result out;out.latency=core.latencySamples();out.processed={input.rate,input.channels,std::vector<float>(input.samples.size())};out.delta=out.processed;
    const auto frames=input.samples.size()/input.channels, delay=std::size_t(out.latency);
    for(std::size_t p=0;p<frames+delay;){
        const int n=static_cast<int>(std::min<std::size_t>(block,frames+delay-p));fill(input,p,n);core.process(l.data(),r.data(),n);
        for(int i=0;i<n;++i){const auto t=p+std::size_t(i);if(t<delay||t-delay>=frames)continue;const auto j=(t-delay)*input.channels;out.processed.samples[j]=l[i];if(input.channels==2)out.processed.samples[j+1]=r[i];}p+=std::size_t(n);
    }
    for(std::size_t i=0;i<input.samples.size();++i){const double x=input.samples[i],y=out.processed.samples[i];const float d=float(x-y);out.delta.samples[i]=d;out.inputRms+=x*x;out.outputRms+=y*y;out.deltaRms+=double(d)*d;}
    const double count=double(input.samples.size());out.inputRms=std::sqrt(out.inputRms/count);out.outputRms=std::sqrt(out.outputRms/count);out.deltaRms=std::sqrt(out.deltaRms/count);return out;
}
} // namespace armix::builtin_fx::anti_noiz::offline
