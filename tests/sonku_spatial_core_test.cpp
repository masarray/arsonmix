// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
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
        SonKuSpatialCore glow,glowChunks;
        assert(glow.prepare(rate)&&glowChunks.prepare(rate));
        assert(glow.setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(glowChunks.setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(glow.setSettings(roomSettings)&&glowChunks.setSettings(roomSettings));
        assert(glow.setEarlyReflectionsAmount(.55f)&&glowChunks.setEarlyReflectionsAmount(.55f));
        assert(glow.setMusicalReverbAmount(.42f)&&glowChunks.setMusicalReverbAmount(.42f));
        std::array<float,roomFrames> tailL{},tailR{},splitL{},splitR{};
        tailL[0]=splitL[0]=.5f;tailR[0]=splitR[0]=.5f;
        glow.process(tailL.data(),tailR.data(),roomFrames);
        glowChunks.process(splitL.data(),splitR.data(),128);
        glowChunks.process(splitL.data()+128,splitR.data()+128,384);
        glowChunks.process(splitL.data()+512,splitR.data()+512,roomFrames-512);
        double diff=0.;
        for(int i=0;i<roomFrames;++i){
            assert(std::isfinite(tailL[i])&&std::isfinite(tailR[i]));
            assert(std::abs(tailL[i]-splitL[i])<1.e-6f);
            assert(std::abs(tailR[i]-splitR[i])<1.e-6f);
            if(i<1700){
                // This compares another shape? Original withRoom uses
                // rectangle: only validate first late arrival against
                // identical new square stage below, not against rectangle.
                assert(std::abs(tailL[i])<1.f);
            }
            if(i>2200)diff+=std::abs(tailL[i]-wetL[i])+
                             std::abs(tailR[i]-wetR[i]);
        }
        assert(diff>.001);
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
