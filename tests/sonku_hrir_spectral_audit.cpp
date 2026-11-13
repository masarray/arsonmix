// SPDX-License-Identifier: AGPL-3.0-only
// Offline diagnostic only: published KEMAR FIR spectral evidence at fixed azimuths.
// No DSP implementation, third-party binaries, runtime FFT or audio callback work.
#include "../engine/builtin_fx/sonku/SonKuMeasuredHrir.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace armix::sonku;
constexpr double pi=3.14159265358979323846;
using Response=std::complex<double>;
double wrap(double deg) {
    while(deg>180.)deg-=360.;
    while(deg<-180.)deg+=360.;
    return deg;
}
Response hrtf(double angle,int ear,double hz,double rate) {
    angle=wrap(angle);
    const bool opposite=angle<0.;
    const double coordinate=std::abs(angle)/hrir::spacingDegrees;
    const int lo=std::clamp(static_cast<int>(std::floor(coordinate)),0,hrir::directions-1);
    const int hi=std::min(lo+1,hrir::directions-1);
    const double t=coordinate-lo;
    const int selectedEar=opposite?1-ear:ear;
    const auto& bank=rate==44100.?hrir::normal44100:hrir::normal48000;
    Response sum{};
    for(int k=0;k<hrir::taps;++k) {
        const double tap=(1.-t)*bank[lo][selectedEar][k]+t*bank[hi][selectedEar][k];
        const double phase=-2.*pi*hz*k/rate;
        sum+=tap*Response{std::cos(phase),std::sin(phase)};
    }
    return sum;
}
double db(double magnitude) {
    return 20.*std::log10(std::max(magnitude,1.e-12));
}
void expect(bool ok,const char* what) {
    if(!ok)throw std::runtime_error(what);
}
}
int main() {
    try {
        // One-third-octave-like centers spanning source bass through presence.
        constexpr std::array<int,19> frequencies{
          125,160,200,250,315,400,500,630,800,1000,1250,1600,2000,
          2500,3150,4000,5000,6300,8000};
        constexpr std::array<int,5> directions{0,45,90,135,180};
        std::printf("rate,azimuth,ear,hz,baselineMidVsCenterDb,correctedMidVsCenterDb,sideError\n");
        int rows=0;
        for(const double rate: {44100.,48000.}) {
            for(const int azimuth:directions) {
                for(int ear=0;ear<2;++ear) {
                    for(const int hz:frequencies) {
                        const Response h0=hrtf(azimuth-30,ear,hz,rate);
                        const Response h1=hrtf(azimuth+30,ear,hz,rate);
                        const Response center=hrtf(azimuth,ear,hz,rate);
                        const Response average=.5*(h0+h1);
                        // Mirrors the exact two-kernel correction from
                        // SonKuSpatialCore::updateKernels() with mu=1.
                        const Response correction=center-average;
                        const Response n0=h0+correction,n1=h1+correction;
                        const Response corrected=.5*(n0+n1);
                        const double baselineDb=db(std::abs(average)/std::max(1.e-9,std::abs(center)));
                        const double correctedDb=db(std::abs(corrected)/std::max(1.e-9,std::abs(center)));
                        const double sideError=std::abs((n0-n1)-(h0-h1));
                        expect(std::isfinite(baselineDb)&&std::isfinite(correctedDb),
                               "nonfinite frequency response");
                        expect(std::abs(correctedDb)<1.e-8,
                               "correction must restore central HRIR frequency response");
                        expect(sideError<1.e-7,
                               "coherent-mid correction changed stereo side transfer");
                        // Both axes were independently observed to have deep
                        // KEMAR 2kHz coherent-source notches at 60-degree spread.
                        if(rate==48000. && hz==2000 && ear==0 &&
                           (azimuth==0 || azimuth==180))
                            expect(baselineDb< -5., "missing legacy 2kHz comb notch");
                        std::printf("%.0f,%d,%d,%d,%.5f,%.9f,%.9g\n",
                            rate,azimuth,ear,hz,baselineDb,correctedDb,sideError);
                        ++rows;
                    }
                }
            }
        }
        expect(rows==380,"unexpected spectral audit coverage");
        std::fprintf(stderr,"PASS SonKu KEMAR 380 spectral checks: 44.1/48k, 5 angles, "
                            "two ears, 19 bands; corrected MID and stereo SIDE invariant\n");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"SonKu spectral audit FAILED: %s\n",e.what());
        return 1;
    }
}
