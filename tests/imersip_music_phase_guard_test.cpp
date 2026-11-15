// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipDsp.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error ImerSip requires actual SonKu P8.2 six-wall/three-band room
#endif
#if !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH
#error ImerSip must use transparent P8.2 bass foundation
#endif
#if !defined(SONKU_P75_MUSIC_SAFE_RESEARCH) || !SONKU_P75_MUSIC_SAFE_RESEARCH
#error ImerSip must use P8.2 preserved music SIDE
#endif
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr int n=16384;
using armix::imersip::ImerSipDsp;
using armix::imersip::Settings;
struct Stereo {
    std::vector<float> l,r;
    explicit Stereo(int count=n):l(count),r(count){}
};
double rms(const Stereo& a,int start,bool side) {
    double e=0.;
    for(int i=start;i<static_cast<int>(a.l.size());++i) {
        const double v=.5*(static_cast<double>(a.l[i])+
            (side?-1.:1.)*a.r[i]);
        e+=v*v;
    }
    return std::sqrt(e/static_cast<double>(a.l.size()-start));
}
void makeSignal(Stereo& a,double rate,int fixture,float hz=120.f) {
    for(int i=0;i<static_cast<int>(a.l.size());++i) {
        const double t=static_cast<double>(i)/rate;
        const float v=.16f*static_cast<float>(std::sin(2.*pi*hz*t))+
                      .045f*static_cast<float>(std::sin(2.*pi*1270.*t));
        const float d=.08f*static_cast<float>(std::sin(2.*pi*733.*t));
        if(fixture==0){a.l[i]=v;a.r[i]=v;}          // mono voice
        if(fixture==1){a.l[i]=v;a.r[i]=-v;}         // full anti-phase
        if(fixture==2){a.l[i]=v;a.r[i]=0.f;}        // left hard pan
        if(fixture==3){a.l[i]=0.f;a.r[i]=v;}        // right hard pan
        if(fixture==4){a.l[i]=v;a.r[i]=.5f*v+d;}    // independent stereo
        if(fixture==5){a.l[i]=v;a.r[i]=-.48f*v+d;}  // weak negative correlation
    }
}
Stereo render(double rate,const Settings& s,const Stereo& input,int chunk=128,
              bool p87=true,bool concertStage=true) {
    Stereo result=input;
    auto dsp=std::make_unique<ImerSipDsp>(p87,concertStage);
    assert(dsp->setSettings(s) && dsp->prepare(rate));
    assert(dsp->measuredStereoStageEnabled());
    for(int at=0;at<static_cast<int>(result.l.size());at+=chunk)
        dsp->process(result.l.data()+at,result.r.data()+at,
                     std::min(chunk,static_cast<int>(result.l.size())-at));
    return result;
}
void assertChunkParity(const Stereo& a,const Stereo& b) {
    assert(a.l.size()==b.l.size());
    for(int i=0;i<static_cast<int>(a.l.size());++i) {
        assert(std::isfinite(a.l[i])&&std::isfinite(a.r[i]));
        assert(std::abs(a.l[i]-b.l[i])<2.e-6f);
        assert(std::abs(a.r[i]-b.r[i])<2.e-6f);
    }
}
void verifySceneGeometry() {
    Settings s{};
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-95.92f)<.02f);
    s.width=1.f;
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-126.f)<.01f);
    s.width=0.f;
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-32.f)<.01f);
    for(int venue=0;venue<3;++venue) {
        s.venue=venue;
        auto scene=armix::imersip::sceneFor(s);
        assert(!scene.moving && scene.startAzimuthDegrees==0.f);
        assert(scene.spacePreset>=2);
        assert(scene.spatialMix>0.f && scene.outputTrim==1.f);
        assert(std::abs(scene.sourceDistanceMeters-(1.6f+5.8f*s.depth))<1.e-4f);
        assert(scene.roomAmount>scene.ambience);
        // P0 had a near-bypassed real room (~0.05 * core return).
        // Increased actual room-gain is intentional; its *processed PCM*
        // must now pass the robust dry/wet null-test below.
        assert(scene.roomAmount<=.90f && scene.ambience<=.58f);
    }
}
void verifyNearOrchestraRoomContract() {
    for(int venue=0;venue<3;++venue)for(float amount:{0.f,.45f,1.f}) {
        Settings s{};s.venue=venue;s.space=amount;s.immersion=1.f;
        const auto room=armix::imersip::sceneFor(s);
        assert(std::abs(room.sourceDistanceMeters-(1.6f+5.8f*s.depth))<1.e-4f);
        // Early and late are INDEPENDENT. At Early=0 no phantom
        // floor/wall returns may survive, even if FDN late is active.
        if(amount==0.f)assert(room.roomAmount==0.f);
        else assert(room.roomAmount>room.ambience);
        assert(room.spacePreset>=2);
        s.immersion=0.f;
        const auto dry=armix::imersip::sceneFor(s);
        assert(std::abs(dry.sourceDistanceMeters-room.sourceDistanceMeters)<1.e-4f);
        assert(dry.spacePreset==0&&dry.roomAmount==0.f&&dry.ambience==0.f);
    }
}
void verifyDryAndBounds(double rate) {
    Settings bad{};bad.venue=3;assert(!armix::imersip::valid(bad));
    bad={};bad.width=2.f;assert(!armix::imersip::valid(bad));
    bad={};bad.immersion=std::numeric_limits<float>::quiet_NaN();
    assert(!armix::imersip::valid(bad));
    Settings s{};s.immersion=0.f;s.outputTrim=1.f;
    assert(armix::imersip::sceneFor(s).spacePreset==0);
    Stereo source(4096);makeSignal(source,rate,4);
    const auto dry=render(rate,s,source);
    for(int i=0;i<4096;++i) {
        assert(std::abs(dry.l[i]-source.l[i])<1.e-5f);
        assert(std::abs(dry.r[i]-source.r[i])<1.e-5f);
    }
    auto badRate=std::make_unique<ImerSipDsp>();
    assert(!badRate->prepare(96000.));
}
void verifyFrontSpeakerVocalDoesNotNull(double rate) {
    // This is the regression v1 missed: an algebraic phase safety test
    // against an already-boxy P8.2 output falsely certified a bad vocal.
    // In v2 the reference is the ACTUAL DRY source, not that HRTF output.
    for(int venue=0;venue<3;++venue)for(int fixture=0;fixture<6;++fixture) {
        Stereo source;makeSignal(source,rate,fixture,450.f);
        Settings s{};s.venue=venue;s.width=.95f;
        s.space=1.f;s.immersion=1.f;s.bassImpact=0.f;s.detail=0.f;s.outputTrim=1.f;
        auto wet=render(rate,s,source,128);
        assertChunkParity(wet,render(rate,s,source,64));
        assertChunkParity(wet,render(rate,s,source,8192));
        double midDiff=0.,midInput=0.;
        for(int i=4096;i<n;++i) {
            const double a=.5*(source.l[i]+source.r[i]);
            const double b=.5*(wet.l[i]+wet.r[i]);
            midDiff+=(a-b)*(a-b);midInput+=a*a;
        }
        // A real frontal HRTF MUST affect MID, so prior near-bit-exact
        // dry-vocal test was a false externalization acceptance condition.
        // Still guard against catastrophic spectral/phase vocal nulls.
        if(midInput>1.e-8)assert(midDiff/midInput<1.0);
        if(fixture==0) {
            const double ref=rms(source,4096,false),now=rms(wet,4096,false);
            assert(now>ref*.70 && now<ref*1.25);
            assert(rms(wet,4096,true)<ref*.45+1.e-6);
        }
    }
}
void verifyOriginalStereoSurvives(double rate) {
    for(int venue=0;venue<3;++venue)
        for(int fixture=1;fixture<6;++fixture) {
            Stereo source;makeSignal(source,rate,fixture);
            Settings s{};s.venue=venue;s.width=1.f;
            s.space=1.f;s.immersion=1.f;s.bassImpact=0.f;
            const auto wide=render(rate,s,source);
            const auto tiny=render(rate,s,source,64);
            assertChunkParity(wide,tiny);
            const double ref=rms(source,4096,true);
            const double now=rms(wide,4096,true);
            // Preserve all source L/R differentiation with room active.
            assert(now>ref*.86); // owner rejects near-mono/washed stereo
            assert(now<ref*1.52+.00001);
            if(fixture==1)assert(now>.035);
        }
}
void verifyRealP86DirectSpeakerReachesOutput(double rate) {
    // The old plugin only exposed P8.2 room returns and discarded its HRTF
    // direct sound. Require a real dual-speaker DIRECT signal even with ROOM
    // completely OFF and prove the full ImerSip render is not near-dry.
    using Core=armix::sonku::SonKuSpatialCore;
    auto core=std::make_unique<Core>();
    using Profile=armix::sonku::SonKuReflectionProfile;
    assert(core->prepare(rate,Profile::SixSurfaceKemarResearch));
    armix::sonku::Settings scene{};
    scene.moving=false;scene.stereoSpreadDegrees=100.f;
    scene.spatialMix=1.f;scene.outputTrim=1.f;scene.spacePreset=0;
    assert(core->setSettings(scene));
    assert(core->enableImerSipMeasuredStereoSide(true));
    Stereo impulse(1024);impulse.l[0]=.16f;
    std::vector<Core::RoomReturns> direct(1024);
    core->process(impulse.l.data(),impulse.r.data(),1024,direct.data());
    double sentToOtherEar=0.,sentToNearEar=0.,wetWithRoomOff=0.;
    for(const auto& x:direct) {
        sentToNearEar+=std::abs(x.directL);
        sentToOtherEar+=std::abs(x.directR);
        wetWithRoomOff+=std::abs(x.earlyL)+std::abs(x.earlyR)+
                        std::abs(x.lateL)+std::abs(x.lateR);
    }
    assert(sentToNearEar>.001 && sentToOtherEar>.001);
    assert(wetWithRoomOff==0.);
    // ImerSip 100% with REAL P8.6 horizontal focus must render a
    // binaural DIRECT field, not just add reverb to original headphone L/R.
    Stereo source(16384);makeSignal(source,rate,4,370.f);
    Settings s{};s.immersion=1.f;s.width=1.f;
    s.space=0.f;s.detail=0.f;s.bassImpact=0.f;
    const auto renderFull=render(rate,s,source);
    double directDifference=0.,dryEnergy=0.;
    for(int i=4096;i<16384;++i) {
        const double dl=renderFull.l[i]-source.l[i];
        const double dr=renderFull.r[i]-source.r[i];
        directDifference+=dl*dl+dr*dr;
        dryEnergy+=static_cast<double>(source.l[i])*source.l[i]+
                   static_cast<double>(source.r[i])*source.r[i];
    }
    const double relative=std::sqrt(directDifference/(dryEnergy+1.e-14));
    std::fprintf(stderr,"IMERSIP_P86_DIRECT rate=%.0f transfer=%.5f\n",rate,relative);
    assert(relative>.15); // rejects the old effectively dry stereo base
}

