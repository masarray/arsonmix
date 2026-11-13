// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
int main(){
    using armix::sonku::SonKuEarlyReflections;
    using armix::sonku::SonKuRoomShape;
    using armix::sonku::SonKuRoomGeometry;
    SonKuEarlyReflections invalid;
    assert(!invalid.prepare(96000.));
    assert(!invalid.prepare(std::numeric_limits<double>::quiet_NaN()));
    for(double rate:{44100.,48000.}){
        SonKuEarlyReflections squareRight,squareLeft;
        assert(squareRight.prepare(rate)&&squareLeft.prepare(rate));
        assert(squareRight.setShape(SonKuRoomShape::Square));
        assert(squareLeft.setShape(SonKuRoomShape::Square));
        assert(squareRight.setAmount(.55f)&&squareLeft.setAmount(.55f));
        double mirrorError=0.,soundEnergy=0.;
        for(int i=0;i<4096;++i){
            const float v=i==0?1.f:0.f;
            const auto right=squareRight.process(v,0.f,90.,0.f);
            const auto left=squareLeft.process(v,0.f,-90.,0.f);
            mirrorError+=std::abs(right.left-left.right)+
                         std::abs(right.right-left.left);
            soundEnergy+=std::abs(right.left)+std::abs(right.right);
        }
        assert(soundEnergy>.004);
        assert(mirrorError<1.e-4);
        // The published user's approved square impulse must remain byte-exact
        // when the same geometry is expressed explicitly.
        {
            SonKuEarlyReflections approved,explicitSquare;
            assert(approved.prepare(rate)&&explicitSquare.prepare(rate));
            assert(approved.setShape(SonKuRoomShape::Square));
            assert(explicitSquare.setGeometry({7.2f,7.2f,1.6f}));
            assert(approved.setAmount(.55f)&&explicitSquare.setAmount(.55f));
            assert(!explicitSquare.setGeometry({14.4f,14.4f,5.f}));
            for(int i=0;i<8192;++i) {
                const float v=i==0?.75f:0.f;
                const auto a0=approved.process(v,v,37.,45.f);
                const auto b0=explicitSquare.process(v,v,37.,45.f);
                assert(a0.left==b0.left&&a0.right==b0.right);
            }
        }
        assert(!SonKuEarlyReflections::validGeometry({7.2f,7.2f,5.f}));
        assert(!SonKuEarlyReflections::validGeometry({40.1f,40.f,1.6f}));
        assert(!SonKuEarlyReflections::validGeometry({
            std::numeric_limits<float>::quiet_NaN(),14.f,2.f}));
        assert(SonKuEarlyReflections::validGeometry({14.4f,14.4f,5.f}));
        assert(SonKuEarlyReflections::validGeometry({28.8f,28.8f,8.f}));
        // Larger boundaries delay the FIRST reflected arrival. No longer
        // room is faked by increasing a single reverb decay number.
        {
            SonKuEarlyReflections nearRoom,largeRoom;
            assert(nearRoom.prepare(rate)&&largeRoom.prepare(rate));
            assert(nearRoom.setShape(SonKuRoomShape::Square));
            assert(largeRoom.setGeometry({28.8f,28.8f,1.6f}));
            assert(nearRoom.setAmount(.55f)&&largeRoom.setAmount(.55f));
            int nearFirst=-1,largeFirst=-1;
            for(int i=0;i<8000;++i) {
                const float v=i==0?1.f:0.f;
                const auto a0=nearRoom.process(v,v,0.,0.f);
                const auto b0=largeRoom.process(v,v,0.,0.f);
                if(nearFirst<0 && (a0.left!=0.f||a0.right!=0.f))nearFirst=i;
                if(largeFirst<0 && (b0.left!=0.f||b0.right!=0.f))largeFirst=i;
            }
            assert(nearFirst>100 && largeFirst>nearFirst+800);
        }
        SonKuEarlyReflections a,b;
        assert(a.prepare(rate)&&b.prepare(rate));
        assert(!a.active());
        assert(!a.setAmount(-.01f));
        assert(!a.setAmount(std::numeric_limits<float>::infinity()));
        assert(a.setAmount(.55f)&&b.setAmount(.55f));
        double echo=0.,earDifference=0.;
        constexpr int count=4096;
        for(int i=0;i<count;++i){
            const float left=i==0?1.f:0.f;
            const auto x=a.process(left,0.f,90.,0.f);
            const auto y=b.process(left,0.f,90.,0.f);
            assert(std::isfinite(x.left)&&std::isfinite(x.right));
            assert(std::abs(x.left-y.left)<1.e-7f);
            assert(std::abs(x.right-y.right)<1.e-7f);
            if(i<180)assert(x.left==0.f && x.right==0.f);
            if(i>180){
                echo+=std::abs(x.left)+std::abs(x.right);
                earDifference+=std::abs(x.left-x.right);
            }
        }
        assert(echo>.004 && earDifference>.001);
        // Switching the optional room off must settle to bit-exact silence;
        // the dry measured HRTF never borrows anything from stale room state.
        assert(a.setAmount(0.f));
        for(int i=0;i<6000;++i){
            auto tail=a.process(0.f,0.f,180.,0.f);
            if(i>5000)assert(tail.left==0.f&&tail.right==0.f);
        }
        assert(!a.active());
    }
    return 0;
}
