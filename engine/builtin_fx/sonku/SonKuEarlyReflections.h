// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "SonKuMeasuredHrir.h"
// The full licensed multi-elevation dataset and geometry are excluded from
// default/public builds at preprocess time: no extra DSP or data cost.
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
#include "SonKuMeasuredHrir3D.h"
#include "SonKuVerticalImageSource.h"
#endif

// Bounded first-order image-source room (Allen & Berkley JASA, 1979).
// LIGHT uses the original small ITD/ILD approximation; P6 uses measured
// horizontal KEMAR; P7 research adds physically delayed floor and ceiling
// with the measured elevation KEMAR HRIR. This is NOT proprietary dearVR BRIR.
// No allocation, locks, worker, I/O, FFT or second device callback.
namespace armix::sonku {
enum class SonKuRoomShape : std::uint8_t { Rectangular=0, Square=1 };
// Offline-only center-listener geometry. Width/depth are full room spans;
// the source moves on a horizontal ring at distanceMeters from the listener.
// No realtime mutation or native state schema is published by this experiment.
struct SonKuRoomGeometry {
    float widthMeters=7.2f,depthMeters=7.2f,distanceMeters=1.6f;
    // Research-only dimensions, no public .smix schema change.
    float heightMeters=3.2f,listenerHeightMeters=1.45f,sourceHeightMeters=1.45f;
};
// One render profile is admitted at prepare time, never switched inside PCM.
// The measured horizontal KEMAR path remains experimental until ASIO/AB FAT.
enum class SonKuReflectionProfile : std::uint8_t {
    Lightweight=0, DirectionalKemar=1, SixSurfaceKemarResearch=2
};
class SonKuEarlyReflections final {
public:
    struct Ears {float left=0.f,right=0.f;};
    static constexpr int wallCount=4; // original wall paths remain exact
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
    static constexpr int maxWallCount=6; // add floor/ceiling only to P7
#else
    static constexpr int maxWallCount=wallCount;
#endif
    static constexpr int sourceCount=2;
    static constexpr int ringFrames=8192;
    static constexpr int geometryInterval=64;
    bool prepare(double rate, SonKuReflectionProfile profile=
                 SonKuReflectionProfile::Lightweight) noexcept {
        ready_=false;
        if(profile!=SonKuReflectionProfile::Lightweight &&
           profile!=SonKuReflectionProfile::DirectionalKemar
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
           && profile!=SonKuReflectionProfile::SixSurfaceKemarResearch
#endif
           )return false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        rate_=static_cast<float>(rate);
        delaySlew_=static_cast<float>(1.-std::exp(-1./(rate*0.024)));
        gainSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.012)));
        amountSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.010)));
        kernelSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.008)));
        // Match the sample-rate tolerance admitted by both native cores; an
        // ASIO rate close to 44100 must never index the 48k measured HRIR.
        hrirBank_=(std::abs(rate-44100.)<=.1)?hrir::normal44100:hrir::normal48000;
        profile_=profile;
        // P6: select the exact P2+P3+P4 kernels before publishing this
        // instance. No runtime transition, heap, mode queue or graph fork.
        directionalHrir_=profile!=SonKuReflectionProfile::Lightweight;
        fastZeroSpread_=directionalHrir_;
        fastContiguousHrir_=directionalHrir_;
        realtimeMode_=false;
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& h:history_)h.fill(0.f);
        lowpass_.fill(0.f);
        for(auto& paths:tap_)for(auto& p:paths)p=Tap{};
        write_=0;untilGeometry_=0;primed_=false;validFrames_=0;
        amount_=0.f;
        fuseEligible_=true;
    }
    SonKuReflectionProfile preparedProfile() const noexcept{return profile_;}
    // OFFLINE A/B ONLY: existing approved room mode remains bit-exact by
    // default. Reject mode changes while active; no public state/GUI switch.
    bool setDirectionalHrir(bool enabled) noexcept {
        if(profile_!=SonKuReflectionProfile::Lightweight || active())return false;
        directionalHrir_=enabled;
        if(!enabled){fastZeroSpread_=false;fastContiguousHrir_=false;}
        fuseEligible_=true;
        untilGeometry_=0;primed_=false;
        return true;
    }
    bool directionalHrirEnabled() const noexcept{return directionalHrir_;}
    // Offline-only P3 exact-linear four-wall source fusion. Valid ONLY
    // while both emitters share the same direction (spread == 0).
    // This changes no public settings or live graph authority.
    bool setDirectionalZeroSpreadFusion(bool enabled) noexcept {
        if(profile_!=SonKuReflectionProfile::Lightweight ||
           active() || (enabled && !directionalHrir_))return false;
        fastZeroSpread_=enabled;
        fuseEligible_=true;
        return true;
    }
    bool zeroSpreadFusionEnabled() const noexcept{return fastZeroSpread_;}
    // P4 experimental, inactive-only: eliminate per-tap circular masking
    // when the delayed 140-tap HRIR FIR window is wholly in one ring span.
    // No duplicated ring storage and no HRIR coefficient truncation.
    bool setDirectionalContiguousHrir(bool enabled) noexcept {
        if(profile_!=SonKuReflectionProfile::Lightweight ||
           active() || (enabled && !directionalHrir_))return false;
        fastContiguousHrir_=enabled;
        return true;
    }
    bool contiguousHrirEnabled() const noexcept{return fastContiguousHrir_;}
    // Deliberately experimental and OFF by default. Legacy 8/8 direction
    // baseline stays bit-identical. A/B is set on an unpublished/offline core
    // until headphone, CPU and device gates approve native publication.
    bool setAmount(float value) noexcept {
        if(!std::isfinite(value)||value<0.f||value>1.f)return false;
        if(amountTarget_==0.f&&amount_==0.f&&value>0.f)reset();
        amountTarget_=value;
        return true;
    }
    // Offline experiment only. Reject shape edits while effect is active.
    bool setShape(SonKuRoomShape shape) noexcept {
        if(shape!=SonKuRoomShape::Rectangular && shape!=SonKuRoomShape::Square)
            return false;
        if(active())return false;
        shape_=shape;
        custom_=false; // restores the exact legacy square/rectangle semantics
        untilGeometry_=0;
        primed_=false;
        return true;
    }
    // Evaluated on an unpublished, inactive DSP instance only. Width up to
    // 40m is safely contained in the existing 8192-frame delay rings at 48k.
    static bool validGeometry(const SonKuRoomGeometry& g) noexcept {
        if(!std::isfinite(g.widthMeters)||!std::isfinite(g.depthMeters)||
           !std::isfinite(g.distanceMeters))return false;
        if(g.widthMeters<5.f||g.depthMeters<5.f||
           g.widthMeters>40.f||g.depthMeters>40.f||
           g.distanceMeters<1.6f||g.distanceMeters>10.f||
           2.f*(g.distanceMeters+.5f)>std::min(g.widthMeters,g.depthMeters))
            return false;
        const float longest=std::max(g.widthMeters,g.depthMeters)+g.distanceMeters;
        const float largestPath=std::sqrt(longest*longest+
                                          g.distanceMeters*g.distanceMeters);
        return (largestPath-g.distanceMeters)*48000.f/343.f+32.f <
               static_cast<float>(ringFrames-3);
    }
    bool setGeometry(const SonKuRoomGeometry& g) noexcept {
        if(active()||!validGeometry(g))return false;
        geometry_=g;
        custom_=true;
        shape_=SonKuRoomShape::Square; // consistent 0.70 walls, not rectangle
        untilGeometry_=0;
        primed_=false;
        return true;
    }
    // Caller validates geometry on CONTROL side before queuing the POD edit.
    // Audio-side update only changes targets for existing 64-frame image
    // geometry updates; old path taps smoothly move, never clear big rings.
    bool setRealtimeRoom(const SonKuRoomGeometry& g,float amount,
                         bool enabled) noexcept {
        if(!std::isfinite(amount)||amount<0.f||amount>1.f)return false;
        if(enabled && !validGeometry(g))return false;
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
        if(enabled && profile_==SonKuReflectionProfile::SixSurfaceKemarResearch &&
           !sixsurface::fitsDelayRing(
               {g.heightMeters,g.listenerHeightMeters,g.sourceHeightMeters},
               g.distanceMeters,rate_,ringFrames,hrir::taps))return false;
#endif
        if(!active() && enabled && amount>0.f){
            validFrames_=0; lowpass_.fill(0.f);
        }
        if(enabled){
            geometry_=g;custom_=true;shape_=SonKuRoomShape::Square;
        }else{
            custom_=false;shape_=SonKuRoomShape::Square;
        }
        realtimeMode_=true;
        amountTarget_=enabled?amount:0.f;
        untilGeometry_=0;
        return true;
    }
    float relativeDistanceGain() const noexcept {
        return custom_?1.6f/geometry_.distanceMeters:1.f;
    }
    SonKuRoomShape shape() const noexcept {return shape_;}
    bool active() const noexcept {return ready_&&(amount_>0.f||amountTarget_>0.f);}
    float targetAmount() const noexcept {return amountTarget_;}
    int activeWallCount() const noexcept {
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
        return profile_==SonKuReflectionProfile::SixSurfaceKemarResearch
            ?maxWallCount:wallCount;
#else
        return wallCount; // original shipping source optimizes to 4
#endif
    }
    Ears process(float left,float right,double azimuth,float spread) noexcept {
        if(!active())return {};
        if(untilGeometry_==0){
            updateGeometry(azimuth,spread);
            untilGeometry_=geometryInterval;
        }
        const float x[2]{std::isfinite(left)?left:0.f,
                         std::isfinite(right)?right:0.f};
        for(int s=0;s<sourceCount;++s){
            // Absorption on the reflected path only; direct measured HRTF
            // stays unchanged. Mild low pass preserves source intelligibility.
            lowpass_[s]+=.27f*(x[s]-lowpass_[s]);
            history_[s][write_]=.70f*x[s]+.30f*lowpass_[s];
        }
        amount_+=amountSlew_*(amountTarget_-amount_);
        // Bound shutdown to a finite 10-12 time-constant fade, then
        // restore strict bit-exact OFF/zero CPU on the next callback.
        if(std::abs(amount_-amountTarget_)<1.e-5f)amount_=amountTarget_;
        // Linearity: for identical source/image direction, FIR(H, xL) +
        // FIR(H, xR) = FIR(H, xL+xR). Stereo content need not be mono.
        // Any nonzero spread permanently disarms fusion until reset, so
        // live spread edits cannot reuse stale second-path filter state.
        if(fastZeroSpread_ && spread!=0.f)fuseEligible_=false;
        const bool fused=directionalHrir_ && fastZeroSpread_ &&
                         fuseEligible_ && spread==0.f;
        Ears output{};
        for(int s=0;s<(fused?1:sourceCount);++s){
            for(int wall=0;wall<activeWallCount();++wall){
                auto& p=tap_[s][wall];
                p.delayL+=delaySlew_*(p.targetDelayL-p.delayL);
                p.delayR+=delaySlew_*(p.targetDelayR-p.delayR);
                p.gainL+=gainSlew_*(p.targetGainL-p.gainL);
                p.gainR+=gainSlew_*(p.targetGainR-p.gainR);
                if(directionalHrir_) {
                    // Image path is common to both ears; KEMAR supplies
                    // measured ITD/ILD and directional spectral pinna cues.
                    const float delay=std::clamp(p.delayL,1.f,
                        static_cast<float>(ringFrames-hrir::taps-3));
                    const int d=static_cast<int>(delay);
                    const float frac=delay-d;
                    const int idx=(write_-d)&(ringFrames-1);
                    const auto& signal=history_[s];
                    float l=0.f,r=0.f;
                    // ROOM may have been OFF for a long time without writing
                    // new audio into history_. After reactivation never read
                    // old, pre-OFF samples as new directional reflections.
                    // The fractional-delay source requires both adjacent
                    // samples to be from the current activation epoch.
                    // Zero-filling a 64KiB ring inside the callback is banned.
                    const int freshTaps=realtimeMode_?
                        std::clamp(validFrames_-d,0,hrir::taps):hrir::taps;
                    const bool warming=freshTaps<hrir::taps;
                    // One source of truth for the coefficient slew and ear
                    // FIR arithmetic; only history address selection varies.
                    // Advance kernels even while history is warming.
                    const auto render=[&](auto&& sample) noexcept {
                        for(int k=0;k<hrir::taps;++k){
                            const float v=(warming && k>=freshTaps)
                                ?0.f:sample(k);
                            auto& hL=p.kernel[0][k];
                            auto& hR=p.kernel[1][k];
                            hL+=kernelSlew_*(p.targetKernel[0][k]-hL);
                            hR+=kernelSlew_*(p.targetKernel[1][k]-hR);
                            if(fused){
                                tap_[1][wall].kernel[0][k]=hL;
                                tap_[1][wall].kernel[1][k]=hR;
                            }
                            l+=v*hL;r+=v*hR;
                        }
                    };
                    if(fastContiguousHrir_ && idx>=hrir::taps){
                        // For 8052/8192 ring positions the entire 140-tap
                        // window is continuous; no wrap mask inside FIR.
                        const float* first=signal.data()+idx;
                        const float* second=history_[1].data()+idx;
                        render([&](int k) noexcept {
                            return fused
                                ?((1.f-frac)*(first[-k]+second[-k])+
                                    frac*(first[-k-1]+second[-k-1]))
                                :((1.f-frac)*first[-k]+frac*first[-k-1]);
                        });
                    }else{
                        // Exact, original masked circular reference path,
                        // used for all non-opt-in and near-wrap samples.
                        render([&](int k) noexcept {
                            const int at=(idx-k)&(ringFrames-1);
                            const int prev=(at-1)&(ringFrames-1);
                            return fused
                                ?((1.f-frac)*(signal[at]+history_[1][at])+
                                    frac*(signal[prev]+history_[1][prev]))
                                :((1.f-frac)*signal[at]+frac*signal[prev]);
                        });
                    }
                    if(fused){
                        auto& second=tap_[1][wall];
                        second.delayL=p.delayL;second.delayR=p.delayR;
                        second.gainL=p.gainL;second.gainR=p.gainR;
                    }
                    output.left+=p.gainL*l;
                    output.right+=p.gainR*r;
                }else{
                    output.left+=p.gainL*delayed(s,p.delayL);
                    output.right+=p.gainR*delayed(s,p.delayR);
                }
            }
        }
        write_=(write_+1)&(ringFrames-1);
        if(realtimeMode_&&validFrames_<ringFrames)++validFrames_;
        --untilGeometry_;
        // Preserve the headroom of the native direct path: no artificial
        // direction-dependent boost at FRONT or REAR.
        output.left*=amount_;
        output.right*=amount_;
        return output;
    }
