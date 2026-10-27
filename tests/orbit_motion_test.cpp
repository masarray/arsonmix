// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/OrbitMotion.h"
#include <cmath>
#include <iostream>
#include <array>
int main() {
    using namespace armix::core;
    int failures=0;
    const auto check=[&](bool b,const char* desc) {
        if(!b){std::cerr<<"FAIL "<<desc<<"\n";++failures;}
    };
    check(!needsOrbitDistance(0)&&!needsOrbitElevation(0),"circle azimuth only");
    check(needsOrbitDistance(1)&&!needsOrbitElevation(1),"front/back requires distance");
    check(needsOrbitElevation(3)&&!needsOrbitDistance(3),"rising circle elevation only");
    for(int shape=0;shape<=5;++shape) {
        for(double p: {0.0,0.125,0.25,0.5,0.75,0.99}) {
            const auto v=orbitMotionPoint(p,shape);
            const auto end=orbitMotionPoint(p+1.0,shape);
            check(std::abs(v.x-end.x)<1.e-5&&std::abs(v.y-end.y)<1.e-5 &&
                  std::abs(v.z-end.z)<1.e-5,"closed path");
            check(v.azimuth>=0.f&&v.azimuth<=1.f,"bounded azimuth");
            check(v.distance>=0.f&&v.distance<=1.f,"bounded distance");
            check(v.elevation>=0.f&&v.elevation<=1.f,"bounded elevation");
            const auto restored=orbitShapePhaseForAzimuth(v.azimuth,shape);
            const auto r=orbitMotionPoint(restored,shape);
            check(std::abs(v.x-r.x)<1.e-4&&std::abs(v.y-r.y)<1.e-4,
                  "azimuth phase inverse");
        }
    }
    const auto fore=orbitMotionPoint(0.,1),side=orbitMotionPoint(.25,1);
    check(fore.distance>side.distance,"front/back far sides near");
    const auto fore2=orbitMotionPoint(0.,2),side2=orbitMotionPoint(.25,2);
    check(fore2.distance<side2.distance,"front/back near sides far");
    const auto up=orbitMotionPoint(.25,3),down=orbitMotionPoint(.75,3);
    check(up.elevation>0.5&&down.elevation<0.5,"rise and fall");
    check(std::abs(orbitMotionPoint(.25,0,.15,.85).distance-orbitMotionPoint(0.,0,.15,.85).distance)<1.e-5,
          "circle radius constant");
    check(std::abs(orbitMotionPoint(.25,0,.2,.4).distance-.4f)<1.e-5,
          "circle far setpoint controls its actual radius");
    check(std::abs(orbitMotionPoint(.25,1,.1,.6).distance-.1f)<1.e-5,
          "ellipse uses near setpoint at sides");
    check(std::abs(orbitMotionPoint(0.,1,.1,.6).distance-.6f)<1.e-5,
          "ellipse uses far setpoint at front/back");
    check(std::abs(orbitMotionPoint(.25,1,.25,.9).distance-.25f)<1.e-5,
          "changing distance setpoints alters C++ output");
    return failures?1:0;
}
