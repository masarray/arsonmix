// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/IndependentClockPipeline.h"
#include <array>
#include <cmath>
#include <cstdlib>
static void check(bool x){if(!x)std::abort();}
int main()
{
    using armix::device::IndependentClockPipeline;
    IndependentClockPipeline p;
    check(p.prepare(44100.,48000.,2,8192,30.));
    std::array<float,512> a{},b{},c{},d{};
    const float* in[2]={a.data(),b.data()};
    float* out[2]={c.data(),d.data()};
    for(int i=0;i<512;++i)a[i]=b[i]=0.15f;
    for(int n=0;n<1200;++n)
    {
        const int captureFrames=(n%10==0)?45:44; // independent source tick
        p.capture(in,2,captureFrames);
        p.render(out,2,48);
        for(int i=0;i<48;++i)
        {
            check(std::isfinite(c[i]) && std::isfinite(d[i]));
            check(std::abs(c[i])<0.25f && std::abs(d[i])<0.25f);
        }
    }
    const auto status=p.streamStatus(),bridge=p.bridgeStatus();
    check(status.captureCallbacks==1200 && status.renderCallbacks==1200);
    check(status.captureFrames>50000 && status.renderFrames==57600);
    check(status.captureDropFrames==bridge.overflowFrames);
    check(status.renderMissingFrames==bridge.underflowFrames);
    check(bridge.primingFrames>0);
    check(bridge.fillFrames<=8192);
    return 0;
}
