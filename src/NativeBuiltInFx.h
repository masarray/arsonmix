// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include "../engine/builtin_fx/ChannelCompressorProcessor.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#include "../engine/builtin_fx/VocalThrowProcessor.h"
#include <array>
#include <memory>

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
        prepareNativeBuiltInFx<VocalThrowProcessor>}
};

inline const NativeBuiltInFx* findNativeBuiltInFx(const juce::String& id) {
    for(const auto& fx:nativeBuiltInFx)
        if(id==fx.id)return &fx;
    return nullptr;
}
} // namespace armix::builtin_fx
