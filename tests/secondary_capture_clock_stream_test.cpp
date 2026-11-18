// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/SecondaryCaptureClockStream.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using Stream=armix::device::SecondaryCaptureClockStream;
static void check(bool ok){if(!ok){std::fputs("IO1_CLOCK_CAPTURE_FAIL\n",stderr);std::abort();}}

static void oneClockPair(double captureHz,double primaryHz)
{
    Stream s;
    const Stream::Format f{captureHz,primaryHz,128,256,2,7,19};
    check(s.prepare(f));
    check(s.targetMs()>=24.);
    check(s.activate());
    std::array<float,256> left{},right{},destL{},destR{};
    const float* write[2]={left.data(),right.data()};
    float* read[2]={destL.data(),destR.data()};
    for(int i=0;i<256;++i)
    {
        left[i]=static_cast<float>(0.21*std::sin(0.075*i));
        right[i]=static_cast<float>(0.37*std::cos(0.045*i));
    }
    for(int i=0;i<16;++i)check(s.push(write,2,256));
    check(!s.pull(read,2,256,7,18)); // old primary epoch: cannot consume
    check(destL[0]==0.f&&destR[255]==0.f);
    check(!s.pull(read,2,256,6,19)); // stale capture generation
    check(destL[77]==0.f&&destR[8]==0.f);
    bool nonzero=false;
    for(int block=0;block<48;++block)
    {
        check(s.pull(read,2,256,7,19));
        for(int j=0;j<256;++j)
        {
            check(std::isfinite(destL[j])&&std::isfinite(destR[j]));
            if(std::abs(destL[j])>0.003f&&std::abs(destR[j])>0.003f)
                nonzero=true;
        }
        if(block%2==0)check(s.push(write,2,256));
    }
    check(nonzero);
    const auto stats=s.status();
    check(stats.captureBlocks>=40&&stats.primaryBlocks==48);
    check(stats.staleReads==2&&stats.asrc.pushedFrames>0);
    check(stats.asrc.renderedFrames>0);
    s.fault();
    check(!s.ready()&&s.faulted());
    destL.fill(0.5f);destR.fill(0.5f);
    check(!s.pull(read,2,256,7,19));
    check(destL[0]==0.f&&destR[255]==0.f);
    s.stop();
    check(!s.ready()&&!s.faulted());
    check(s.prepare({48000.,48000.,128,256,1,8,20}));
    check(s.activate());
    const float* mono[1]={left.data()};
    check(s.push(mono,1,128));
    s.stop();
}

static void invalidAndOverflow()
{
    Stream s;
    check(!s.prepare({48000.,48000.,128,128,0,1,1}));
    check(!s.prepare({48000.,48000.,128,128,2,0,1}));
    check(!s.prepare({48000.,48000.,128,128,2,1,0}));
    check(!s.prepare({48000.,48000.,32769,128,2,1,1}));
    check(!s.prepare({48000.,48000.,128,32769,2,1,1}));
    check(!s.prepare({48000.,48000.,8192,8192,2,1,1}));

    // A rejected replacement of a valid, unactivated preflight must NOT
    // leave its previous bridge armed. Before this regression, prepared_
    // remained true and activate() falsely accepted an empty format.
    check(s.prepare({48000.,48000.,128,128,2,1,1}));
    check(!s.prepare({48000.,48000.,128,128,0,1,1}));
    check(!s.activate());
    check(!s.ready());
    check(s.format().channels==0);
    check(!s.prepare({48000.,48000.,8192,8192,2,1,1}));
    check(!s.activate());
    s.stop();
    check(s.prepare({48000.,48000.,128,128,2,2,3}));
    check(s.activate());
    check(!s.prepare({48000.,48000.,128,128,2,4,3})); // live reset forbidden
    std::array<float,8192> left{},right{};
    const float* write[2]={left.data(),right.data()};
    left[0]=std::numeric_limits<float>::quiet_NaN();
    right[17]=std::numeric_limits<float>::infinity();
    for(int b=0;b<4;++b)check(s.push(write,2,8192));
    check(!s.push(write,2,128)); // bounded ring drops newest
    const auto before=s.status();
    check(before.asrc.overflowFrames>=128);
    check(before.asrc.nonFiniteInputSamples>=8);
    check(s.ready()); // healthy producer overflow does not kill primary
    s.stop();
    check(s.prepare({48000.,48000.,128,128,2,3,4}));
    check(s.activate());
    check(!s.push(write,1,128)); // wrong callback channel count fail closed
    check(s.faulted()&&!s.ready());
    std::array<float,128> l{},r{};
    float* out[2]={l.data(),r.data()};
    l.fill(1.f);r.fill(1.f);
    check(!s.pull(out,2,128,3,4));
    check(l[44]==0.f&&r[127]==0.f);
    s.stop();
}
int main()
{
    for(double capture:{44100.,48000.,96000.})
        for(double primary:{44100.,48000.,96000.})
            oneClockPair(capture,primary);
    invalidAndOverflow();
    std::puts("IO1_CLOCK_CAPTURE_PASS 9x rates, identity epochs, FIFO, fault isolation");
}
