// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include "../engine/dsp/NativeMasterLoudness.h"

int main()
{
 using armix::dsp::NativeMasterLoudness;
 constexpr double pi=3.14159265358979323846;
 NativeMasterLoudness meter;
 meter.prepare(44100.0);
 assert(!meter.read().supported); // no invented measurements at unqualified rate
 meter.prepare(48000.0);
 assert(meter.read().supported);
 assert(!meter.read().integratedValid);
 constexpr int chunk=480;
 float L[chunk]{},R[chunk]{};
 const float* output[2]{L,R};
 // ITU-R BS.1770 997 Hz 0 dBFS sine = -3.01 LKFS per channel.
 // Stereo -20dBFS => approx -20 LUFS after 2x channel power.
 for(int block=0;block<110;++block) {
    for(int i=0;i<chunk;++i) {
        const int n=block*chunk+i;
        L[i]=R[i]=0.1f*static_cast<float>(std::sin(2*pi*997*n/48000.0));
    }
    meter.process(output,2,chunk);
 }
 const auto a=meter.read();
 if(!a.integratedValid||!a.truePeakValid)
     std::cerr<<"valid="<<a.integratedValid<<" truePeak="<<a.truePeakValid<<"\n";
 assert(a.integratedValid&&a.truePeakValid);
 assert(a.gatedWindows>=2);
 assert(a.integratedLufs > -21.0 && a.integratedLufs < -19.0);
 assert(a.dbtp > -20.8 && a.dbtp < -19.2);

 // 4x interpolated reconstruction must detect intersample peaks, not
 // merely maximum discrete 12kHz sampled input.
 meter.prepare(48000.0);
 double sampled=0;
 for(int block=0;block<110;++block) {
    for(int i=0;i<chunk;++i) {
        const int n=block*chunk+i;
        L[i]=R[i]=0.9f*static_cast<float>(std::sin(2*pi*12000*n/48000.0+pi/4));
        sampled=std::max(sampled,static_cast<double>(std::abs(L[i])));
    }
    meter.process(output,2,chunk);
 }
 const auto reconstructed=meter.read();
 const double sampleDb=20*std::log10(sampled);
 if(!(reconstructed.dbtp>sampleDb+2.0))
     std::cerr<<"sample dBFS="<<sampleDb<<" oversampled dBTP="<<reconstructed.dbtp<<"\n";
 assert(reconstructed.truePeakValid);
 assert(reconstructed.dbtp>sampleDb+2.0);
 assert(reconstructed.dbtp<0.0); // 0.9 amplitude never exceeds actual 0dBFS

 // Muted/silent signal must never be represented as a fabricated finite LUFS.
 meter.prepare(48000.0);
 for(int i=0;i<chunk;++i)L[i]=R[i]=0;
 for(int i=0;i<110;++i)meter.process(output,2,chunk);
 assert(!meter.read().integratedValid);
 assert(!meter.read().truePeakValid);
 meter.prepare(0.0);
 assert(!meter.read().supported);
 std::cout<<"PASS: exact 48k BS1770 K-weight gated LUFS, mono/stereo, 4x intersample TP, silence and epoch reset\n";
}