void verifyP87MusicalAirBodyRealIntegration(double rate) {
    using armix::sonku::p87::MusicalAirBody;
    MusicalAirBody helper;
    constexpr int n=16384;
    double priorAir8=0.;
    for(float hz:{85.f,1000.f,8000.f,12000.f}) {
        assert(helper.prepare(rate));
        double src=0.,air=0.,body=0.;
        for(int i=0;i<n;++i) {
            const float x=.10f*std::sin(2.*pi*hz*i/rate);
            float l=x,r=x;
            helper.enhanceDirect(l,r);
            const auto b=helper.diffuseLowBody(x,x);
            if(i>8192) {src+=x*x;air+=l*l;body+=(x+b.left)*(x+b.left);}
        }
        const double ar=std::sqrt(air/src),br=std::sqrt(body/src);
        if(hz==85.f)assert(br>1.30&&br<1.46);
        if(hz==8000.f) {assert(ar>1.13&&ar<1.28);priorAir8=ar;}
        // Matches P8.7 source's transfer property: smooth increasing
        // shelf, NOT an invented 12kHz absolute floor.
        if(hz==12000.f)assert(ar>priorAir8&&ar<1.29);
        if(hz==8000.f)assert(br<1.05);
        std::fprintf(stderr,
            "IMERSIP_P87_FILTER rate=%.0f hz=%.0f air=%.5f body=%.5f\\n",
            rate,static_cast<double>(hz),ar,br);
    }
    // The real callback must differ from the matched P8.7-disabled A/B.
    Settings s{};s.immersion=1.f;s.space=1.f;
    s.width=1.f;s.detail=0.f;s.bassImpact=0.f;
    Stereo music(32768);
    for(int i=0;i<32768;++i){
        const float t=static_cast<float>(i/rate);
        const float x=.080f*std::sin(2.*pi*8100.*t)+
                      .035f*std::sin(2.*pi*83.*t);
        const float y=.020f*std::sin(2.*pi*4700.*t);
        music.l[i]=x+y;music.r[i]=x-y;
    }
    const auto old=render(rate,s,music,128,false);
    const auto improved=render(rate,s,music,128,true);
    const auto chunks=render(rate,s,music,64,true);
    assertChunkParity(improved,chunks);
    double delta=0.,e=0.;
    for(int i=4096;i<32768;++i) {
        const double dl=improved.l[i]-old.l[i];
        const double dr=improved.r[i]-old.r[i];
        delta+=dl*dl+dr*dr;
        e+=music.l[i]*music.l[i]+music.r[i]*music.r[i];
    }
    const double rel=std::sqrt(delta/(e+1.e-14));
    std::fprintf(stderr,"IMERSIP_P87_LIVE_PCM rate=%.0f relDelta=%.5f\n",rate,rel);
    assert(rel>.006 && rel<.30);
    s.immersion=0.f;
    const auto dry=render(rate,s,music,128,true);
    for(int i=0;i<32768;++i) {
        assert(std::abs(dry.l[i]-music.l[i])<1.e-5f);
        assert(std::abs(dry.r[i]-music.r[i])<1.e-5f);
    }
}

