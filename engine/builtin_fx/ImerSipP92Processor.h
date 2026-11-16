// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ImerSipP92Scene.h"
#include "SonKuSpatialProcessor.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

// ImerSip identity/state/UI are independent. Audio uses the SAME
// SonKuSpatialProcessor -> SonKuSpatialCore (mode 9) and same audio ACK.
// No P8 ImerSipDsp, second HRTF, stereo widening or enhancer is instantiated.
namespace armix::builtin_fx {
class ImerSipP92Processor final : public SonKuSpatialProcessor {
public:
    // Public name may change; persisted .smix FX ID must not.
    static constexpr const char* catalogIdentity="armix.builtin.imersip.v1";
    static constexpr const char* productDisplayName="ImmerSIB";
    ImerSipP92Processor()
        : SonKuSpatialProcessor(imersipP92Factory(2)) {
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
        // Distinct from 8D-SonKu P9.9 opt-in; original session ABI unchanged.
        optInImmerSIBVocalRoom();
#endif
    }
    const juce::String getName() const override {return productDisplayName;}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;

    // Editor gesture serials survive closing/reopening this processor's UI.
    std::uint64_t nextEditorRequestId() noexcept {
        if(++issued_==0)++issued_;
        return issued_;
    }
    bool queueSettings(const armix::sonku::Settings& scene,
                       std::uint64_t requestId) noexcept {
        if(!validImerSipP92Scene(scene) || requestId==0)return false;
        if(requestId>issued_)issued_=requestId;
        return SonKuSpatialProcessor::queueSettings(scene,requestId);
    }
    bool pollCompletion(SonKuSpatialProcessor::Completion& completed) noexcept {
        if(!SonKuSpatialProcessor::pollCompletion(completed))return false;
        lastCompletedRequest_=completed.requestId;
        lastCompletionApplied_=completed.applied;
        return true;
    }
    std::uint64_t lastCompletedRequestId() const noexcept {
        return lastCompletedRequest_;
    }
    bool lastCompletionApplied() const noexcept {
        return lastCompletionApplied_;
    }

    void getStateInformation(juce::MemoryBlock& destination) override {
        // Persist the new IMRS v6 identity, but embed the AUTHORITATIVE
        // native SONK v11 settings verbatim, so no shadow state is invented.
        // Session capture must use accepted audio settings, not UI drafts.
        SonKuSpatialProcessor::Completion done{};
        while(pollCompletion(done)){}
        juce::MemoryBlock native;
        SonKuSpatialProcessor::getStateInformation(native);
        juce::MemoryOutputStream out(destination,false);
        out.writeInt(0x494D5253); // IMRS
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
        out.writeInt(7); // IMRS v7: SONK v12 + typed scene, 75 bytes
#else
        out.writeInt(6); // IMRS v6: SONK v11, 71 bytes
#endif
        out.write(native.getData(),native.getSize());
    }

