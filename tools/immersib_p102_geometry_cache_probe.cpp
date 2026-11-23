// SPDX-License-Identifier: AGPL-3.0-only
// Bit-exact P102.3 original 6-wall EARLY vs generated geometry-cache candidate.
// The approved SonKuEarlyReflections.h is included UNMODIFIED.
#include "SonKuEarlyReflectionsGeometryCache.h"
#include "engine/builtin_fx/ImerSipP92Scene.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {
using armix::sonku::SonKuEarlyReflections;
using armix::sonku::SonKuEarlyReflectionsGeometryCache;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::SonKuRoomGeometry;

std::uint64_t updateHash(std::uint64_t hash,float value) {
    hash^=std::bit_cast<std::uint32_t>(value);
    return hash*1099511628211ull;
}

bool sameBits(float a,float b) {
    return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);
}

bool runCase(int rate,int presetId) {
    auto reference=std::make_unique<SonKuEarlyReflections>();
    auto candidate=std::make_unique<SonKuEarlyReflectionsGeometryCache>();
    if(!reference->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch)
       || !candidate->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch))
        return false;
    auto scene=armix::builtin_fx::imersipP92Factory(presetId);
    const float width=28.8f,height=7.2f;
    SonKuRoomGeometry geometry{
        width,width,scene.sourceDistanceMeters,height,1.45f,1.45f
    };
    const float wet=std::min(1.f,scene.roomAmount);
    if(!reference->setRealtimeRoom(geometry,wet,true) ||
       !candidate->setRealtimeRoom(geometry,wet,true))
        return false;

    float yaw=0.f,spread=scene.stereoSpreadDegrees;
    std::uint64_t hash=14695981039346656037ull;
    constexpr int count=12544; // crosses the real 8192-sample delay ring wrap
    for(int i=0;i<count;++i) {
        // Same accepted native P102.3 controls on both independent stages.
        // Cover geometry invalidation, yaw, spread, ROOM off/on, ring wrap,
        // coherent, anti-phase, L-only, R-only, silence and transient impulses.
        if(i==2560)yaw=22.f;
        if(i==3584)spread=0.f;
        if(i==4608)spread=scene.stereoSpreadDegrees;
        if(i==5120) {
            if(!reference->setRealtimeRoom(geometry,0.f,false) ||
               !candidate->setRealtimeRoom(geometry,0.f,false))return false;
        }
        if(i==6400) {
            if(!reference->setRealtimeRoom(geometry,wet,true) ||
               !candidate->setRealtimeRoom(geometry,wet,true))return false;
        }
        if(i==8192) {
            geometry.distanceMeters+=0.1f;
            if(!reference->setRealtimeRoom(geometry,wet,true) ||
               !candidate->setRealtimeRoom(geometry,wet,true))return false;
        }
        if(i==9216){yaw=0.f;spread=scene.stereoSpreadDegrees;}
        const float l=0.064f*static_cast<float>(
            std::sin(double(i)*0.0137)+.3*std::sin(double(i)*0.053));
        const float r=0.057f*static_cast<float>(
            std::sin(double(i)*0.0181)-.2*std::sin(double(i)*0.042));
        float inL=l,inR=r;
        switch((i/797)%6) {
            case 1: inR=-l;break;          // anti-phase
            case 2: inR=0.f;break;         // left only
            case 3: inL=0.f;break;         // right only
            case 4: inL=inR=0.f;break;     // silence / reverb tail
            case 5: inL=i%97==0?.08f:0.f;inR=i%193==0?-.05f:0.f;break;
            default:break;
        }
        const bool focus=(i/1500)%2!=0;
        const auto before=reference->process(inL,inR,0.,spread,yaw,focus);
        const auto after=candidate->process(inL,inR,0.,spread,yaw,focus);
        if(!std::isfinite(before.left)||!std::isfinite(before.right)||
           !std::isfinite(after.left)||!std::isfinite(after.right)||
           !sameBits(before.left,after.left)||
           !sameBits(before.right,after.right)) {
            std::fprintf(stderr,"GEOM_CACHE_PCM_DRIFT rate=%d scene=%d "
                "sample=%d refL=%a optL=%a refR=%a optR=%a\n",
                rate,presetId,i,double(before.left),double(after.left),
                double(before.right),double(after.right));
            return false;
        }
        hash=updateHash(hash,before.left);
        hash=updateHash(hash,before.right);
    }
    const auto skips=candidate->skippedGeometryUpdates();
    if(skips<12) {
        std::fprintf(stderr,"GEOM_CACHE_NOT_EXERCISED rate=%d scene=%d skips=%llu\n",
            rate,presetId,static_cast<unsigned long long>(skips));
        return false;
    }
    std::printf("IMMERSIB_GEOMETRY_BITEXACT_PASS rate=%d preset=%d "
                "samples=%d skipped_geometry=%llu pcm_hash=%016llx\n",
                rate,presetId,count,
                static_cast<unsigned long long>(skips),
                static_cast<unsigned long long>(hash));
    return true;
}

} // namespace

int main() {
    // No extra PCM owner: instantiate the SAME original and derived
    // SonKuEarlyReflections implementation, never a host/ASIO double graph.
    for(int rate:{44100,48000}) {
        for(int preset=2;preset<=8;++preset) {
            if(!runCase(rate,preset))return 1;
        }
    }
    std::puts("IMMERSIB_GEOMETRY_LAB_PASS full_pcm_bits=IDENTICAL "
              "all_7_scenes=YES rates=44100_48000 original_pcm_source=LOCKED "
              "shipping_dsp=UNMODIFIED hardware=NOT_TESTED");
    return 0;
}