void verifyConcertStageLateralReflection(double rate) {
    using armix::imersip::ConcertStage;
    auto band=std::make_unique<ConcertStage>();
    assert(band->prepare(rate));
    band->setScene(1.f,1.f,1.f);
    assert(std::abs(band->targetGain()-4.f)<1.e-5f);
    // Mono early room adds NO synthetic SIDE; source MID is untouched.
    for(int i=0;i<512;++i) {
        const auto s=band->process(.1f,.1f);
        assert(s.left==0.f&&s.right==0.f);
    }
    // Stronger 250-3800 Hz true lateral early band than subbass/air:
    // 1 kHz contributes lively concert-stage width but not low punch or
    // additional 8-12 kHz diffuse brightness on top of P8.7 direct air.
    auto amplitude=[rate](float hz) {
        armix::imersip::ConcertStage filter;
        assert(filter.prepare(rate));
        filter.setScene(1.f,1.f,1.f);
        filter.reset();
        constexpr int frames=16384;
        double input=0.,extra=0.;
        for(int i=0;i<frames;++i) {
            const float x=.1f*std::sin(2.*pi*hz*i/rate);
            const auto ears=filter.process(x,-x);
            assert(std::isfinite(ears.left)&&std::isfinite(ears.right));
            assert(std::abs(ears.left+ears.right)<1.e-7f);
            if(i>4096){input+=x*x;extra+=ears.left*ears.left;}
        }
        return std::sqrt(extra/(input+1.e-14));
    };
    const double sub=amplitude(75.f);
    const double presence=amplitude(1000.f);
    const double air=amplitude(10000.f);
    std::fprintf(stderr,
        "IMERSIP_CONCERT_BAND rate=%.0f sub=%.5f width1k=%.5f air=%.5f\n",
        rate,sub,presence,air);
    assert(presence>3.0 && presence<3.9);
    // Two poles attenuate both bass and upper reverb much more than the
    // previous broad one-pole passband: NO fake LF or doubled 10kHz wash.
    assert(sub<presence*.20 && air<presence*.30);

    Stereo music(32768);makeSignal(music,rate,4,450.f);
    Settings s{};s.venue=0;s.immersion=1.f;s.space=1.f;
    s.width=1.f;s.bassImpact=0.f;s.detail=0.f;
    const auto baseline=render(rate,s,music,128,true,false);
    const auto stage=render(rate,s,music,128,true,true);
    assertChunkParity(stage,render(rate,s,music,64,true,true));
    assertChunkParity(stage,render(rate,s,music,8192,true,true));
    double effect=0.,source=0.;
    for(int i=4096;i<32768;++i) {
        const double dl=stage.l[i]-baseline.l[i];
        const double dr=stage.r[i]-baseline.r[i];
        effect+=dl*dl+dr*dr;
        source+=music.l[i]*music.l[i]+music.r[i]*music.r[i];
    }
    const double change=std::sqrt(effect/(source+1.e-14));
    std::fprintf(stderr,
        "IMERSIP_CONCERT_REAL_P86_ER rate=%.0f delta=%.7f\n",rate,change);
    // Reject the prior technically-PASS but practically near-bypassed
    // 0.004 RMS lateral enhancement. Preserve upper bound/headroom.
    assert(change>.012 && change<.12);
    // At familiar default 82% IMMERSION and 65% VENUE SPACE the actual
    // audio must still have a non-negligible lateral contribution.
    const Settings defaults{};
    const auto defaultDry=render(rate,defaults,music,128,true,false);
    const auto defaultStage=render(rate,defaults,music,128,true,true);
    double d=0.,q=0.;
    for(int i=4096;i<32768;++i) {
        const double dl=defaultStage.l[i]-defaultDry.l[i];
        const double dr=defaultStage.r[i]-defaultDry.r[i];
        d+=dl*dl+dr*dr;
        q+=music.l[i]*music.l[i]+music.r[i]*music.r[i];
    }
    const double defaultDelta=std::sqrt(d/(q+1.e-14));
    std::fprintf(stderr,
        "IMERSIP_CONCERT_DEFAULT rate=%.0f delta=%.7f\n",rate,defaultDelta);
    assert(defaultDelta>.004 && defaultDelta<.09);

    // Turning off the VENUE SPACE knob must remove this stage, while
    // leaving the P8.7 direct binaural effect and native stage intact.
    s.space=0.f;
    const auto roomZero=render(rate,s,music,128,true,true);
    assertChunkParity(roomZero,render(rate,s,music,128,true,false));
    // Width=0 also gates only this early-lateral enhancement, not the
    // original measured front-speaker engine.
    s.space=1.f;s.width=0.f;
    const auto narrow=render(rate,s,music,128,true,true);
    assertChunkParity(narrow,render(rate,s,music,128,true,false));
}

