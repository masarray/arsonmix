// SPDX-License-Identifier: AGPL-3.0-only
// P7 measured-elevation 6-plane native room pilot. Research ONLY.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#ifndef SONKU_P7_MEASURED_ELEVATION_RESEARCH
#error P7 test must compile with measured elevation experiment enabled
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
int main(){
    using namespace armix::sonku;
    constexpr double pi=3.14159265358979323846;
    // Horizontal KEMAR is reused from shipped data EXACTLY, not
    // re-quantized into a second hidden elevation=0 table.
    for(double rate:{44100.,48000.}){
        for(float az:{0.f,45.f,90.f,135.f,180.f}){
            hrir3d::EarBank horizontal{},upper{},lower{},mirrored{};
            hrir3d::interpolate(az,0.f,rate==44100.,horizontal);
            hrir3d::interpolate(az,40.f,rate==44100.,upper);
            hrir3d::interpolate(az,-40.f,rate==44100.,lower);
            hrir3d::interpolate(-az,40.f,rate==44100.,mirrored);
            const auto& origin=rate==44100.?hrir::normal44100:hrir::normal48000;
            double diffUpper=0.,diffLower=0.;
            for(int ear=0;ear<2;++ear){
                for(int k=0;k<hrir::taps;++k){
                    const auto expected=origin[static_cast<int>(az)/5][ear][k]/
                        32768.f;
                    assert(std::abs(horizontal[ear][k]-expected)<1.e-7f);
                    assert(std::abs(upper[ear][k]-mirrored[1-ear][k])<1.e-7f);
                    diffUpper+=std::abs(upper[ear][k]-horizontal[ear][k]);
                    diffLower+=std::abs(lower[ear][k]-horizontal[ear][k]);
                }
            }
            assert(diffUpper>.005 && diffLower>.005);
        }
        const auto h=sixsurface::RoomHeight{3.2f,1.45f,1.45f};
        assert(sixsurface::validHeight(h));
        assert(!sixsurface::validHeight({2.0f,1.45f,1.45f}));
        assert(sixsurface::validHeight({3.2f,1.45f,1.65f}));
        assert(!sixsurface::validHeight({3.2f,1.45f,2.85f}));
        assert(sixsurface::fitsDelayRing(h,1.6f,rate,8192,140));
        auto floor=sixsurface::image(h,1.6f,0.f,sixsurface::Boundary::Floor,
                                      static_cast<float>(rate));
        auto ceil=sixsurface::image(h,1.6f,0.f,sixsurface::Boundary::Ceiling,
                                     static_cast<float>(rate));
        assert(floor.elevation<0.f&&ceil.elevation>0.f);
        assert(floor.delaySamples>0.f&&ceil.delaySamples>floor.delaySamples);
        assert(floor.gain>0.f&&ceil.gain>0.f);
        // Reject bad-height geometry before entering active callback.
        auto invalid=std::make_unique<SonKuEarlyReflections>();
        assert(invalid->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
        assert(!invalid->setRealtimeRoom({7.2f,7.2f,1.6f,2.f,1.f,1.f},.6f,true));
        assert(!invalid->active());
        assert(invalid->setRealtimeRoom({7.2f,7.2f,1.6f,3.2f,1.45f,1.45f},.6f,true));
        assert(invalid->active());
        assert(!invalid->setDirectionalHrir(false));
        assert(!invalid->setDirectionalContiguousHrir(false));
        // P5 still applies to BOTH new vertical reflection FIR histories.
        // Never repeat old sound when ROOM is re-activated on digital silence.
        {
            auto room=std::make_unique<SonKuEarlyReflections>();
            assert(room->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
            const SonKuRoomGeometry g{7.2f,7.2f,1.6f,3.2f,1.45f,1.45f};
            assert(room->setRealtimeRoom(g,.65f,true));
            for(int i=0;i<3800;++i){
                const float l=.1f*std::sin(static_cast<float>(2*pi*330*i/rate));
                const float r=.07f*std::cos(static_cast<float>(2*pi*725*i/rate));
                room->process(l,r,37.,60.f);
            }
            assert(room->setRealtimeRoom(g,0.f,false));
            int fade=0;
            while(room->active() && fade<12000){
                const float x=.1f*std::sin(static_cast<float>(2*pi*480*fade/rate));
                room->process(x,-x,65.,60.f);
                ++fade;
            }
            assert(!room->active()&&fade>100);
            assert(room->setRealtimeRoom(g,.65f,true));
            for(int i=0;i<2000;++i){
                const auto v=room->process(0.f,0.f,170.,60.f);
                assert(v.left==0.f&&v.right==0.f);
            }
            int first=-1;double energy=0.;
            for(int i=0;i<6800;++i){
                const float impulse=i==0?.35f:0.f;
                const auto v=room->process(impulse,impulse,170.,60.f);
                assert(std::isfinite(v.left)&&std::isfinite(v.right));
                if(first<0&&(v.left!=0.f||v.right!=0.f))first=i;
                energy+=v.left*v.left+v.right*v.right;
            }
            assert(first>100&&first<6000&&energy>1.e-8);
        }
        // P6 and P7 process the exact SAME active ROOM scene, even
        // with a stereo side conflicting in phase in the high band.
        for(float spread:{0.f,60.f}){
            auto four=std::make_unique<SonKuSpatialCore>();
            auto six=std::make_unique<SonKuSpatialCore>();
            auto chunks=std::make_unique<SonKuSpatialCore>();
            assert(four->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
            assert(six->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
            assert(chunks->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
            Settings scene{};scene.moving=true;scene.startAzimuthDegrees=45.f;
            scene.revolutionSeconds=3.f;scene.spacePreset=1;
            scene.stereoSpreadDegrees=spread;scene.roomAmount=.60f;
            scene.ambience=.20f;scene.outputTrim=.56f;
            assert(four->setSettings(scene)&&six->setSettings(scene)&&chunks->setSettings(scene));
            constexpr int total=12288;
            std::array<float,total> fl{},fr{},sl{},sr{},cl{},cr{};
            for(int n=0;n<total;++n){
                const float t=static_cast<float>(n/rate);
                const float bass=.1f*std::sin(static_cast<float>(2*pi*220*t));
                const float treble=.04f*std::sin(static_cast<float>(2*pi*3200*t));
                const float lead=.023f*std::sin(static_cast<float>(2*pi*930*t));
                fl[n]=sl[n]=cl[n]=bass+treble+lead;
                fr[n]=sr[n]=cr[n]=bass-treble+.7f*lead;
            }
            four->process(fl.data(),fr.data(),total);
            six->process(sl.data(),sr.data(),total);
            for(int at=0;at<total;){
                int n=std::min((at%2==0)?64:257,total-at);
                chunks->process(cl.data()+at,cr.data()+at,n);
                at+=n;
            }
            double energy=0.,wetDifference=0.;
            for(int n=0;n<total;++n){
                assert(std::isfinite(sl[n])&&std::isfinite(sr[n]));
                assert(std::abs(sl[n])<1.f&&std::abs(sr[n])<1.f);
                assert(std::abs(sl[n]-cl[n])<2.e-6f);
                assert(std::abs(sr[n]-cr[n])<2.e-6f);
                if(n<96){
                    // 4 existing walls and direct HRTF must be identical;
                    // no floor/ceiling reflection can arrive this early.
                    assert(fl[n]==sl[n]&&fr[n]==sr[n]);
                }
                if(n>2500)wetDifference+=std::abs(fl[n]-sl[n])+std::abs(fr[n]-sr[n]);
                energy+=sl[n]*sl[n]+sr[n]*sr[n];
            }
            assert(energy>1e-2&&wetDifference>1e-4);
            assert(std::abs(six->azimuthDegrees()-chunks->azimuthDegrees())<1e-7);
            // Full 3D room must remain safe after restart and off/on.
            assert(six->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
            assert(six->settings().spacePreset==1);
            assert(six->preparedReflectionProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch);
        }
    }
    std::puts("PASS SonKu P7 14-elevation measured KEMAR, 6-boundary image geometry, stereo PCM parity 44.1/48k");
}
