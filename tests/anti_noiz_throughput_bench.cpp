// SPDX-License-Identifier: AGPL-3.0-only
// Standalone exploratory throughput benchmark; not a realtime deadline test.
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <array>
#include <chrono>
#include <iostream>
#include <random>
using armix::builtin_fx::anti_noiz::Core;
int main(){for(int mode=0;mode<3;++mode){Core c;c.prepare(48000,2);std::array<float,64> l{},r{};std::mt19937 g(42);std::uniform_real_distribution<float>d(-.003f,.003f);if(mode){c.beginLearn();for(int b=0;b<1600;++b){for(int i=0;i<64;++i)l[i]=r[i]=d(g);c.process(l.data(),r.data(),64);}if(!c.hasProfile())return 1;c.setSettings({mode==1?Core::Mode::Music:Core::Mode::Voice,14,32});}std::array<float,64> src{};for(auto& x:src)x=d(g);double checksum=0;auto start=std::chrono::steady_clock::now();for(int b=0;b<60000;++b){l=src;r=src;c.process(l.data(),r.data(),64);checksum+=l[b%64];}auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<mode<<" "<<ms<<" ms "<<checksum<<"\n";}}
