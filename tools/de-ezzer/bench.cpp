// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <chrono>
#include <iostream>
#include <vector>
using armix::builtin_fx::de_ezzer::Core;
int main(){
 using Clock=std::chrono::steady_clock;
 for(int frames:{32,64,128,256,512}){
  Core c;if(!c.prepare(48000,2))return 1;
  std::vector<float> l(frames),r(frames);std::vector<double> times;times.reserve(10000);
  for(int i=0;i<10200;++i){
   for(int j=0;j<frames;++j){double t=(i*frames+j)/48000.;l[j]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.25*std::sin(2*std::numbers::pi*7200*t));r[j]=-l[j];}
   auto start=Clock::now();c.process(l.data(),r.data(),frames);auto stop=Clock::now();
   if(i>=200)times.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
  }
  std::sort(times.begin(),times.end());std::cout<<frames<<" P50/P95/P99/max us "<<times[4999]<<' '<<times[9499]<<' '<<times[9899]<<' '<<times.back()<<" GR "<<c.bandReductionDb()<<'\n';
 }
}