    bool restoreUnpublishedState(const void* data,int bytes) {
        if(!data || bytes<8)return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(bytes),false);
        if(in.readInt()!=0x494D5253)return false;
        const int version=in.readInt();
        if(version==6
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
           || version==7
#endif
           ) {
            // Accept historical 71-byte IMRS v6/SONK v11 (voicing=0)
            // and new 75-byte IMRS v7/SONK v12 atomically.
            const int nativeBytes=version==7?67:63;
            if(bytes!=nativeBytes+8)return false;
            const auto* raw=static_cast<const std::uint8_t*>(data)+8;
            juce::MemoryInputStream check(raw,static_cast<size_t>(nativeBytes),false);
            if(check.readInt()!=0x534F4E4B ||
               check.readInt()!=(version==7?12:11))return false;
            // Preflight the exact SONK v11 wire schema in a local POD.
            // Parent restoration may change its accepted scene/core; doing
            // that before validating ImerSip would let an invalid v6
            // session silently replace approved world-speaker P9 mode 9.
            auto next=imersipP92ReferenceScene();
            next.startAzimuthDegrees=check.readFloat();
            next.revolutionSeconds=check.readFloat();
            next.stereoSpreadDegrees=check.readFloat();
            next.spatialMix=check.readFloat();
            next.outputTrim=check.readFloat();
            const int moving=check.readByte();
            const int clockwise=check.readByte();
            next.moving=moving!=0;
            next.clockwise=clockwise!=0;
            next.spacePreset=check.readInt();
            next.sourceDistanceMeters=check.readFloat();
            next.roomAmount=check.readFloat();
            next.ambience=check.readFloat();
            next.directElevationDegrees=check.readFloat();
            next.renderMode=check.readInt();
            const int incompatibleLf=check.readByte();
            next.musicCompatibleLowFrequency=incompatibleLf!=0;
            next.headYawDegrees=check.readFloat();
            next.hrtfElevationTrimDegrees=check.readFloat();
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
            if(version==7)next.sceneVoicing=check.readInt();
#endif
            if(!check.isExhausted() || (moving!=0&&moving!=1) ||
               (clockwise!=0&&clockwise!=1) ||
               (incompatibleLf!=0&&incompatibleLf!=1) ||
               !armix::sonku::valid(next) || next.renderMode!=9 ||
               next.moving || next.musicCompatibleLowFrequency ||
               next.startAzimuthDegrees!=0.f)return false;
            // Legacy 1.0m / 4-10m sessions migrate to the new listening
            // envelope. Keep voicing, room, gain, IMRS bytes and FX ID.
            next.sourceDistanceMeters=std::clamp(next.sourceDistanceMeters,
                imersipP92MinDistance(),imersipP92MaxDistance(next.spacePreset));
            return validImerSipP92Scene(next) && adoptUnpublishedScene(next);
        }
        // Original IMRS v1-v5: read the actual previous format, fail closed
        // on corrupt bytes and map only parameters with a real P9 counterpart.
        if(version<1||version>5 ||
           bytes!=(version==1?32:version==2?36:
                   version==3?44:version==4?48:52))return false;
        const int venue=in.readInt();
        const float immersion=in.readFloat();
        const float space=in.readFloat();
        const float width=in.readFloat();
        const float bassImpact=in.readFloat();
        const float trim=in.readFloat();
        const float detail=version>=2?in.readFloat():0.f;
        const float stereoKeep=version>=3?in.readFloat():1.f;
        const float late=version>=3?in.readFloat():.33f;
        const float depth=version>=4?in.readFloat():0.f;
        const float earAlign=version>=5?in.readFloat():0.f;
        const auto unit=[](float x){return std::isfinite(x)&&x>=0.f&&x<=1.f;};
        if(!in.isExhausted() || venue<0 || venue>2 ||
           !unit(immersion)||!unit(space)||!unit(width)||
           !unit(bassImpact)||!unit(detail)||!unit(stereoKeep)||
           !unit(late)||!unit(depth)||!std::isfinite(trim)||
           trim<.25f||trim>1.5f||!std::isfinite(earAlign)||
           earAlign< -30.f||earAlign>20.f)return false;
        auto next=imersipP92ReferenceScene();
        next.spacePreset=venue==0?3:venue==1?2:1;
        next.spatialMix=immersion;
        next.roomAmount=space;
        next.stereoSpreadDegrees=30.f+150.f*width;
        next.ambience=late;
        next.sourceDistanceMeters=std::clamp(1.6f+5.8f*depth,
            imersipP92MinDistance(),imersipP92MaxDistance(next.spacePreset));
        next.outputTrim=trim;
        next.hrtfElevationTrimDegrees=earAlign;
        // Bass Impact, AIR, Stereo Keep were P8-only enhancer axes.
        // Deliberately DO NOT recreate P8 dry SIDE/headphone bass, which
        // would destroy the owner-approved external P9 scene.
        return validImerSipP92Scene(next) && adoptUnpublishedScene(next);
    }
    // JUCE's public AudioProcessor restore entrypoint is virtual. Without
    // this override it would call the inherited SONK parser, silently
    // rejecting an IMRS show even though the native FX registry restores it.
    void setStateInformation(const void* data,int bytes) override {
        (void)restoreUnpublishedState(data,bytes);
    }
protected:
    bool admitQueuedScene(
        const armix::sonku::Settings& next) const noexcept override {
        return validImerSipP92Scene(next);
    }
private:
    std::uint64_t issued_=0,lastCompletedRequest_=0;
    bool lastCompletionApplied_=false;
};
} // namespace armix::builtin_fx
#include "ImerSipP92Editor.h"