private:
    struct Tap {
        float delayL=1.f,delayR=1.f,gainL=0.f,gainR=0.f;
        float targetDelayL=1.f,targetDelayR=1.f;
        float targetGainL=0.f,targetGainR=0.f;
        using EarBank=std::array<std::array<float,hrir::taps>,2>;
        EarBank kernel{},targetKernel{};
    };
    float delayed(int source,float delay) const noexcept {
        // Fractional delay interpolated in a fixed-size power-of-two ring.
        const float bounded=std::clamp(delay,1.f,static_cast<float>(ringFrames-3));
        const int integer=static_cast<int>(bounded);
        // Avoid re-reading a stale tail after the user disabled and later
        // re-enabled the ROOM. Entire 64 KiB ring never needs RT clearing.
        if(realtimeMode_&&validFrames_<=integer+1)return 0.f;
        const float fract=bounded-static_cast<float>(integer);
        const int i0=(write_-integer)&(ringFrames-1);
        const int i1=(i0-1)&(ringFrames-1);
        return history_[source][i0]*(1.f-fract)+history_[source][i1]*fract;
    }
    void updateGeometry(double azimuth,float spread) noexcept {
        constexpr float pi=3.14159265358979323846f;
        const float sourceDistance=custom_?geometry_.distanceMeters:1.60f;
        // Preserve the approved 7.2m/1.6m path exactly when custom_ is false.
        // A wider room changes physical image paths, not KEMAR azimuth signs.
        const float halfWidth=custom_?geometry_.widthMeters*.5f:
            ((shape_==SonKuRoomShape::Square)?3.60f:3.20f);
        const float halfDepth=custom_?geometry_.depthMeters*.5f:
            ((shape_==SonKuRoomShape::Square)?3.60f:4.00f);
        constexpr float speedOfSound=343.f;
        constexpr float earHalfTime=.00031f; // max +/-0.31ms
        constexpr std::array<float,wallCount> rectangular{.74f,.74f,.67f,.67f};
        constexpr std::array<float,wallCount> square{.70f,.70f,.70f,.70f};
        const auto& walls=(shape_==SonKuRoomShape::Square)?square:rectangular;
        for(int source=0;source<sourceCount;++source){
            const float degrees=static_cast<float>(azimuth)
                +(source==0?-.5f:.5f)*spread;
            const float rad=degrees*(pi/180.f);
            const float sx=sourceDistance*std::sin(rad);
            const float sy=sourceDistance*std::cos(rad);
            const std::array<std::array<float,2>,wallCount> images{{
                {{-2.f*halfWidth-sx,sy}},{{2.f*halfWidth-sx,sy}},
                {{sx,2.f*halfDepth-sy}},{{sx,-2.f*halfDepth-sy}}
            }};
            for(int wall=0;wall<wallCount;++wall){
                const float x=images[wall][0],y=images[wall][1];
                const float path=std::sqrt(x*x+y*y);
                const float directionX=x/path;
                const float extra=(path-sourceDistance)*rate_/speedOfSound;
                // Constant 0.31ms ear baseline makes differential ITD
                // symmetric while keeping every reflection causal.
                const float common=extra+(directionalHrir_?0.f:earHalfTime*rate_);
                const float earDelta=directionX*earHalfTime*rate_;
                // New distance studies use fixed 1.6m acoustic reference:
                // pressure at a reflected path falls with 1/path. Legacy
                // calculations are untouched at the accepted radius.
                const float level=.28f*walls[wall]*
                                  ((custom_?1.6f:sourceDistance)/path);
                auto& tap=tap_[source][wall];
                tap.targetDelayL=std::clamp(common+(directionalHrir_?0.f:earDelta),1.f,
                                           static_cast<float>(ringFrames-3));
                tap.targetDelayR=std::clamp(common-(directionalHrir_?0.f:earDelta),1.f,
                                           static_cast<float>(ringFrames-3));
                tap.targetGainL=level*(directionalHrir_?1.f:(1.f-.13f*directionX));
                tap.targetGainR=level*(directionalHrir_?1.f:(1.f+.13f*directionX));
                if(directionalHrir_){
                    // The IMAGE-source arrival angle, NOT source azimuth.
                    const float dir=std::atan2(x,y)*180.f/pi;
                    const float pos=std::abs(dir)/hrir::spacingDegrees;
                    const int lo=std::clamp(static_cast<int>(pos),0,
                        hrir::directions-1);
                    const int hi=std::min(lo+1,hrir::directions-1);
                    const float t=pos-lo;
                    constexpr float scale=1.f/32768.f;
                    for(int ear=0;ear<2;++ear){
                        const int measured=dir<0.f?1-ear:ear;
                        for(int k=0;k<hrir::taps;++k){
                            const float value=scale*
                                ((1.f-t)*hrirBank_[lo][measured][k]+
                                  t*hrirBank_[hi][measured][k]);
                            tap.targetKernel[ear][k]=value;
                            if(!primed_)tap.kernel[ear][k]=value;
                        }
                    }
                }
                if(!primed_){
                    tap.delayL=tap.targetDelayL;
                    tap.delayR=tap.targetDelayR;
                    tap.gainL=tap.targetGainL;
                    tap.gainR=tap.targetGainR;
                }
            }
        }
#if defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
        if(profile_==SonKuReflectionProfile::SixSurfaceKemarResearch){
            const sixsurface::RoomHeight heights{
                geometry_.heightMeters,geometry_.listenerHeightMeters,
                geometry_.sourceHeightMeters};
            for(int source=0;source<sourceCount;++source){
                const float sourceAz=static_cast<float>(azimuth)+
                    (source==0?-.5f:.5f)*spread;
                for(int i=0;i<2;++i){
                    const auto boundary=i==0?sixsurface::Boundary::Floor:
                                               sixsurface::Boundary::Ceiling;
                    const auto arrival=sixsurface::image(
                        heights,sourceDistance,sourceAz,boundary,rate_);
                    auto& p=tap_[source][wallCount+i];
                    // Measured HRIR already contains directional ITD/ILD.
                    // No invented extra ear delay, EQ or pseudo elevation.
                    const float delay=std::clamp(arrival.delaySamples,1.f,
                        static_cast<float>(ringFrames-hrir::taps-3));
                    p.targetDelayL=p.targetDelayR=delay;
                    p.targetGainL=p.targetGainR=arrival.gain;
                    hrir3d::interpolate(arrival.azimuth,arrival.elevation,
                                        std::abs(rate_-44100.f)<.1f,
                                        p.targetKernel);
                    if(!primed_){
                        p.kernel=p.targetKernel;
                        p.delayL=p.delayR=delay;
                        p.gainL=p.gainR=arrival.gain;
                    }
                }
            }
        }
#endif
        primed_=true;
    }
    static_assert((ringFrames&(ringFrames-1))==0);
    std::array<std::array<float,ringFrames>,sourceCount> history_{};
    std::array<std::array<Tap,maxWallCount>,sourceCount> tap_{};
    std::array<float,sourceCount> lowpass_{};
    using Bank=const std::int16_t (*)[2][hrir::taps];
    Bank hrirBank_=nullptr;
    float rate_=0.f,delaySlew_=0.f,gainSlew_=0.f,amountSlew_=0.f,kernelSlew_=0.f;
    float amount_=0.f,amountTarget_=0.f;
    int write_=0,untilGeometry_=0,validFrames_=0;
    bool realtimeMode_=false;
    SonKuRoomShape shape_=SonKuRoomShape::Rectangular;
    SonKuRoomGeometry geometry_{};
    bool custom_=false,ready_=false,primed_=false;
    bool directionalHrir_=false;
    bool fastZeroSpread_=false,fuseEligible_=true;
    bool fastContiguousHrir_=false;
    SonKuReflectionProfile profile_=SonKuReflectionProfile::Lightweight;
};
} // namespace armix::sonku