void verifyStereoIntegrityOwnerRejection(double rate) {
    // The old test only required 45% original SIDE and thus CERTIFIED
    // near-mono. Test BOTH full immersive and defaults against actual source.
    for(int venue=0;venue<3;++venue)
    for(int fixture:{1,2,3,4,5}) {
        Stereo source(32768);makeSignal(source,rate,fixture,441.f);
        Settings s{};s.venue=venue;s.width=1.f;s.immersion=1.f;
        s.space=1.f;s.lateRoom=1.f;s.stereoLock=1.f;
        s.bassImpact=0.f;s.detail=0.f;s.outputTrim=1.f;
        const auto out=render(rate,s,source,128);
        const double before=rms(source,4096,true);
        const double after=rms(out,4096,true);
        const double retained=after/(before+1.e-12);
        std::fprintf(stderr,
            "IMERSIP_STEREO_LOCK rate=%.0f venue=%d fixture=%d ratio=%.5f\n",
            rate,venue,fixture,retained);
        assert(retained>.86 && retained<1.75);
        // Dedicated control must have real audio influence. At minimum
        // lock the original SIDE; without lock, deliberately more HRTF SIDE.
        s.stereoLock=0.f;
        const auto free=render(rate,s,source,128);
        double difference=0.,original=0.;
        for(int i=4096;i<32768;++i) {
            const double dl=free.l[i]-out.l[i];
            const double dr=free.r[i]-out.r[i];
            difference+=dl*dl+dr*dr;
            original+=source.l[i]*source.l[i]+source.r[i]*source.r[i];
        }
        assert(std::sqrt(difference/(original+1.e-14))>.01);
    }
    // Separate LATE TAIL must affect the actual PCM without mutating
    // early lateral band or source SIDE. Same full source and other knobs.
    Settings s{};s.immersion=1.f;s.space=.8f;s.lateRoom=0.f;
    Stereo music(32768);makeSignal(music,rate,4);
    const auto noLate=render(rate,s,music);
    s.lateRoom=1.f;
    const auto yesLate=render(rate,s,music);
    double diff=0.;
    for(int i=4096;i<32768;++i)
        diff+=std::abs(yesLate.l[i]-noLate.l[i])+
              std::abs(yesLate.r[i]-noLate.r[i]);
    assert(diff>.001);
}
void verifyCompareNeverChangesSettings(double rate) {
    Settings s{};s.immersion=1.f;s.stereoLock=1.f;
    auto fx=std::make_unique<ImerSipDsp>();
    assert(fx->setSettings(s)&&fx->prepare(rate));
    assert(fx->settings().stereoLock==1.f);
    constexpr int frames=32768;
    Stereo music(frames);makeSignal(music,rate,4,300.f);
    Stereo wet=music,dry=music;
    // First fresh DSP starts processed; second instance in compare mode
    // must converge toward ORIGINAL with fixed trim at least after 200ms.
    auto compare=std::make_unique<ImerSipDsp>();
    assert(compare->setSettings(s)&&compare->prepare(rate));
    compare->setCompareDry(true);
    for(int at=0;at<frames;at+=128) {
        fx->process(wet.l.data()+at,wet.r.data()+at,128);
        compare->process(dry.l.data()+at,dry.r.data()+at,128);
    }
    for(int i=24000;i<frames;++i) {
        assert(std::abs(dry.l[i]-music.l[i])<1.e-5f);
        assert(std::abs(dry.r[i]-music.r[i])<1.e-5f);
    }
    assert(compare->settings().immersion==1.f);
    assert(compare->settings().stereoLock==1.f);
    assert(fx->lastStereoRetention()>0.f);
}

