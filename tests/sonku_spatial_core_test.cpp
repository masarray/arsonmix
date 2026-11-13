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

int main() {
    using namespace armix::sonku;
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
