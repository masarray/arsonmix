// SPDX-License-Identifier: AGPL-3.0-only
// P5 stale-tail prevention when a native directional room is turned OFF
// and re-enabled with silence, without clearing the ring in audio callback.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
int main(){
    using namespace armix::sonku;
    constexpr double pi=3.14159265358979323846;
    for(double rate:{44100.,48000.}){
        for(bool contiguous:{false,true}){
            for(float spread:{0.f,60.f,120.f}){
                auto room=std::make_unique<SonKuEarlyReflections>();
                assert(room->prepare(rate));
                assert(room->setDirectionalHrir(true));
                if(contiguous)assert(room->setDirectionalContiguousHrir(true));
                const SonKuRoomGeometry geometry{7.2f,7.2f,1.6f};
                assert(room->setRealtimeRoom(geometry,.55f,true));
                for(int i=0;i<4500;++i){
                    float l=.22f*std::sin(static_cast<float>(2*pi*440*i/rate));
                    float r=.13f*std::sin(static_cast<float>(2*pi*720*i/rate));
                    room->process(l,r,45.,spread);
                }
                // Drive the old ring with audio while the ROOM fades out,
                // so stale samples are definitely present at the restart.
                assert(room->setRealtimeRoom(geometry,0.f,false));
                int fadeFrames=0;
                while(room->active()&&fadeFrames<10000){
                    const float l=.14f*std::sin(static_cast<float>(
                        2*pi*520*fadeFrames/rate));
                    const float r=.11f*std::cos(static_cast<float>(
                        2*pi*1250*fadeFrames/rate));
                    room->process(l,r,70.,spread);
                    ++fadeFrames;
                }
                assert(!room->active());
                assert(fadeFrames>100);
                // OFF => no history writes. Re-enabling on a changed
                // source scene with zero input must NOT resurrect old PCM.
                assert(room->setRealtimeRoom(geometry,.55f,true));
                for(int i=0;i<1500;++i){
                    auto out=room->process(0.f,0.f,135.,spread);
                    assert(std::isfinite(out.left)&&std::isfinite(out.right));
                    assert(out.left==0.f && out.right==0.f);
                }
                // But newly captured signal MUST still emerge with
                // physically causal image-path delay, not be dead-muted.
                int first=-1;
                double energy=0.;
                for(int i=0;i<7000;++i){
                    const float v=i==0?.35f:0.f;
                    const auto out=room->process(v,v,135.,spread);
                    if(first<0&&(out.left!=0.f||out.right!=0.f))first=i;
                    energy+=out.left*out.left+out.right*out.right;
                }
                assert(first>100&&first<6000);
                assert(energy>1.e-7);
            }
        }
    }
    std::puts("PASS SonKu P5 no old room ghost on reactivation; real new impulse remains audible");
}
