// SPDX-License-Identifier: AGPL-3.0-only
#include "Render.h"
#include <sstream>
#include <iostream>
#include <cstdlib>
void check(bool ok,const char* s){if(!ok){std::cerr<<s<<'\n';std::exit(1);}}
int main(){
    using namespace dez_render;
    for(unsigned bits:{16u,24u,32u}){
        std::ostringstream o(std::ios::binary);const auto bytes=bits/8;const unsigned n=2*bytes;
        o.write("RIFF",4);dez_wav::put(o,36+n,4);o.write("WAVEfmt ",8);dez_wav::put(o,16,4);dez_wav::put(o,1,2);dez_wav::put(o,1,2);dez_wav::put(o,48000,4);dez_wav::put(o,48000*bytes,4);dez_wav::put(o,bytes,2);dez_wav::put(o,bits,2);o.write("data",4);dez_wav::put(o,n,4);dez_wav::put(o,1u<<(bits-1),bytes);dez_wav::put(o,1u<<(bits-2),bytes);
        std::istringstream i(o.str(),std::ios::binary);auto a=dez_wav::read(i);check(a.pcm[0]==std::vector<float>{-1,.5f},"PCM decode");
    }
    dez_wav::Audio dry{48000,2,{}};for(int i=0;i<48000;++i){float x=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*i/48000.)+.25*std::sin(2*std::numbers::pi*7200*i/48000.));dry.pcm[0].push_back(x);dry.pcm[1].push_back(-x);}
    auto a=render(dry,{},1),b=render(dry,{},257);check(a.wet.pcm==b.wet.pcm&&a.delta.pcm==b.delta.pcm,"partition parity");
    double energy=0;for(unsigned c=0;c<2;++c)for(std::size_t i=0;i<dry.pcm[c].size();++i){check(std::abs((a.wet.pcm[c][i]+a.delta.pcm[c][i])-dry.pcm[c][i])<1e-7,"delta identity");energy+=static_cast<double>(a.delta.pcm[c][i])*a.delta.pcm[c][i];}check(energy>1,"nonzero delta");
    Settings zero;zero.amount=0;auto bypass=render(dry,zero,64);check(bypass.wet.pcm==dry.pcm,"unity");for(const auto& c:bypass.delta.pcm)for(float x:c)check(x==0,"zero delta");
    std::ostringstream o(std::ios::binary);dez_wav::write(o,a.delta);std::istringstream i(o.str(),std::ios::binary);auto round=dez_wav::read(i);check(round.pcm==a.delta.pcm&&round.rate==48000&&round.channels==2,"float roundtrip");
    for(auto malformed:{o.str().substr(0,11),o.str().substr(0,o.str().size()-1)}){bool rejected=false;try{std::istringstream bad(malformed,std::ios::binary);(void)dez_wav::read(bad);}catch(const std::runtime_error&){rejected=true;}check(rejected,"reject truncated RIFF");}
    std::cout<<"PASS: PCM decode, float roundtrip, delta identity, nonzero/zero delta, partition, malformed input\n";
}
