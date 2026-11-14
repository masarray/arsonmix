// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <memory>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

// Measurable failure case from the listening report: two equal-angle virtual
// emitters fed opposite-polarity stereo cancel in each ear HRTF summation.
// Test the actual native PCM path, not a JS animation or guessed acoustics.
static void verifySonKuMusicIntegrity(double rate) {
    using namespace armix::sonku;
    Settings s{};
    s.moving=false;s.stereoSpreadDegrees=0.f;s.spatialMix=1.f;
    s.outputTrim=.75f;s.spacePreset=0;
    // A perfectly coherent stereo center must remain the exact old measured
    // impulse, including coefficient, amplitude, phase and callback timing.
    {
        auto center=std::make_unique<SonKuSpatialCore>();
        assert(center->prepare(rate)&&center->setSettings(s));
        std::array<float,256> l{},r{};
        l[0]=r[0]=.24f;
        center->process(l.data(),r.data(),static_cast<int>(l.size()));
        const auto& h=(std::abs(rate-44100.)<.1)?
            hrir::normal44100:hrir::normal48000;
        for(int i=0;i<hrir::taps;++i){
            const float expectedL=.24f*s.outputTrim*
                static_cast<float>(h[0][0][i])/32768.f;
            const float expectedR=.24f*s.outputTrim*
                static_cast<float>(h[0][1][i])/32768.f;
            assert(std::abs(l[i]-expectedL)<3.e-6f);
            assert(std::abs(r[i]-expectedR)<3.e-6f);
        }
    }
    constexpr int n=16384;
    constexpr float pi=3.14159265358979323846f;
    for (float angle : {0.f,90.f,180.f,270.f}) {
        auto whole=std::make_unique<SonKuSpatialCore>();
        auto partition=std::make_unique<SonKuSpatialCore>();
        s.startAzimuthDegrees=angle;
        assert(whole->prepare(rate)&&partition->prepare(rate));
        assert(whole->setSettings(s)&&partition->setSettings(s));
        std::vector<float> l(n),r(n),cl(n),cr(n);
        for(int i=0;i<n;++i){
            const float v=.33f*std::sin(2.f*pi*220.f*
                static_cast<float>(i)/static_cast<float>(rate));
            l[i]=cl[i]=v;
            r[i]=cr[i]=-v;
        }
        whole->process(l.data(),r.data(),n);
        for(int at=0;at<n;at+=128)
            partition->process(cl.data()+at,cr.data()+at,
                               std::min(128,n-at));
        double leftEnergy=0.,rightEnergy=0.,crossEnergy=0.;
        for(int i=0;i<n;++i){
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            assert(std::abs(l[i]-cl[i])<2.e-6f);
            assert(std::abs(r[i]-cr[i])<2.e-6f);
            assert(std::abs(l[i])<.98f&&std::abs(r[i])<.98f);
            if(i>n/2){
                leftEnergy+=static_cast<double>(l[i])*l[i];
                rightEnergy+=static_cast<double>(r[i])*r[i];
                crossEnergy+=static_cast<double>(l[i])*r[i];
            }
        }
        const double rmsL=std::sqrt(leftEnergy/(n/2-1));
        const double rmsR=std::sqrt(rightEnergy/(n/2-1));
        // Original SonKu collapses to zero at FRONT/REAR and zero SPREAD.
        // The phase-aware branch must preserve a musical stereo side signal
        // without converting it into a mono HRTF object.
        assert(rmsL>.065&&rmsR>.065);
        if(angle==0.f||angle==180.f)assert(crossEnergy<0.);
    }
    // Mixed stereo without strong anti-correlation never activates recovery:
    // a mono-correlated signal is not given a phantom second bed.
    {
        SonKuStereoIntegrity guard;
        assert(guard.prepare(rate));
        for(int i=0;i<8000;++i){
            float v=.4f*std::sin(2.f*pi*160.f*
                static_cast<float>(i)/static_cast<float>(rate));
            assert(guard.preservedSide(v,.7f*v)==0.f);
        }
        assert(guard.recoveryAmount()==0.f);
        assert(guard.coherentAmount()>.95f);
        guard.reset();
        assert(guard.coherentAmount()==0.f);
        for(int i=0;i<8000;++i){
            float v=.4f*std::sin(2.f*pi*160.f*
                static_cast<float>(i)/static_cast<float>(rate));
            (void)guard.preservedSide(v,-v);
        }
        assert(guard.recoveryAmount()>.95f);
        assert(guard.coherentAmount()<.01f);
        for(int i=0;i<25000;++i)
            assert(guard.preservedSide(0.f,0.f)==0.f);
        assert(guard.recoveryAmount()<.05f);
        assert(guard.coherentAmount()<.05f);
        assert(guard.prepare(rate)); // device restart must not remember polarity
        assert(guard.recoveryAmount()==0.f);
        assert(guard.coherentAmount()==0.f);
    }
    // A fully coherent music MID should take one center KEMAR HRIR instead
    // of the comb-filtered mean of two spatially separated virtual speakers.
    // This exercises the REAL native FIR in steady state (not a UI pan).
    {
        Settings coherent=s;
        coherent.startAzimuthDegrees=180.f;
        coherent.stereoSpreadDegrees=60.f;
        coherent.spatialMix=1.f;
        coherent.outputTrim=.6f;
        auto mid=std::make_unique<SonKuSpatialCore>();
        assert(mid->prepare(rate)&&mid->setSettings(coherent));
        constexpr int frames=32768;
        constexpr double frequency=2000.;
        std::vector<float> left(frames),right(frames);
        for(int i=0;i<frames;++i)
            left[i]=right[i]=.04f*static_cast<float>(
                std::sin(2.*3.14159265358979323846*frequency*i/rate));
        mid->process(left.data(),right.data(),frames);
        double sum=0.;
        for(int i=frames-8192;i<frames;++i)
            sum+=static_cast<double>(left[i])*left[i];
        const double observed=std::sqrt(sum/8192.)*std::sqrt(2.);
        const auto& h=(std::abs(rate-44100.)<.1)?
            hrir::normal44100:hrir::normal48000;
        auto response=[&](int azimuth,int ear) {
            double real=0.,imag=0.;
            for(int i=0;i<hrir::taps;++i){
                const double value=h[azimuth/5][ear][i]/32768.;
                const double phase=2.*3.14159265358979323846*frequency*i/rate;
                real+=value*std::cos(phase);
                imag-=value*std::sin(phase);
            }
            return std::array<double,2>{real,imag};
        };
        const auto center=response(180,0);
        const double expected=.04*coherent.outputTrim*
            std::hypot(center[0],center[1]);
        assert(expected>.005 && observed/expected>.86
               && observed/expected<1.14);
        if(std::abs(rate-48000.)<.1){
            const auto a=response(150,0),b=response(150,1);
            const double oldMean=.5*std::hypot(a[0]+b[0],a[1]+b[1]);
            const double newCenter=std::hypot(center[0],center[1]);
            // Independent frequency-domain HRTF audit: REAR 2kHz old
            // 60deg coherent stereo comb ~11.6dB, center is much clearer.
            assert(newCenter>oldMean*2.5);
        }
    }
    // The exact native ROOM ON path uses the SAME phase guard. Live room
    // reflections and the FDN cannot silently drop the side content, exceed
    // peak bounds or depend on host block partition. This is actual PCM.
    {
        auto live=std::make_unique<SonKuSpatialCore>();
        auto chunks=std::make_unique<SonKuSpatialCore>();
        Settings scene=s;
        scene.startAzimuthDegrees=180.f;
        scene.stereoSpreadDegrees=0.f;
        scene.spatialMix=1.f;
        scene.outputTrim=.65f;
        scene.spacePreset=2; // 14.4m chamber, 5m source
        scene.sourceDistanceMeters=5.f;
        scene.roomAmount=.55f;
        scene.ambience=.32f;
        assert(valid(scene));
        assert(live->prepare(rate)&&chunks->prepare(rate));
        assert(live->setSettings(scene)&&chunks->setSettings(scene));
        constexpr int sceneFrames=16384;
        std::vector<float> l(sceneFrames),r(sceneFrames),
                           cl(sceneFrames),cr(sceneFrames);
        for(int i=0;i<sceneFrames;++i) {
            const float v=.18f*static_cast<float>(
                std::sin(2.*3.14159265358979323846*440.*i/rate));
            l[i]=cl[i]=v;r[i]=cr[i]=-v;
        }
        live->process(l.data(),r.data(),sceneFrames);
        for(int at=0;at<sceneFrames;at+=256)
            chunks->process(cl.data()+at,cr.data()+at,
                            std::min(256,sceneFrames-at));
        double leftPower=0.,rightPower=0.;
        for(int i=0;i<sceneFrames;++i) {
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            assert(std::abs(l[i]-cl[i])<2.e-6f);
            assert(std::abs(r[i]-cr[i])<2.e-6f);
            assert(std::abs(l[i])<.98f&&std::abs(r[i])<.98f);
            if(i>=sceneFrames/2) {
                leftPower+=static_cast<double>(l[i])*l[i];
                rightPower+=static_cast<double>(r[i])*r[i];
            }
        }
        assert(std::sqrt(leftPower/(sceneFrames/2))>.025);
        assert(std::sqrt(rightPower/(sceneFrames/2))>.025);
    }
    // Mix=0 preserves the source channels as dry, including negative phase.
    {
        auto dry=std::make_unique<SonKuSpatialCore>();
        s.spatialMix=0.f;s.startAzimuthDegrees=0.f;
        assert(dry->prepare(rate)&&dry->setSettings(s));
        // The original engine intentionally slews MIX edits for de-click;
        // exact dry identity applies after the accepted transition settles.
        std::array<float,256> warmL{},warmR{};
        for(int k=0;k<48;++k)
            dry->process(warmL.data(),warmR.data(),256);
        std::array<float,2048> l{},r{};
        for(int i=0;i<static_cast<int>(l.size());++i){
            l[i]=.15f*std::sin(i*.017f);r[i]=-l[i];
        }
        const auto before=l;
        dry->process(l.data(),r.data(),static_cast<int>(l.size()));
        for(int i=0;i<static_cast<int>(l.size());++i){
            assert(l[i]==before[i]&&r[i]==-before[i]);
        }
    }
}

