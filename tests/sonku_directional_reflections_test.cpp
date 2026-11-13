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
        // P2: moving 360-degree source, frequency-conflicted stereo
        // input, two different native host block partitionings.
        // No GUI/WebAudio proxy: exact native core process sample history.
        {
            constexpr int samples=12288;
            auto one=std::make_unique<SonKuSpatialCore>();
            auto chunks=std::make_unique<SonKuSpatialCore>();
            assert(one->prepare(rate)&&chunks->prepare(rate));
            assert(one->setDirectionalEarlyReflectionsHrir(true));
            assert(chunks->setDirectionalEarlyReflectionsHrir(true));
            assert(one->setEarlyReflectionsShape(SonKuRoomShape::Square));
            assert(chunks->setEarlyReflectionsShape(SonKuRoomShape::Square));
            assert(one->setEarlyReflectionsAmount(.55f));
            assert(chunks->setEarlyReflectionsAmount(.55f));
            Settings orbit{};
            orbit.moving=true;orbit.revolutionSeconds=4.f;
            orbit.stereoSpreadDegrees=60.f;orbit.outputTrim=.6f;
            assert(one->setSettings(orbit)&&chunks->setSettings(orbit));
            std::array<float,samples> a{},b{},ca{},cb{};
            constexpr double pi=3.14159265358979323846;
            for(int i=0;i<samples;++i){
                const float bass=.15f*std::sin(static_cast<float>(
                    2.*pi*220.*i/rate));
                const float treble=.07f*std::sin(static_cast<float>(
                    2.*pi*3200.*i/rate));
                a[i]=ca[i]=bass+treble;
                b[i]=cb[i]=bass-treble;
            }
            one->process(a.data(),b.data(),samples);
            for(int at=0;at<samples;){
                const int n=std::min((at/64)%2==0?64:256,samples-at);
                chunks->process(ca.data()+at,cb.data()+at,n);
                at+=n;
            }
            double energy=0.;
            for(int i=0;i<samples;++i){
                assert(std::isfinite(a[i])&&std::isfinite(b[i]));
                assert(std::abs(a[i]-ca[i])<1.e-6f);
                assert(std::abs(b[i]-cb[i])<1.e-6f);
                assert(std::abs(a[i])<.98f&&std::abs(b[i])<.98f);
                energy+=a[i]*a[i]+b[i]*b[i];
            }
            assert(energy>1.);
            assert(std::abs(one->azimuthDegrees()-chunks->azimuthDegrees())<1.e-8);
        }
        // Larger actual wall geometry delays directional image-source
        // first arrival, not merely a longer arbitrary reverb preset.
        {
            auto small=std::make_unique<SonKuEarlyReflections>();
            auto large=std::make_unique<SonKuEarlyReflections>();
            assert(small->prepare(rate)&&large->prepare(rate));
            assert(small->setDirectionalHrir(true)&&large->setDirectionalHrir(true));
            assert(small->setShape(SonKuRoomShape::Square));
            assert(large->setGeometry({28.8f,28.8f,1.6f}));
            assert(small->setAmount(.55f)&&large->setAmount(.55f));
            int firstSmall=-1,firstLarge=-1;
            for(int i=0;i<7500;++i){
                const float input=i==0?.5f:0.f;
                const auto a=small->process(input,input,0.,0.f);
                const auto b=large->process(input,input,0.,0.f);
                if(firstSmall<0&&(a.left!=0.f||a.right!=0.f))firstSmall=i;
                if(firstLarge<0&&(b.left!=0.f||b.right!=0.f))firstLarge=i;
            }
            assert(firstSmall>100&&firstLarge>firstSmall+800);
        }
    }
    std::puts("PASS P2 directional reflected-KEMAR 44.1/48k, mirror, orbit chunk parity, near/far, OFF");
}
