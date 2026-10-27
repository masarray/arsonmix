// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/OrbitDistanceMapping.h"
#include <cmath>
#include <iostream>
#include <limits>
int main(){
  using armix::core::normalizedForDisplayedSetpoint;
  int failed=0;
  auto check=[&](bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<"\n";++failed;}};
  // Simulate a vendor's quantized text for a 0.1-30m parameter.
  auto vendor=[](float v){
    const double physical=0.1+29.9*std::pow(v,1.7);
    return std::round(physical*100.)/100.;
  };
  float norm=0.f;
  check(normalizedForDisplayedSetpoint(2.,.1,30.,vendor,norm),"2m mapped");
  check(vendor(norm)==2.,"2m displayed exactly (never 1.99)");
  check(normalizedForDisplayedSetpoint(1.5,.1,30.,vendor,norm),"1.5m mapped");
  check(vendor(norm)==1.5,"1.5m roundtrip");
  check(normalizedForDisplayedSetpoint(30.,.1,30.,vendor,norm),"upper boundary");
  check(vendor(norm)==30.,"30m exact boundary");
  check(!normalizedForDisplayedSetpoint(31.,.1,30.,vendor,norm),"outside bounds rejected");
  // A plugin with only 0.1m increments cannot represent 2.05m;
  // silently using 2.0/2.1 would mislead the user.
  auto coarse=[](float v){return std::round(v*100.)/10.;};
  check(!normalizedForDisplayedSetpoint(2.05,0.,10.,coarse,norm),"unrepresentable rejected");
  auto nan=[](float){return std::numeric_limits<double>::quiet_NaN();};
  check(!normalizedForDisplayedSetpoint(2.,0.,10.,nan,norm),"invalid vendor mapping rejected");
  return failed?1:0;
}
