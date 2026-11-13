// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include "../engine/builtin_fx/ChannelCompressorProcessor.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#include "../engine/builtin_fx/VocalThrowProcessor.h"
#include "../engine/builtin_fx/GuitarRigEditor.h"
#include "../engine/core/PreparedMixerBlock.h"
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>

namespace armix::builtin_fx {
// Control-side registration shared by live insert, offline edit and restore.
// The descriptor does not own a processor, callback, editor or render graph.
struct NativeBuiltInFx {
    using Prepare=std::unique_ptr<juce::AudioProcessor>(*)(double,int,const void*,int);
    const char* id;
    const char* name;
    const char* category;
    bool showInPicker;
    double minimumLiveRate;
    double maximumLiveRate;
    const char* hostUnavailable;
    const char* deviceChanged;
    const char* unsupportedFormat;
    const char* prepareRejected;
    const char* prepareException;
    const char* restoreRejected;
    Prepare prepare;
};

template<class Processor>
std::unique_ptr<juce::AudioProcessor> prepareNativeBuiltInFx(
        double rate,int frames,const void* state,int bytes) {
    // State bytes are CONTROL-only. A missing pointer with a positive size
    // must never be mistaken for a valid default, especially during restore.
    // Reject malformed device formats BEFORE constructing any DSP/hosted
    // processor. This is shared by EQ, Compressor, Harmonice, Vocal Throw,
    // offline .smix restore and future built-in catalog entries. A corrupt
    // rate/quantum must not cause extreme allocation inside prepareToPlay.
    // 16-frame ASIO is deliberately valid at the universal host boundary;
    // a processor may impose stricter limits via its own isReady() policy.
    if(!std::isfinite(rate) || rate<8000. || rate>384000. ||
       frames<16 || frames>armix::mixer::PreparedMixerBlock::hardFrameLimit ||
       bytes<0 || bytes>8*1024*1024 ||
       (state==nullptr && bytes!=0) || (state!=nullptr && bytes==0))
        return {};
    auto processor=std::make_unique<Processor>();
    processor->prepareToPlay(rate,frames);
    if(!processor->isReady() || (state && !processor->restoreUnpublishedState(state,bytes)))
        return {};
    return processor;
}

inline constexpr std::array nativeBuiltInFx{
    NativeBuiltInFx{ChannelEqProcessor::catalogIdentity,"Parametric EQ","EQ",true,8000.,384000.,
        "NATIVE_EQ_HOST_UNAVAILABLE","NATIVE_EQ_DEVICE_CHANGED",
        "NATIVE_EQ_UNSUPPORTED_AUDIO_FORMAT","NATIVE_EQ_PREPARE_REJECTED",
        "NATIVE_EQ_PREPARE_EXCEPTION","Saved native EQ state rejected; graph unchanged",
        prepareNativeBuiltInFx<ChannelEqProcessor>},
    NativeBuiltInFx{ChannelCompressorProcessor::catalogIdentity,"Compressor","Dynamics",true,8000.,384000.,
        "NATIVE_COMP_HOST_UNAVAILABLE","NATIVE_COMP_DEVICE_CHANGED",
        "NATIVE_COMP_UNSUPPORTED_AUDIO_FORMAT","NATIVE_COMP_PREPARE_REJECTED",
        "NATIVE_COMP_PREPARE_EXCEPTION","Saved native Compressor state rejected; graph unchanged",
        prepareNativeBuiltInFx<ChannelCompressorProcessor>},
    NativeBuiltInFx{ChannelHarmonyProcessor::catalogIdentity,"Harmonice","Utility",true,32000.,192000.,
        "NATIVE_HARMONY_HOST_UNAVAILABLE","NATIVE_HARMONY_DEVICE_CHANGED",
        "NATIVE_HARMONY_UNSUPPORTED_AUDIO_FORMAT","NATIVE_HARMONY_PREPARE_OR_PDC_REJECTED",
        "NATIVE_HARMONY_PREPARE_EXCEPTION","Saved native Harmony state rejected; graph unchanged",
        prepareNativeBuiltInFx<ChannelHarmonyProcessor>},
    NativeBuiltInFx{VocalThrowProcessor::catalogIdentity,"ArSon Vocal Throw","Utility",false,8000.,192000.,
        "NATIVE_THROW_HOST_UNAVAILABLE","NATIVE_THROW_DEVICE_CHANGED",
        "NATIVE_THROW_UNSUPPORTED_AUDIO_FORMAT","NATIVE_THROW_PREPARE_REJECTED",
        "NATIVE_THROW_PREPARE_EXCEPTION","Saved native Vocal Throw state rejected; graph unchanged",
        prepareNativeBuiltInFx<VocalThrowProcessor>},
    NativeBuiltInFx{GuitarRigProcessor::catalogIdentity,"Guitar FX Rig","Guitar",true,32000.,192000.,
        "NATIVE_GFX_HOST_UNAVAILABLE","NATIVE_GFX_DEVICE_CHANGED",
        "NATIVE_GFX_UNSUPPORTED_AUDIO_FORMAT","NATIVE_GFX_PREPARE_REJECTED",
        "NATIVE_GFX_PREPARE_EXCEPTION","Saved Guitar FX Rig state rejected; graph unchanged",
        prepareNativeBuiltInFx<GuitarRigProcessor>}
};

// Compile-time registration contract: one stable catalog identity per FX,
// with one prepare function and a bounded device-rate policy. The same
// validator is usable by a fixture registry without adding a product host
// branch or touching the realtime callback.
constexpr bool validNativeBuiltInFxCatalog(std::span<const NativeBuiltInFx> registry) {
    if(registry.empty())return false;
    for(std::size_t i=0;i<registry.size();++i) {
        const auto& fx=registry[i];
        if(!fx.id || !fx.name || !fx.category || !fx.prepare ||
           std::string_view(fx.id).empty() ||
           std::string_view(fx.name).empty() ||
           std::string_view(fx.category).empty() ||
           !(fx.minimumLiveRate>=8000.) ||
           !(fx.maximumLiveRate>=fx.minimumLiveRate) ||
           !(fx.maximumLiveRate<=384000.))
            return false;
        for(std::size_t j=i+1;j<registry.size();++j)
            if(registry[j].id && std::string_view(fx.id)==registry[j].id)
                return false;
    }
    return true;
}
static_assert(validNativeBuiltInFxCatalog(nativeBuiltInFx));

// One lookup contract for production registration and isolated extension
// fixtures. Catalog IDs are exact and are never inferred from a slot index.
inline const NativeBuiltInFx* findNativeBuiltInFxIn(
        std::span<const NativeBuiltInFx> registry,const juce::String& id) {
    if(id.isEmpty())return nullptr;
    for(const auto& fx:registry)
        if(fx.id && id==fx.id)return &fx;
    return nullptr;
}
inline const NativeBuiltInFx* findNativeBuiltInFx(const juce::String& id) {
    return findNativeBuiltInFxIn(nativeBuiltInFx,id);
}
} // namespace armix::builtin_fx