void verifyP87NotTheQualityReferenceAndRealDepth(double rate) {
    // Owner FAT: SonKu P8.7 default is NOT a perceptual reference.
    // The previous ImerSip 1.6m constant / 97-to-100deg WIDTH dial
    // had no useful front-depth control. Require *real* geometry and PCM.
    Settings s{};s.immersion=1.f;s.width=.65f;
    s.space=.80f;s.lateRoom=.15f;
    s.bassImpact=0.f;s.detail=0.f;s.outputTrim=1.f;
    s.stereoLock=1.f;
    assert(std::abs(armix::imersip::sceneFor(s).sourceDistanceMeters-
                    (1.6f+5.8f*s.depth))<1.e-4f);
    Stereo stereo(32768);makeSignal(stereo,rate,4,370.f);
    auto compare=[&](const Stereo& a,const Stereo& b) {
        double delta=0.,source=0.;
        for(int i=4096;i<32768;++i){
            const double dl=a.l[i]-b.l[i],dr=a.r[i]-b.r[i];
            delta+=dl*dl+dr*dr;
            source+=stereo.l[i]*stereo.l[i]+stereo.r[i]*stereo.r[i];
        }
        return std::sqrt(delta/(source+1.e-14));
    };
    s.depth=0.f;const auto near=render(rate,s,stereo,128);
    s.depth=1.f;const auto far=render(rate,s,stereo,128);
    const double depthDelta=compare(near,far);
    std::fprintf(stderr,
        "IMERSIP_REAL_DEPTH rate=%.0f near=1.6m far=7.4m delta=%.5f\n",
        rate,depthDelta);
    assert(depthDelta>.009);  // rejects old completely inert control
    s.depth=.6f;s.width=0.f;
    const auto narrow=render(rate,s,stereo,128);
    s.width=1.f;
    const auto wide=render(rate,s,stereo,128);
    const double spreadDelta=compare(narrow,wide);
    std::fprintf(stderr,
        "IMERSIP_WIDTH_FULL_RANGE rate=%.0f from=32 to=126 delta=%.5f\n",
        rate,spreadDelta);
    assert(spreadDelta>.025);
    // The FULL STOP must also matter from a usable musical default.
    s.width=Settings{}.width;
    const auto defWidth=render(rate,s,stereo,128);
    const double maxVsDefault=compare(defWidth,wide);
    std::fprintf(stderr,
        "IMERSIP_WIDTH_DEFAULT_TO_MAX rate=%.0f delta=%.5f\n",
        rate,maxVsDefault);
    assert(maxVsDefault>.010);
    Settings bad{};bad.depth=1.01f;assert(!armix::imersip::valid(bad));
    bad.depth=std::numeric_limits<float>::quiet_NaN();
    assert(!armix::imersip::valid(bad));
}
void verifyBassFoundation(double rate) {
    for(int venue=0;venue<3;++venue)for(float hz:{40.f,70.f,110.f,160.f,220.f}) {
        Stereo source;
        for(int i=0;i<n;++i) {
            const float x=.18f*static_cast<float>(std::sin(2*pi*hz*i/rate));
            source.l[i]=source.r[i]=x;
        }
        Settings s{};s.venue=venue;s.immersion=1.f;s.bassImpact=1.f;
        auto processed=render(rate,s,source);
        const auto before=rms(source,4096,false);
        const auto after=rms(processed,4096,false);
        // Coherent LF boost must not attenuate the original music bass.
        std::fprintf(stderr,"IMERSIP_BASS_GUARD rate=%.0f venue=%d hz=%.0f ratio=%.5f\n",
                     rate,venue,hz,after/before);
        assert(after>before*.995);
        // Up to ~2.0dB optional LF is deliberate, but never a doubling.
        assert(after<before*1.39);
    }
}
void verifyP86HorizontalRoomIsolation(double rate) {
    // Source of coefficients: SonKu P8.6 #711 (P8.5 stack).
    // Extract ONLY wall pressure weights; neither its native v7 ABI,
    // head-yaw automation nor rejected P8.3 architecture belongs here.
    using Room=armix::sonku::SonKuEarlyReflections;
    using Profile=armix::sonku::SonKuReflectionProfile;
    for(int wall=0;wall<6;++wall) {
        const float expected=wall<4?1.f:wall==4?.24f:.15f;
        assert(Room::horizontalFocusWeight(wall,false)==1.f);
        assert(Room::horizontalFocusWeight(wall,true)==expected);
    }
    auto normal=std::make_unique<Room>();
    auto focused=std::make_unique<Room>();
    assert(normal->prepare(rate,Profile::SixSurfaceKemarResearch));
    assert(focused->prepare(rate,Profile::SixSurfaceKemarResearch));
    armix::sonku::SonKuRoomGeometry g{};
    g.widthMeters=14.4f;g.depthMeters=14.4f;g.distanceMeters=1.6f;
    g.heightMeters=4.8f;g.sourceHeightMeters=1.45f;
    g.listenerHeightMeters=1.45f;
    assert(normal->setRealtimeRoom(g,.75f,true));
    assert(focused->setRealtimeRoom(g,.75f,true));
    double delta=0.,normalEnergy=0.,focusEnergy=0.;
    for(int i=0;i<16384;++i) {
        const double time=static_cast<double>(i)/rate;
        const float direct=.13f*static_cast<float>(
            std::sin(2.*pi*317.*time));
        const float side=.04f*static_cast<float>(
            std::sin(2.*pi*1237.*time));
        const float l=direct+side,r=direct-side;
        const auto a=normal->process(l,r,0.,175.f,false);
        const auto b=focused->process(l,r,0.,175.f,true);
        assert(std::isfinite(a.left)&&std::isfinite(a.right));
        assert(std::isfinite(b.left)&&std::isfinite(b.right));
        delta+=std::abs(static_cast<double>(a.left)-b.left)+
               std::abs(static_cast<double>(a.right)-b.right);
        normalEnergy+=static_cast<double>(a.left)*a.left+
                      static_cast<double>(a.right)*a.right;
        focusEnergy+=static_cast<double>(b.left)*b.left+
                     static_cast<double>(b.right)*b.right;
    }
    // Four lateral measured paths remain active; floor and ceiling
    // physically differ. Phase-interference means the TOTAL wet energy
    // need not be monotonic, so do not make a false total-level claim.
    assert(delta>1.e-4);
    assert(normalEnergy>1.e-8 && focusEnergy>1.e-8);
    std::fprintf(stderr,
        "IMERSIP_P86_HORIZONTAL rate=%.0f wetDifference=%.6f focusRatio=%.5f\n",
        rate,delta,focusEnergy/normalEnergy);
}
void verifyP84FusedKernelProvenanceAndLowFrequency(double rate) {
    // The standalone kernel source is transferred from P8.4; no P8.3
    // physical-renderer LF480, extra worker, 2nd runtime core or macro.
    using armix::sonku::p84::FusedMusicHrir;
    FusedMusicHrir merger;
    assert(merger.prepare(rate,.12f,750.f));
    assert(!merger.prepare(rate,.31f,750.f));
    assert(merger.prepare(rate,.12f,750.f));
    constexpr int taps=140;
    using Kernel=std::array<std::array<float,taps>,2>;
    Kernel center{},left{},right{};
    center[0][0]=1.f;center[1][0]=1.f;
    left[0][0]=1.70f;left[1][0]=.45f;
    right[0][0]=.45f;right[1][0]=1.70f;
    merger.merge(center,left,right);
    // Direction is real: L and R FIRs remain differentiated.
    assert(left[0][0]>right[0][0]);
    assert(left[1][0]<right[1][0]);
    // The 750Hz difference high-pass has ~zero DC coefficient sum
    // over the 140-tap FIR support: original music/bass center retained.
    for(int ear=0;ear<2;++ear) {
        double lsum=0.,rsum=0.;
        for(int tap=0;tap<taps;++tap) {
            lsum+=left[ear][tap];
            rsum+=right[ear][tap];
        }
        assert(std::abs(lsum-1.)<2.e-4);
        assert(std::abs(rsum-1.)<2.e-4);
    }
    // Strongly typed bounds also protect PCM update admissions.
    assert(!merger.prepare(rate,-.01f,750.f));
    assert(!merger.prepare(rate,.12f,350.f));
}
void verifyP82NormalPathRemainsUnchanged(double rate) {
    // Enabling the ImerSip-only research MODE=FALSE is exact P8.2 parity.
    // Normal SonKu direct kernels must not be recomposed or renamed.
    Stereo input;makeSignal(input,rate,4);
    auto legacy=std::make_unique<armix::sonku::SonKuSpatialCore>();
    auto disabled=std::make_unique<armix::sonku::SonKuSpatialCore>();
    using Profile=armix::sonku::SonKuReflectionProfile;
    assert(legacy->prepare(rate,Profile::SixSurfaceKemarResearch));
    assert(disabled->prepare(rate,Profile::SixSurfaceKemarResearch));
    Settings s{};s.width=.91f;s.space=.47f;s.immersion=1.f;
    const auto original=armix::imersip::sceneFor(s);
    assert(legacy->setSettings(original));
    assert(disabled->setSettings(original));
    assert(disabled->enableImerSipMeasuredStereoSide(false));
    legacy->reset();disabled->reset();
    Stereo a=input,b=input;
    for(int at=0;at<n;at+=128) {
        legacy->process(a.l.data()+at,a.r.data()+at,128);
        disabled->process(b.l.data()+at,b.r.data()+at,128);
    }
    for(int i=0;i<n;++i) {
        assert(a.l[i]==b.l[i]);
        assert(a.r[i]==b.r[i]);
    }
}
void verifyRoomLinkedBassStaysCoherent(double rate) {
    // Full venue space should slightly enrich the coherent low shelf;
    // real six-wall changes must not null the direct music bass.
    for(int venue=0;venue<3;++venue)for(float hz:{45.f,80.f,120.f}) {
        Stereo input;
        for(int i=0;i<n;++i) {
            const float v=.13f*static_cast<float>(
                std::sin(2.*pi*hz*i/rate));
            input.l[i]=v;input.r[i]=v;
        }
        Settings s{};s.venue=venue;s.immersion=1.f;
        s.bassImpact=1.f;s.detail=0.f;s.width=.95f;
        s.space=0.f;const auto low=render(rate,s,input);
        s.space=1.f;const auto rich=render(rate,s,input);
        const auto original=rms(input,4096,false);
        assert(rms(low,4096,false)>=original*.995);
        assert(rms(rich,4096,false)>=rms(low,4096,false)*.995);
        assert(rms(rich,4096,false)<original*1.14);
    }
}
void verifyActualMeasuredDualSpeakers(double rate) {
    // P8.2 standard music-safe renderer = centered KEMAR FIR.
    // The ImerSip experimental mode must employ TWO independent measured
    // KEMAR directions, not merely relabel the WIDTH control as 175 degrees.
    Stereo input;makeSignal(input,rate,2);
    auto dryP82=std::make_unique<armix::sonku::SonKuSpatialCore>();
    auto dual=std::make_unique<armix::sonku::SonKuSpatialCore>();
    using Profile=armix::sonku::SonKuReflectionProfile;
    assert(dryP82->prepare(rate,Profile::SixSurfaceKemarResearch));
    assert(dual->prepare(rate,Profile::SixSurfaceKemarResearch));
    armix::sonku::Settings s{};
    s.moving=false;s.startAzimuthDegrees=0.f;
    s.stereoSpreadDegrees=175.f;s.spatialMix=.65f;s.outputTrim=1.f;
    assert(dryP82->setSettings(s) && dual->setSettings(s));
    assert(!dryP82->imersipMeasuredStereoSideEnabled());
    assert(dual->enableImerSipMeasuredStereoSide(true));
    assert(dual->imersipMeasuredStereoSideEnabled());
    dryP82->reset();dual->reset();
    Stereo oldRender=input,newRender=input;
    for(int at=0;at<n;at+=128) {
        dryP82->process(oldRender.l.data()+at,oldRender.r.data()+at,128);
        dual->process(newRender.l.data()+at,newRender.r.data()+at,128);
    }
    double sideDifference=0.;
    for(int i=4096;i<n;++i)
        sideDifference+=std::abs(
            (newRender.l[i]-newRender.r[i])-
            (oldRender.l[i]-oldRender.r[i]));
    // Real measured source directions give a nontrivial interaural field.
    assert(sideDifference>1.);
}
void verifyActualP82RoomChangesSideOnly(double rate) {
    // Independent side material + different six-wall programs must produce
    // audible stereo room differences; not a fake width-only bypass.
    Stereo source;makeSignal(source,rate,4);
    Settings s{};s.immersion=1.f;s.width=1.f;s.bassImpact=0.f;
    s.venue=0;s.space=0.f;const auto first=render(rate,s,source);
    s.space=1.f;const auto second=render(rate,s,source);
    double residual=0.;
    for(int i=4000;i<n;++i)
        residual+=std::abs((second.l[i]-second.r[i])-
                           (first.l[i]-first.r[i]));
    assert(residual>1.e-5);
    // True room returns necessarily add small delayed MID energy:
    // the previous exact MID invariant made center source sound near-dry.
    assert(std::isfinite(rms(second,4096,false)));
}
void verifyDetailIsNotMidBoost(double rate) {
    // Detail is a positive >5k high shelf, never a 1-4k shout EQ.
    // Compare the EXACT same core/venue with detail OFF and ON.
    Settings s{};s.venue=0;s.bassImpact=0.f;s.width=.95f;
    s.immersion=1.f;s.space=.45f;
    for(float hz:{90.f,500.f,1500.f,3000.f,7000.f,11000.f}) {
        Stereo input;
        for(int i=0;i<n;++i) {
            const float x=.09f*static_cast<float>(std::sin(2*pi*hz*i/rate));
            input.l[i]=x;input.r[i]=x;
        }
        s.detail=0.f;const auto before=render(rate,s,input);
        s.detail=1.f;const auto after=render(rate,s,input);
        const double a=rms(before,4096,false),b=rms(after,4096,false);
        assert(a>1.e-4);
        const double ratio=b/a;
        if(hz<=3000.f)assert(ratio>=.999&&ratio<1.055);
        else assert(ratio>1.02&&ratio<1.39);
    }
}
void verifyAudibleWetIsNotNearBypassed(double rate) {
    for(int fixture:{0,4,5}) {
        Stereo input(32768);makeSignal(input,rate,fixture,311.f);
        Settings s{};s.immersion=0.f;s.width=1.f;
        s.detail=0.f;s.bassImpact=0.f;s.outputTrim=1.f;
        const auto dry=render(rate,s,input);
        s.immersion=1.f;s.space=1.f;
        const auto wet=render(rate,s,input);
        double power=0.,delta=0.;
        for(int i=4096;i<32768;++i) {
            const double a=input.l[i],b=input.r[i];
            const double dl=wet.l[i]-dry.l[i],dr=wet.r[i]-dry.r[i];
            power+=a*a+b*b;delta+=dl*dl+dr*dr;
        }
        const double ratio=std::sqrt(delta/(power+1.e-16));
        // Energy-based minimum -24dB PCM delta. This is NOT listener
        // acceptability, but rejects the previously practically-bypassed
        // room even when a trivial >1e-5 float test passed.
        std::fprintf(stderr,"IMERSIP_FULL_ROOM_DELTA rate=%.0f fixture=%d ratio=%.6f dB=%.2f\n",
            rate,fixture,ratio,20.*std::log10(ratio+1.e-12));
        if(!(ratio>.063)) {
            std::fprintf(stderr,
                "IMERSIP_NEAR_BYPASS: rate=%.0f fixture=%d deltaRms=%.8f\n",
                rate,fixture,ratio);
            assert(false);
        }
        for(int i=0;i<32768;++i) {
            assert(std::abs(dry.l[i]-input.l[i])<1.e-5f);
            assert(std::abs(dry.r[i]-input.r[i])<1.e-5f);
        }
    }
}
void verifyDefaultRoomActuallyChangesPCM(double rate) {
    for(int fixture:{0,4}) {
        Stereo input(32768);makeSignal(input,rate,fixture,311.f);
        Settings settings{};settings.immersion=0.f;
        settings.detail=0.f;settings.bassImpact=0.f;
        const auto dry=render(rate,settings,input);
        const Settings preset{};
        settings.immersion=preset.immersion;
        settings.space=preset.space;
        const auto wet=render(rate,settings,input);
        double signal=0.,changed=0.;
        for(int i=4096;i<32768;++i) {
            signal+=static_cast<double>(input.l[i])*input.l[i]
                   +static_cast<double>(input.r[i])*input.r[i];
            changed+=static_cast<double>(wet.l[i]-dry.l[i])*
                     (wet.l[i]-dry.l[i])
                    +static_cast<double>(wet.r[i]-dry.r[i])*
                     (wet.r[i]-dry.r[i]);
        }
        const double ratio=std::sqrt(changed/(signal+1.e-14));
        std::fprintf(stderr,"IMERSIP_DEFAULT_ROOM_DELTA rate=%.0f fixture=%d ratio=%.6f\n",
                     rate,fixture,ratio);
        // This tests REAL ROOM/HRIR: both tonal knobs are OFF. Old
        // visually-changing near-bypass code is rejected by this gate.
        assert(ratio>.025);
    }
}
void verifyPeakSafety(double rate) {
    Stereo source;
    for(int i=0;i<n;++i) {
        const float x=.95f*static_cast<float>(
            std::sin(2*pi*997.*i/rate));
        source.l[i]=x;source.r[i]=0.f;
    }
    Settings s{};s.immersion=1.f;s.space=1.f;
    s.width=1.f;s.bassImpact=1.f;
    auto result=render(rate,s,source);
    for(int i=0;i<n;++i) {
        assert(std::isfinite(result.l[i])&&std::isfinite(result.r[i]));
        assert(std::abs(result.l[i])<=.9801f);
        assert(std::abs(result.r[i])<=.9801f);
    }
}
} // namespace
int main() {
    verifySceneGeometry();
    verifyNearOrchestraRoomContract();
    for(double rate:{44100.,48000.}) {
        verifyDryAndBounds(rate);
        verifyFrontSpeakerVocalDoesNotNull(rate);
        verifyOriginalStereoSurvives(rate);
        verifyStereoIntegrityOwnerRejection(rate);
        verifyP87NotTheQualityReferenceAndRealDepth(rate);
        verifyCompareNeverChangesSettings(rate);
        verifyP84FusedKernelProvenanceAndLowFrequency(rate);
        verifyP86HorizontalRoomIsolation(rate);
        verifyP82NormalPathRemainsUnchanged(rate);
        verifyActualMeasuredDualSpeakers(rate);
        verifyRealP86DirectSpeakerReachesOutput(rate);
        verifyP87MusicalAirBodyRealIntegration(rate);
        verifyConcertStageLateralReflection(rate);
        verifyBassFoundation(rate);
        verifyRoomLinkedBassStaysCoherent(rate);
        verifyDetailIsNotMidBoost(rate);
        verifyActualP82RoomChangesSideOnly(rate);
        verifyAudibleWetIsNotNearBypassed(rate);
        verifyDefaultRoomActuallyChangesPCM(rate);
        verifyPeakSafety(rate);
    }
}