int main() {
    using namespace armix::sonku;
    for(double r:{44100.,48000.})verifySonKuMusicIntegrity(r);
    SonKuSpatialCore rejected;
    assert(!rejected.prepare(96000.));
    assert(!rejected.prepare(std::numeric_limits<double>::infinity()));
    assert(!rejected.ready());
    for(double rate:{44100.,48000.}) {
        SonKuSpatialCore core;
        assert(core.prepare(rate));
        Settings s{};
        s.moving=false; s.stereoSpreadDegrees=0.f; s.spatialMix=1.f;
        s.outputTrim=0.8f;
        assert(core.setSettings(s));
        constexpr int frames=512;
        std::array<float,frames> frontL{},frontR{},rearL{},rearR{},rightL{},rightR{};
        auto impulse=[&](float angle,std::array<float,frames>& a,std::array<float,frames>& b) {
            core.reset();Settings pos=s;pos.startAzimuthDegrees=angle;
            assert(core.setSettings(pos));
            a.fill(0.f);b.fill(0.f);a[0]=1.f;
            core.process(a.data(),b.data(),frames);
        };
        impulse(0.f,frontL,frontR);
        impulse(180.f,rearL,rearR);
        impulse(90.f,rightL,rightR);
        double front=0.,rear=0.,right=0.,difference=0.;
        for(int i=0;i<frames;++i){
            front+=std::abs(frontL[i])+std::abs(frontR[i]);
            rear+=std::abs(rearL[i])+std::abs(rearR[i]);
            right+=std::abs(rightL[i])+std::abs(rightR[i]);
            difference+=std::abs(frontL[i]-rearL[i])+std::abs(frontR[i]-rearR[i]);
            assert(std::isfinite(frontL[i])&&std::isfinite(rearL[i])&&std::isfinite(rightL[i]));
        }
        assert(front>.01&&rear>.01&&right>.01&&difference>.01);
        double lrDelta=0.;
        for(int i=0;i<frames;++i)lrDelta+=std::abs(rightL[i]-rightR[i]);
        assert(lrDelta>.01); // genuine differing HRIR ear filters
        // MIT KEMAR +90 degrees is RIGHT, not left: the physical right-ear
        // measured impulse arrives earlier and carries substantially more
        // early energy. This catches coordinate/ear-swapping mistakes.
        double earlyLeft=0.,earlyRight=0.;
        for(int i=0;i<32;++i) {
            earlyLeft+=rightL[i]*rightL[i];
            earlyRight+=rightR[i]*rightR[i];
        }
        assert(earlyRight>earlyLeft*4.);
        Settings moving{};
        moving.revolutionSeconds=0.5f;
        moving.stereoSpreadDegrees=50.f;
        moving.outputTrim=0.5f;
        assert(core.setSettings(moving));
        assert(!core.setSettings(Settings{0.f,0.f,60.f,1.f,0.5f,true,true}));
        constexpr int longFrames=1024;
        std::array<float,longFrames> oneL{},oneR{},chunkL{},chunkR{};
        for(int i=0;i<longFrames;++i){
            oneL[i]=chunkL[i]=std::sin(i*0.07f)*0.2f;
            oneR[i]=chunkR[i]=std::cos(i*0.09f)*0.2f;
        }
        SonKuSpatialCore whole,chunked;
        assert(whole.prepare(rate)&&chunked.prepare(rate));
        assert(whole.setSettings(moving)&&chunked.setSettings(moving));
        whole.process(oneL.data(),oneR.data(),longFrames);
        chunked.process(chunkL.data(),chunkR.data(),128);
        chunked.process(chunkL.data()+128,chunkR.data()+128,384);
        chunked.process(chunkL.data()+512,chunkR.data()+512,512);
        for(int i=0;i<longFrames;++i){
            assert(std::isfinite(oneL[i])&&std::isfinite(oneR[i]));
            assert(std::abs(oneL[i]-chunkL[i])<1.e-6f);
            assert(std::abs(oneR[i]-chunkR[i])<1.e-6f);
        }
        assert(std::abs(whole.azimuthDegrees()-chunked.azimuthDegrees())<1.e-10);
        // V2 A/B: room OFF is the verified original renderer; room ON
        // produces only delayed acoustic energy, without modifying first
        // arrival, direction signs or host buffer partition behavior.
        constexpr int roomFrames=4096;
        std::array<float,roomFrames> noL{},noR{},wetL{},wetR{},partL{},partR{};
        noL[0]=wetL[0]=partL[0]=.5f;
        noR[0]=wetR[0]=partR[0]=.5f;
        SonKuSpatialCore noRoom,withRoom,partRoom;
        assert(noRoom.prepare(rate)&&withRoom.prepare(rate)&&partRoom.prepare(rate));
        Settings roomSettings=s;roomSettings.startAzimuthDegrees=0.f;
        assert(noRoom.setSettings(roomSettings)&&withRoom.setSettings(roomSettings)
               &&partRoom.setSettings(roomSettings));
        assert(withRoom.setEarlyReflectionsAmount(.55f));
        assert(partRoom.setEarlyReflectionsAmount(.55f));
        noRoom.process(noL.data(),noR.data(),roomFrames);
        withRoom.process(wetL.data(),wetR.data(),roomFrames);
        partRoom.process(partL.data(),partR.data(),128);
        partRoom.process(partL.data()+128,partR.data()+128,384);
        partRoom.process(partL.data()+512,partR.data()+512,roomFrames-512);
        double roomDifference=0.;
        for(int i=0;i<roomFrames;++i){
            assert(std::isfinite(wetL[i])&&std::isfinite(wetR[i]));
            assert(std::abs(wetL[i]-partL[i])<1.e-6f);
            assert(std::abs(wetR[i]-partR[i])<1.e-6f);
            if(i<180){
                assert(std::abs(wetL[i]-noL[i])<1.e-7f);
                assert(std::abs(wetR[i]-noR[i])<1.e-7f);
            }
            if(i>180)roomDifference+=std::abs(wetL[i]-noL[i])+
                                     std::abs(wetR[i]-noR[i]);
        }
        assert(roomDifference>.001);
        // V3 late room is strictly opt-in, delayed and sample-partition
        // deterministic. The approved direct-HRTF + square ER is untouched
        // before the first late echo.
        // IMPORTANT: the accepted reference is SQUARE, not the earlier
        // rectangular V2 room. A rectangle-vs-square comparison cannot
        // establish that adding the FDN leaves the approved orbit intact.
        // Allocate this extra test processor off-stack (each core owns
        // fixed ~192 KiB acoustic rings; Windows test stacks are bounded).
        auto approvedSquare=std::make_unique<SonKuSpatialCore>();
        SonKuSpatialCore glow,glowChunks;
        assert(approvedSquare->prepare(rate)&&glow.prepare(rate)&&glowChunks.prepare(rate));
        assert(approvedSquare->setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(glow.setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(glowChunks.setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(approvedSquare->setSettings(roomSettings)&&
               glow.setSettings(roomSettings)&&glowChunks.setSettings(roomSettings));
        assert(approvedSquare->setEarlyReflectionsAmount(.55f)&&
               glow.setEarlyReflectionsAmount(.55f)&&
               glowChunks.setEarlyReflectionsAmount(.55f));
        assert(glow.setMusicalReverbAmount(.42f)&&glowChunks.setMusicalReverbAmount(.42f));
        std::array<float,roomFrames> squareL{},squareR{},tailL{},tailR{},splitL{},splitR{};
        squareL[0]=squareR[0]=tailL[0]=tailR[0]=splitL[0]=splitR[0]=.5f;
        approvedSquare->process(squareL.data(),squareR.data(),roomFrames);
        glow.process(tailL.data(),tailR.data(),roomFrames);
        glowChunks.process(splitL.data(),splitR.data(),128);
        glowChunks.process(splitL.data()+128,splitR.data()+128,384);
        glowChunks.process(splitL.data()+512,splitR.data()+512,roomFrames-512);
        double lateDifference=0.;
        for(int i=0;i<roomFrames;++i){
            assert(std::isfinite(tailL[i])&&std::isfinite(tailR[i]));
            assert(std::abs(tailL[i]-splitL[i])<1.e-6f);
            assert(std::abs(tailR[i]-splitR[i])<1.e-6f);
            // Earliest FDN energy is >43 ms at both qualified rates.
            // This proves exact sample identity in the approved square
            // direct+ER path before late ambience arrives.
            if(i<1800){
                assert(tailL[i]==squareL[i]);
                assert(tailR[i]==squareR[i]);
            }
            if(i>2200)lateDifference+=std::abs(tailL[i]-squareL[i])+
                                       std::abs(tailR[i]-squareR[i]);
        }
        assert(lateDifference>.001);
        // Explicit approved square parameters remain PCM-identical. Radius
        // attenuation works even with early reflections disabled, so SPACE
        // and DISTANCE remain independent, not one misleading "big hall" knob.
        {
            auto approved=std::make_unique<SonKuSpatialCore>();
            auto same=std::make_unique<SonKuSpatialCore>();
            auto distant=std::make_unique<SonKuSpatialCore>();
            assert(approved->prepare(rate)&&same->prepare(rate)&&
                   distant->prepare(rate));
            Settings pose=s;pose.moving=false;pose.startAzimuthDegrees=0.f;
            assert(approved->setSettings(pose)&&same->setSettings(pose)&&
                   distant->setSettings(pose));
            assert(approved->setEarlyReflectionsShape(SonKuRoomShape::Square));
            assert(same->setAcousticRoomGeometry({7.2f,7.2f,1.6f}));
            assert(distant->setAcousticRoomGeometry({28.8f,28.8f,8.f}));
            assert(approved->setEarlyReflectionsAmount(.55f));
            assert(same->setEarlyReflectionsAmount(.55f));
            std::array<float,4096> aa{},ab{},ba{},bb{};
            aa[0]=ab[0]=ba[0]=bb[0]=.6f;
            approved->process(aa.data(),ab.data(),4096);
            same->process(ba.data(),bb.data(),4096);
            for(int i=0;i<4096;++i) {
                assert(aa[i]==ba[i]&&ab[i]==bb[i]);
            }
            std::array<float,1024> nearL{},nearR{},farL{},farR{};
            nearL[0]=nearR[0]=farL[0]=farR[0]=.6f;
            // Compare separate fresh reference to distant field (ER OFF).
            auto dryNear=std::make_unique<SonKuSpatialCore>();
            assert(dryNear->prepare(rate)&&dryNear->setSettings(pose));
            dryNear->process(nearL.data(),nearR.data(),1024);
            distant->process(farL.data(),farR.data(),1024);
            double nearEnergy=0.,farEnergy=0.;
            for(int i=0;i<1024;++i) {
                nearEnergy+=std::abs(nearL[i])+std::abs(nearR[i]);
                farEnergy+=std::abs(farL[i])+std::abs(farR[i]);
            }
            assert(nearEnergy>.01);
            assert(std::abs(farEnergy/nearEnergy-.2)<1.e-5);
        }
        // M4: real native scene settings must affect the exact PCM core, not
        // just a visual mock. Old 1.6m/Square HRTF remains the same baseline.
        {
            auto reference=std::make_unique<SonKuSpatialCore>();
            auto scene=std::make_unique<SonKuSpatialCore>();
            auto partition=std::make_unique<SonKuSpatialCore>();
            assert(reference->prepare(rate)&&scene->prepare(rate)&&
                   partition->prepare(rate));
            Settings live=s;live.moving=false;live.startAzimuthDegrees=0.f;
            live.spacePreset=2;live.sourceDistanceMeters=5.f;
            live.roomAmount=.55f;live.ambience=.32f;
            assert(valid(live));
            Settings bad=live;bad.spacePreset=1;
            assert(!valid(bad)); // 5m cannot fit inside 7.2m square
            bad=live;bad.ambience=std::numeric_limits<float>::quiet_NaN();
            assert(!valid(bad));
            bad=live;bad.spacePreset=4;assert(!valid(bad));
            assert(reference->setSettings(s));
            assert(scene->setSettings(live)&&partition->setSettings(live));
            constexpr int n=4096;
            std::array<float,n> refL{},refR{},a{},b{},chunkL{},chunkR{};
            for(int i=0;i<n;++i){
                const float sample=.12f*std::sin(static_cast<float>(i)*.07f);
                refL[i]=refR[i]=a[i]=b[i]=chunkL[i]=chunkR[i]=sample;
            }
            reference->process(refL.data(),refR.data(),n);
            scene->process(a.data(),b.data(),n);
            for(int i=0;i<n;i+=128)
                partition->process(chunkL.data()+i,chunkR.data()+i,128);
            double delta=0.;
            for(int i=0;i<n;++i){
                assert(std::isfinite(a[i])&&std::isfinite(b[i]));
                assert(std::abs(a[i]-chunkL[i])<1.e-6f);
                assert(std::abs(b[i]-chunkR[i])<1.e-6f);
                delta+=std::abs(refL[i]-a[i])+std::abs(refR[i]-b[i]);
            }
            assert(delta>.1); // actual accepted-space DSP, not GUI-only
            // Changing room size in-flight is permitted: all geometry and
            // FDN coefficients are already bounded, no memory allocation.
            live.spacePreset=3;live.sourceDistanceMeters=8.f;
            assert(valid(live)&&scene->setSettings(live));
            std::array<float,128> transitionL{},transitionR{};
            transitionL.fill(.13f);transitionR.fill(.13f);
            scene->process(transitionL.data(),transitionR.data(),128);
            for(int i=0;i<128;++i)
                assert(std::isfinite(transitionL[i])&&
                       std::isfinite(transitionR[i])&&
                       std::abs(transitionL[i])<1.f&&
                       std::abs(transitionR[i])<1.f);
            // Turning SPACE off removes all room+FDN work after smoothing.
            live.spacePreset=0;assert(scene->setSettings(live));
            for(int i=0;i<90;++i){
                transitionL.fill(0.f);transitionR.fill(0.f);
                scene->process(transitionL.data(),transitionR.data(),128);
            }
        }
        // P0 field report: live SOURCE DISTANCE used the full 1/r pressure
        // drop (-14 dB by 8m) on top of already attenuated KEMAR bass.
        // Native SPACE now uses sample-smoothed *monitoring compensation*;
        // no alteration to the user-approved ROOM OFF/offline reference.
        {
            auto listening=std::make_unique<SonKuSpatialCore>();
            auto control=std::make_unique<SonKuSpatialCore>();
            auto original=std::make_unique<SonKuSpatialCore>();
            assert(listening->prepare(rate)&&control->prepare(rate)&&
                   original->prepare(rate));
            Settings near=roomSettings;
            near.moving=false;near.startAzimuthDegrees=0.f;
            near.stereoSpreadDegrees=0.f;near.outputTrim=0.8f;
            near.spacePreset=3;near.sourceDistanceMeters=1.6f;
            near.roomAmount=0.f;near.ambience=0.f;
            Settings off=near;off.spacePreset=0;
            assert(listening->setSettings(near)&&
                   control->setSettings(near)&&
                   original->setSettings(off));
            constexpr int block=256;
            std::array<float,block> audioL{},audioR{},controlL{},controlR{},
                                    dryL{},dryR{};
            auto fillSine=[&](int start) {
                constexpr double pi=3.14159265358979323846;
                for(int i=0;i<block;++i){
                    const float v=.17f*std::sin(static_cast<float>(
                        2.*pi*120.*(start+i)/rate));
                    audioL[i]=audioR[i]=controlL[i]=controlR[i]=dryL[i]=dryR[i]=v;
                }
            };
            for(int n=0;n<24;++n){
                fillSine(n*block);
                listening->process(audioL.data(),audioR.data(),block);
                control->process(controlL.data(),controlR.data(),block);
                original->process(dryL.data(),dryR.data(),block);
            }
            double nearEnergy=0.,dryEnergy=0.;
            for(int i=0;i<block;++i){
                nearEnergy+=std::abs(audioL[i])+std::abs(audioR[i]);
                dryEnergy+=std::abs(dryL[i])+std::abs(dryR[i]);
            }
            assert(nearEnergy>dryEnergy*1.10); // bounded bass warmth
            // Independent native distance edit while steady 120Hz music plays.
            Settings far=near;far.sourceDistanceMeters=8.f;
            assert(control->setSettings(far));
            fillSine(24*block);
            listening->process(audioL.data(),audioR.data(),block);
            control->process(controlL.data(),controlR.data(),block);
            assert(std::abs(audioL[0]-controlL[0])<.006f);
            assert(std::abs(audioR[0]-controlR[0])<.006f);
            for(int i=0;i<block;++i){
                assert(std::isfinite(controlL[i])&&std::isfinite(controlR[i]));
                assert(std::abs(controlL[i])<=.981f&&std::abs(controlR[i])<=.981f);
                if(i){
                    assert(std::abs(controlL[i]-controlL[i-1])<.05f);
                    assert(std::abs(controlR[i]-controlR[i-1])<.05f);
                }
            }
            for(int n=25;n<50;++n){
                fillSine(n*block);
                listening->process(audioL.data(),audioR.data(),block);
                control->process(controlL.data(),controlR.data(),block);
            }
            double farEnergy=0.;nearEnergy=0.;
            for(int i=0;i<block;++i){
                farEnergy+=std::abs(controlL[i])+std::abs(controlR[i]);
                nearEnergy+=std::abs(audioL[i])+std::abs(audioR[i]);
            }
            // Far must perceptually recede without becoming practically mute.
            assert(farEnergy/nearEnergy>.39&&farEnergy/nearEnergy<.83);
            // A full-scale musical peak cannot overflow the 0dBFS sample
            // limit in native SPACE, regardless of correlated L/R emitters.
            for(int n=0;n<30;++n){
                for(int i=0;i<block;++i){
                    const float v=.95f*std::sin(static_cast<float>(
                        2.*3.14159265358979323846*3000.*(n*block+i)/rate));
                    audioL[i]=audioR[i]=v;
                }
                listening->process(audioL.data(),audioR.data(),block);
                for(int i=0;i<block;++i)
                    assert(std::abs(audioL[i])<=.981f &&
                           std::abs(audioR[i])<=.981f);
            }
        }
        // Explicit operator report: early and late are *inaudible* in the
        // pre-boosted native scene. Assert true independent wet returns and
        // their tail window; pure UI % changes cannot satisfy this test.
        {
            constexpr int n=16384;
            Settings base=s;base.moving=false;base.startAzimuthDegrees=0.f;
            base.stereoSpreadDegrees=0.f;base.outputTrim=1.f;
            base.spacePreset=1;base.sourceDistanceMeters=1.6f;
            base.roomAmount=0.f;base.ambience=0.f;
            Settings first=base;first.roomAmount=.55f;
            Settings tail=base;tail.ambience=.30f;
            Settings both=first;both.ambience=.30f;
            auto mk=[](){return std::make_unique<SonKuSpatialCore>();};
            auto reference=mk(),reflected=mk(),reverberated=mk(),combined=mk();
            assert(reference->prepare(rate)&&reflected->prepare(rate)&&
                   reverberated->prepare(rate)&&combined->prepare(rate));
            assert(reference->setSettings(base)&&reflected->setSettings(first)&&
                   reverberated->setSettings(tail)&&combined->setSettings(both));
            // Measure the SETTLED room response, not its deliberate 10-30ms
            // activation fades. Real operator hears the running room after
            // ACK. Using a t=0 impulse conflates wet attenuation with the
            // intentional click-preventing fade, falsely failing ER.
            const int preroll=static_cast<int>(rate/5.);
            std::vector<float> warmL(preroll,0.f),warmR(preroll,0.f);
            for(auto* core:{reference.get(),reflected.get(),
                            reverberated.get(),combined.get()}){
                std::fill(warmL.begin(),warmL.end(),0.f);
                std::fill(warmR.begin(),warmR.end(),0.f);
                core->process(warmL.data(),warmR.data(),preroll);
            }
            std::vector<float> dryL(n),dryR(n),erL(n),erR(n),
                               rvL(n),rvR(n),allL(n),allR(n);
            dryL[0]=dryR[0]=erL[0]=erR[0]=rvL[0]=rvR[0]=
                allL[0]=allR[0]=.40f;
            reference->process(dryL.data(),dryR.data(),n);
            reflected->process(erL.data(),erR.data(),n);
            reverberated->process(rvL.data(),rvR.data(),n);
            combined->process(allL.data(),allR.data(),n);
            double direct=0.,erPresence=0.,latePresence=0.,combinedDifference=0.;
            for(int i=0;i<n;++i){
                direct+=std::abs(dryL[i])+std::abs(dryR[i]);
                if(i>=150 && i<2200)
                    erPresence+=std::abs(erL[i]-dryL[i])+
                                std::abs(erR[i]-dryR[i]);
                if(i>=2200 && i<13000)
                    latePresence+=std::abs(rvL[i]-dryL[i])+
                                  std::abs(rvR[i]-dryR[i]);
                combinedDifference+=std::abs(allL[i]-dryL[i])+
                                    std::abs(allR[i]-dryR[i]);
                for(const float v:{erL[i],erR[i],rvL[i],rvR[i],allL[i],allR[i]})
                    assert(std::isfinite(v)&&std::abs(v)<=.981f);
            }
            // Wet effects must be sizeable relative to the original measured
            // binaural first arrival, and independently controllable.
            assert(direct>.01);
            assert(erPresence/direct>.035);
            assert(latePresence/direct>.035);
            assert(combinedDifference>erPresence*.55);
            // Gain is not a renamed cap at unity: new saved v2 linear values
            // >1 are admitted and affect the *output*, including MIX=0.
            Settings boosted=base;boosted.spacePreset=0;
            boosted.spatialMix=0.f;boosted.outputTrim=2.f;
            assert(valid(boosted));
            Settings invalid=boosted;invalid.outputTrim=4.1f;
            assert(!valid(invalid));
            auto gainCore=mk();assert(gainCore->prepare(rate));
            assert(gainCore->setSettings(boosted));
            std::array<float,8192> gainL{},gainR{};
            gainL.fill(.12f);gainR.fill(-.10f);
            gainCore->process(gainL.data(),gainR.data(),8192);
            assert(gainL.back()>.22f && gainR.back()<-.18f);
            gainL.fill(.93f);gainR.fill(-.93f);
            gainCore->process(gainL.data(),gainR.data(),8192);
            for(int i=0;i<8192;++i){
                assert(std::abs(gainL[i])<=.981f);
                assert(std::abs(gainR[i])<=.981f);
            }
        }
        // A hard FRONT->REAR teleport must not replace 560 FIR coefficients
        // at the next 16-sample boundary. Compare the identical dry/PCM
        // history before and immediately after reposition.
        SonKuSpatialCore teleport,stationary;
        assert(teleport.prepare(rate)&&stationary.prepare(rate));
        Settings frontPose=s;frontPose.startAzimuthDegrees=0.f;
        assert(teleport.setSettings(frontPose)&&stationary.setSettings(frontPose));
        std::array<float,512> preL{},preR{},sameL{},sameR{};
        preL.fill(.12f);preR.fill(-.10f);sameL=preL;sameR=preR;
        teleport.process(preL.data(),preR.data(),512);
        stationary.process(sameL.data(),sameR.data(),512);
        Settings rearPose=frontPose;rearPose.startAzimuthDegrees=180.f;
        assert(teleport.setSettings(rearPose));
        std::array<float,1024> stepL{},stepR{},refL{},refR{};
        stepL.fill(.12f);stepR.fill(-.10f);refL=stepL;refR=stepR;
        teleport.process(stepL.data(),stepR.data(),1024);
        stationary.process(refL.data(),refR.data(),1024);
        assert(std::abs(stepL[0]-refL[0])<.008f);
        assert(std::abs(stepR[0]-refR[0])<.008f);
        double changed=0.;
        for(int i=750;i<1024;++i)
            changed+=std::abs(stepL[i]-refL[i])+std::abs(stepR[i]-refR[i]);
        assert(changed>.005);
        // MIX/trim changes are smoothed on the same audio clock.
        SonKuSpatialCore mixCore;
        assert(mixCore.prepare(rate));
        Settings dry=frontPose;dry.spatialMix=0.f;
        assert(mixCore.setSettings(dry));
        std::array<float,2048> warmL{},warmR{};
        warmL.fill(.19f);warmR.fill(-.13f);
        mixCore.process(warmL.data(),warmR.data(),2048);
        Settings wet=dry;wet.spatialMix=1.f;wet.outputTrim=.5f;
        assert(mixCore.setSettings(wet));
        std::array<float,1024> fadeL{},fadeR{};
        fadeL.fill(.19f);fadeR.fill(-.13f);
        mixCore.process(fadeL.data(),fadeR.data(),1024);
        assert(std::abs(fadeL[0]-.19f)<.01f);
        assert(std::abs(fadeR[0]+.13f)<.01f);
        for(int i=1;i<1024;++i) {
            assert(std::isfinite(fadeL[i])&&std::isfinite(fadeR[i]));
            assert(std::abs(fadeL[i]-fadeL[i-1])<.04f);
            assert(std::abs(fadeR[i]-fadeR[i-1])<.04f);
        }
        moving.spatialMix=0.f; assert(chunked.setSettings(moving));
        // The knob transition is deliberately clickless; zero wet is exact
        // after its finite smoothing interval, not the first sample.
        std::array<float,128> settleL{},settleR{};
        for(int block=0;block<80;++block) {
            settleL.fill(0.f);settleR.fill(0.f);
            chunked.process(settleL.data(),settleR.data(),128);
        }
        std::array<float,32> dryL{},dryR{};dryL.fill(.17f);dryR.fill(-.29f);
        chunked.process(dryL.data(),dryR.data(),32);
        for(int i=0;i<32;++i){assert(std::abs(dryL[i]-.17f)<1.e-6f);assert(std::abs(dryR[i]+.29f)<1.e-6f);}
    }
    return 0;
}
