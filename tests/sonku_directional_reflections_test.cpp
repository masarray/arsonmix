// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

int main(){
    using namespace armix::sonku;
    for(double rate:{44100.,48000.}){
        auto light=std::make_unique<SonKuEarlyReflections>();
        auto hrirRoom=std::make_unique<SonKuEarlyReflections>();
        auto mirror=std::make_unique<SonKuEarlyReflections>();
        assert(light->prepare(rate)&&hrirRoom->prepare(rate)&&mirror->prepare(rate));
        assert(!light->directionalHrirEnabled());
        assert(hrirRoom->setDirectionalHrir(true)&&mirror->setDirectionalHrir(true));
        assert(light->setShape(SonKuRoomShape::Square));
        assert(hrirRoom->setShape(SonKuRoomShape::Square));
        assert(mirror->setShape(SonKuRoomShape::Square));
        assert(light->setAmount(.55f)&&hrirRoom->setAmount(.55f));
        assert(mirror->setAmount(.55f));
        assert(!hrirRoom->setDirectionalHrir(false)); // active mode change rejected
        int first=-1;
        double energy=0.,delta=0.,sym=0.;
        for(int i=0;i<6500;++i){
            const float v=i==0?.40f:0.f;
            const auto ref=light->process(v,0.f,90.,0.f);
            const auto a=hrirRoom->process(v,0.f,90.,0.f);
            const auto b=mirror->process(v,0.f,-90.,0.f);
            assert(std::isfinite(a.left)&&std::isfinite(a.right));
            assert(std::abs(a.left)<.98f&&std::abs(a.right)<.98f);
            if(i<150)assert(a.left==0.f&&a.right==0.f);
            if(first<0&&(a.left!=0.f||a.right!=0.f))first=i;
            energy+=std::abs(a.left)+std::abs(a.right);
            delta+=std::abs(a.left-ref.left)+std::abs(a.right-ref.right);
            sym+=std::abs(a.left-b.right)+std::abs(a.right-b.left);
        }
        assert(first>150&&energy>.0001&&delta>.0005);
        assert(sym<.02); // measured right/left KEMAR symmetry
        assert(hrirRoom->setAmount(0.f));
        for(int i=0;i<10000;++i){
            auto out=hrirRoom->process(0.f,0.f,0.,0.f);
            if(i>9000)assert(out.left==0.f&&out.right==0.f);
        }
        assert(!hrirRoom->active());
        assert(hrirRoom->setDirectionalHrir(false));
        auto core=std::make_unique<SonKuSpatialCore>();
        assert(core->prepare(rate));
        assert(core->setDirectionalEarlyReflectionsHrir(true));
        assert(core->setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(core->setEarlyReflectionsAmount(.55f));
        Settings s{};s.moving=false;s.stereoSpreadDegrees=0.f;
        assert(core->setSettings(s));
        float l[256]{},r[256]{};l[0]=r[0]=.14f;
        core->process(l,r,256);
        for(int i=0;i<256;++i)assert(std::isfinite(l[i])&&std::isfinite(r[i]));
    }
    std::puts("PASS P2 directional reflected-KEMAR 44.1/48k, mirror, latency, OFF");
}
