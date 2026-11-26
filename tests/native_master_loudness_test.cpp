// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include "../engine/dsp/NativeMasterLoudness.h"
#include "../engine/dsp/NativeMasterAnalysisTap.h"
#include <memory>
#include <thread>
#include <chrono>

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
 // Deterministic queue saturation: reject without overwrite; exact mono,
 // stereo and null-channel PCM remain ordered across generations.
 armix::dsp::MasterAnalysisQueue<4,8> q;
 float v[8]{1,2,3,4,5,6,7,8};const float* pair[2]{v,nullptr};
 for(unsigned i=0;i<4;++i)assert(q.push(i+1,pair,2,8));
 assert(!q.push(99,pair,2,8));
 for(unsigned i=0;i<4;++i)assert(q.consume([&](const auto& b){
     assert(b.generation==i+1&&b.channels==2&&b.frames==8);
     for(int j=0;j<8;++j)assert(b.pcm[0][j]==v[j]&&b.pcm[1][j]==0.f);
 }));
 assert(!q.consume([](const auto&){}));
 assert(q.push(9,pair,1,3));
 assert(q.consume([](const auto& b){assert(b.generation==9&&b.channels==1&&b.frames==3);}));
 // Actual threaded path, same analyzer + PCM as direct reference. Use
 // bounded pacing and an explicit coverage check, not CI timing claims.
 auto tap=std::make_unique<armix::dsp::NativeMasterAnalysisTap>();
 tap->prepare(48000.0);meter.prepare(48000.0);
 std::uint64_t frames=0;
 for(int block=0;block<110;++block){
    for(int i=0;i<chunk;++i){int n=block*chunk+i;
        L[i]=0.15f*static_cast<float>(std::sin(2*pi*997*n/48000.0));
        R[i]=0.3f*static_cast<float>(std::sin(2*pi*12000*n/48000.0+pi/4));}
    meter.process(output,2,chunk);tap->publish(output,2,chunk);frames+=chunk;
    if(block%8==0)std::this_thread::sleep_for(std::chrono::milliseconds(3));
 }
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 auto reading=tap->read();
 while(reading.processedFrames!=frames && std::chrono::steady_clock::now()<deadline){
    std::this_thread::sleep_for(std::chrono::milliseconds(2));reading=tap->read();
 }
 const auto reference=meter.read();
 assert(reading.processedFrames==frames&&reading.droppedFramesLifetime==0);
 assert(reading.integratedValid&&reading.truePeakValid&&reading.gatedWindows==reference.gatedWindows);
 assert(reading.integratedLufs==reference.integratedLufs&&reading.dbtp==reference.dbtp);
 tap->prepare(44100.0);assert(!tap->read().supported&&!tap->read().integratedValid);
 tap->prepare(48000.0);assert(!tap->read().integratedValid&&!tap->read().truePeakValid);
 std::cout<<"PASS: exact 48k BS1770 K-weight gated LUFS, mono/stereo, 4x intersample TP, silence and epoch reset\n";
}
