// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    std::mt19937 gen(42);std::uniform_real_distribution<float> d(-.003f,.003f);
    offline::Audio noise{48000,2,std::vector<float>(144000*2)};for(auto& x:noise.samples)x=d(gen);
    offline::Audio input=noise;for(std::size_t i=0;i<input.samples.size()/2;++i){if(i<48000||i>=96000){input.samples[2*i]+=.1f*std::sin(float(i)*.07f);input.samples[2*i+1]+=.08f*std::cos(float(i)*.11f);}}
    Core::Settings s;s.mode=Core::Mode::Music;
    auto dry=offline::render(input,nullptr,s,31);offline::require(dry.deltaRms<1e-7,"music reconstruction");
    s.mode=Core::Mode::Voice;auto a=offline::render(input,&noise,s,64),b=offline::render(input,&noise,s,127);
    offline::require(a.processed.samples==b.processed.samples,"offline block parity");
    double before=0,after=0;for(std::size_t i=80000*2;i<95000*2;++i){before+=double(input.samples[i])*input.samples[i];after+=double(a.processed.samples[i])*a.processed.samples[i];}
    const double reduction=10*std::log10(before/after);offline::require(reduction>=20,"stereo idle reduction");
    for(std::size_t i=0;i<input.samples.size();++i)offline::require(std::abs(input.samples[i]-a.processed.samples[i]-a.delta.samples[i])<1e-7f,"delta identity");
    auto rejects=[](auto fn){try{fn();}catch(const std::exception&){return true;}return false;};
    offline::Audio shortNoise{48000,2,std::vector<float>(512,.001f)};
    offline::require(rejects([&]{(void)offline::render(input,&shortNoise,s);}),"short learn rejected");
    const auto path=std::filesystem::current_path()/"anti-noiz-offline-test.wav";
    offline::write(path,a.processed);auto restored=offline::read(path);
    offline::require(rejects([&]{offline::write(path,a.processed);}),"overwrite rejected");
    std::filesystem::resize_file(path,20);
    offline::require(rejects([&]{(void)offline::read(path);}),"truncated WAV rejected");
    std::filesystem::remove(path);offline::require(restored.samples==a.processed.samples,"float WAV parity");
    std::cout<<"Offline aligned A/B/delta PASS; independent stereo idle reduction "<<reduction<<" dB\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
