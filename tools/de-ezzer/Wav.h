// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <vector>
#include <array>
#include <cmath>
namespace dez_wav {
using Bytes=std::vector<unsigned char>;
struct Audio { unsigned rate=0,channels=0;std::array<std::vector<float>,2> pcm; };
inline void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline unsigned get(const unsigned char* p,unsigned n){unsigned v=0;for(unsigned i=0;i<n;++i)v|=static_cast<unsigned>(p[i])<<(8*i);return v;}
inline void put(std::ostream& out,unsigned v,unsigned n){for(unsigned i=0;i<n;++i)out.put(static_cast<char>((v>>(8*i))&255));}
inline bool tag(const unsigned char* p,const char* s){return std::memcmp(p,s,4)==0;}
inline bool supported(unsigned r){return r>=44100&&r<=96000;}
inline Audio read(std::istream& in){
    std::array<unsigned char,12> h{};in.read(reinterpret_cast<char*>(h.data()),12);
    require(static_cast<bool>(in)&&tag(h.data(),"RIFF")&&tag(h.data()+8,"WAVE"),"expected little-endian RIFF WAVE");
    const unsigned size=get(h.data()+4,4);require(size>=4&&size<=256u*1024*1024,"RIFF size outside offline limit");
    Bytes b(size-4);in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));require(static_cast<bool>(in),"truncated RIFF");
    const unsigned char* fmt=nullptr;const unsigned char* data=nullptr;unsigned fmtSize=0,dataSize=0;
    for(std::size_t pos=0;pos<b.size();){
        require(b.size()-pos>=8,"truncated chunk header");const auto* p=b.data()+pos;const unsigned n=get(p+4,4);pos+=8;
        require(n<=b.size()-pos&&static_cast<std::size_t>(n)+(n&1u)<=b.size()-pos,"truncated chunk");
        if(tag(p,"fmt ")){require(!fmt,"duplicate fmt");fmt=b.data()+pos;fmtSize=n;}
        if(tag(p,"data")){require(!data,"duplicate data");data=b.data()+pos;dataSize=n;}
        pos+=static_cast<std::size_t>(n)+(n&1u);
    }
    require(fmt&&fmtSize>=16&&data,"missing fmt/data");
    const unsigned format=get(fmt,2),ch=get(fmt+2,2),rate=get(fmt+4,4),align=get(fmt+12,2),bits=get(fmt+14,2),bytes=bits/8;
    require((ch==1||ch==2)&&supported(rate),"unsupported channels/rate");
    require((format==1&&(bits==16||bits==24||bits==32))||(format==3&&bits==32),"unsupported PCM format");
    require(align==ch*bytes&&get(fmt+8,4)==rate*align&&dataSize%align==0,"invalid PCM alignment");
    Audio a{rate,ch,{}};const unsigned frames=dataSize/align;require(frames>0,"empty PCM");
    for(unsigned c=0;c<ch;++c)a.pcm[c].resize(frames);
    for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<ch;++c){
        const unsigned u=get(data+static_cast<std::size_t>(i)*align+c*bytes,bytes);double x=0;
        if(format==3)x=std::bit_cast<float>(static_cast<std::uint32_t>(u));
        else {const double full=std::ldexp(1.,static_cast<int>(bits));x=(static_cast<double>(u)-(u>=full/2?full:0))/(full/2);}
        require(std::isfinite(x)&&std::abs(x)<=16.,"nonfinite/out-of-domain PCM");a.pcm[c][i]=static_cast<float>(x);
    }
    return a;
}
inline void write(std::ostream& out,const Audio& a){
    const auto n=a.pcm[0].size();require(supported(a.rate)&&(a.channels==1||a.channels==2)&&n>0&&n<=(256u*1024*1024-50)/(4*a.channels),"invalid output shape");
    require(a.channels==1||a.pcm[1].size()==n,"channel size mismatch");
    const unsigned size=static_cast<unsigned>(n*4*a.channels);
    out.write("RIFF",4);put(out,50+size,4);out.write("WAVEfmt ",8);put(out,18,4);put(out,3,2);put(out,a.channels,2);put(out,a.rate,4);put(out,a.rate*a.channels*4,4);put(out,a.channels*4,2);put(out,32,2);put(out,0,2);
    out.write("fact",4);put(out,4,4);put(out,static_cast<unsigned>(n),4);out.write("data",4);put(out,size,4);
    for(std::size_t i=0;i<n;++i)for(unsigned c=0;c<a.channels;++c){const float x=a.pcm[c][i];require(std::isfinite(x)&&std::abs(x)<=16.f,"invalid output sample");put(out,std::bit_cast<std::uint32_t>(x),4);}
    require(static_cast<bool>(out),"WAV write failed");
}
} // namespace dez_wav
