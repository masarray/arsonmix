// SPDX-License-Identifier: AGPL-3.0-only
#include "Render.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc,char** argv){
    try{
        if(argc!=9)throw std::runtime_error("usage: de_ezzer_render input.wav wet.wav delta.wav auto|voice|auto-focused|voice-focused amount sensitivity rangeDb block");
        using namespace dez_render;Settings s;
        const std::string mode=argv[4];dez_wav::require(mode=="auto"||mode=="voice"||mode=="auto-focused"||mode=="voice-focused","invalid mode");s.mode=(mode=="voice"||mode=="voice-focused")?Mode::Voice:Mode::Auto;s.shape=(mode=="auto-focused"||mode=="voice-focused")?Shape::Focused:Shape::Shelf;
        auto number=[](const char* x){std::size_t end=0;float v=std::stof(x,&end);dez_wav::require(end==std::strlen(x)&&std::isfinite(v),"invalid number");return v;};
        s.amount=number(argv[5]);s.sensitivity=number(argv[6]);s.rangeDb=number(argv[7]);const float b=number(argv[8]);dez_wav::require(b>=1&&b<=65536&&std::floor(b)==b,"invalid block");
        namespace fs=std::filesystem;const auto wet=fs::weakly_canonical(argv[2]),delta=fs::weakly_canonical(argv[3]);
        dez_wav::require(wet!=delta&&!fs::exists(wet)&&!fs::exists(delta),"output paths must be distinct and new");
        std::ifstream in(argv[1],std::ios::binary);dez_wav::require(static_cast<bool>(in),"cannot open input");const auto dry=dez_wav::read(in);const auto r=render(dry,s,static_cast<unsigned>(b));
        std::ofstream w(wet,std::ios::binary),d(delta,std::ios::binary);dez_wav::require(static_cast<bool>(w)&&static_cast<bool>(d),"cannot open outputs");dez_wav::write(w,r.wet);dez_wav::write(d,r.delta);w.close();d.close();dez_wav::require(static_cast<bool>(w)&&static_cast<bool>(d),"output close failed");
        std::cout<<"rendered "<<dry.pcm[0].size()<<" frames at "<<dry.rate<<" Hz, "<<dry.channels<<" channels; zero added sample delay\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
