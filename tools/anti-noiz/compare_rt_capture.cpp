// SPDX-License-Identifier: AGPL-3.0-only
// Stream the actual rt_qualify captures, verify sample counts and reconstruct
// the exact FNV fingerprint. No generated fixture or alternative render path.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
using U=std::uint64_t;
constexpr U offset=14695981039346656037ull,prime=1099511628211ull;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Stats {
    U bits=0,numeric=0,first=std::numeric_limits<U>::max(),count=0;
    double peak=0,sum=0,ref=0;
    void add(float a,float b,U frame){
        check(std::isfinite(a)&&std::isfinite(b),"nonfinite capture");
        if(std::bit_cast<std::uint32_t>(a)!=std::bit_cast<std::uint32_t>(b)){
            ++bits;first=std::min(first,frame);
        }
        const double d=double(a)-double(b);numeric+=d!=0;
        peak=std::max(peak,std::abs(d));sum+=d*d;ref+=double(a)*a;++count;
    }
    double rms()const{return std::sqrt(sum/double(count));}
    double relative()const{return sum==0?-std::numeric_limits<double>::infinity():ref==0?std::numeric_limits<double>::infinity():10*std::log10(sum/ref);}
};
void report(const std::string& a,const std::string& b,unsigned rate,int block,const char* mode){
    const std::string suffix="-"+std::to_string(rate)+"-"+std::to_string(block)+"-"+mode+".f32";
    std::ifstream old(a+suffix,std::ios::binary),now(b+suffix,std::ios::binary);
    check(bool(old)&&bool(now),"capture open");
    const U total=U(block)+13ull*rate,learnEnd=U(block)+3ull*rate;
    std::array<float,4096> x{},y{};
    U hx=offset,hy=offset;
    Stats input,output,learn,active;
    for(U pos=0;pos<total;){
        const U n=std::min<U>(1024,total-pos);
        const auto bytes=std::streamsize(n*4*sizeof(float));
        old.read(reinterpret_cast<char*>(x.data()),bytes);now.read(reinterpret_cast<char*>(y.data()),bytes);
        check(old.gcount()==bytes&&now.gcount()==bytes,"capture truncated");
        for(U j=0;j<n;++j)for(int c=0;c<2;++c){
            const auto k=4*j+U(c);
            input.add(x[k],y[k],pos+j);
            output.add(x[k+2],y[k+2],pos+j);
            (pos+j<learnEnd?learn:active).add(x[k+2],y[k+2],pos+j);
            hx=(hx^std::bit_cast<std::uint32_t>(x[k+2]))*prime;
            hy=(hy^std::bit_cast<std::uint32_t>(y[k+2]))*prime;
        }
        pos+=n;
    }
    check(old.peek()==std::char_traits<char>::eof()&&now.peek()==std::char_traits<char>::eof(),"capture extra samples");
    std::cout<<std::setprecision(12)<<"CAPTURE,"<<rate<<','<<block<<','<<mode<<','<<input.bits<<','<<output.bits<<','<<output.numeric<<','
             <<(output.bits?std::to_string(output.first):"-1")<<','<<output.peak<<','<<output.rms()<<','<<output.relative()<<','
             <<learn.bits<<','<<active.bits<<','<<active.peak<<','<<active.relative()<<','<<hx<<','<<hy<<'\n';
    check(input.bits==0,"input mismatch between revisions");
}
}
int main(int argc,char** argv){try{
    check(argc==3,"old and new capture prefix");
    for(unsigned rate:{44100u,48000u,88200u,96000u})
        for(int block:{32,64,128,256,1024})
            for(const char* mode:{"weak-auto","coherent","spectral"})
                report(argv[1],argv[2],rate,block,mode);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
