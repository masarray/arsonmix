// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DearVrPolarPolicy.h"
#include <iostream>
int main(){using namespace armix::core;int f=0;
 auto test=[&](bool pass){if(!pass)++f;};
 test(isDearVrPro2("dearVR PRO 2"));test(isDearVrPro2("dearVR PRO2"));
 test(!isDearVrPro2("dearVR PRO"));test(!isDearVrPro2("Other Spatial FX"));
 test(isCartesianPolarMode("input mode","cartesian","polar"));
 test(isCartesianPolarMode("coordinate mode","cartesian xyz","polar r"));
 test(!isCartesianPolarMode("input mode","off","on"));
 test(!isCartesianPolarMode("azimuth scale","0","1"));
 if(f)std::cerr<<"POLAR POLICY FAIL "<<f<<"\n";return f?1:0;
}
