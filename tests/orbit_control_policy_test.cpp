// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/OrbitControlPolicy.h"
#include <limits>
#include <iostream>
#include <cmath>
int main() {
    using namespace armix::core;
    int failures=0;
    auto check=[&](bool success,const char* what) {
        if(!success){std::cerr<<"FAIL: "<<what<<"\n";++failures;}
    };
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    check(shouldPublishOrbit(.5f,nan,360),"initial tick");
    check(!shouldPublishOrbit(.5f,.5f,360),"unchanged");
    check(!shouldPublishOrbit(.501f,.5f,360),"quantized duplicate");
    check(shouldPublishOrbit(.505f,.5f,360),"quantized advance");
    check(shouldPublishOrbit(.1f,.09f,2147483647),"continuous change");
    check(!shouldPublishOrbit(.1f,.100001f,2147483647),"continuous duplicate");
    check(!shouldPublishOrbit(0.f,1.f,360),"seam equivalence");
    check(shouldPublishOrbit(.002f,.999f,360),"postwrap advance");
    check(!shouldPublishOrbit(nan,.1f,360),"nonfinite rejected");
    check(!shouldPublishOrbit(1.2f,.1f,360),"range rejected");
    OrbitDispatchStats s;
    s.observe(16.7);s.observe(50.1);s.observe(120.);
    s.observe(-2.);s.observe(std::numeric_limits<double>::quiet_NaN());
    check(s.lateTicks==2 && std::abs(s.maxGapMs-120.)<.0001,"control delay evidence");
    return failures?1:0; // Release-safe with NDEBUG
}
