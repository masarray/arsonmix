// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// One typed CONTROL-plane schema for the existing SonKu JUCE/JS bridge.
// No renderer, callback, allocation or additional FX authority lives here.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <memory>
namespace armix::builtin_fx::sonku_bridge {
inline constexpr int schemaVersion() noexcept {
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
    return 3;
#else
    return 2;
#endif
}
inline bool number(const juce::var& values,const char* key,float& result,
                   bool required) noexcept {
    const auto raw=values.getProperty(key,{});
    if(raw.isVoid())return !required;
    if(!raw.isDouble()&&!raw.isInt())return false;
    const double d=static_cast<double>(raw);
    if(!std::isfinite(d)||d< -1000000.||d>1000000.)return false;
    const auto f=static_cast<float>(d);
    if(!std::isfinite(f))return false;
    result=f;
    return true;
}
inline bool decodeSettings(const juce::var& values,armix::sonku::Settings& out) noexcept {
    if(!values.isObject())return false;
    armix::sonku::Settings proposed{};
    if(!number(values,"startAzimuthDegrees",proposed.startAzimuthDegrees,true)||
       !number(values,"revolutionSeconds",proposed.revolutionSeconds,true)||
       !number(values,"stereoSpreadDegrees",proposed.stereoSpreadDegrees,true)||
       !number(values,"spatialMix",proposed.spatialMix,true)||
       !number(values,"outputTrim",proposed.outputTrim,true))return false;
    const auto moving=values.getProperty("moving",{});
    const auto clockwise=values.getProperty("clockwise",{});
    if(!moving.isBool()||!clockwise.isBool())return false;
    proposed.moving=static_cast<bool>(moving);
    proposed.clockwise=static_cast<bool>(clockwise);
    const auto preset=values.getProperty("spacePreset",{});
    if(!preset.isVoid()){
        if(!preset.isInt())return false;
        proposed.spacePreset=static_cast<int>(preset);
    }
    if(!number(values,"sourceDistanceMeters",proposed.sourceDistanceMeters,false)||
       !number(values,"roomAmount",proposed.roomAmount,false)||
       !number(values,"ambience",proposed.ambience,false))return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
    // Legacy clients legitimately omit elevation; default exactly zero.
    if(!number(values,"directElevationDegrees",
               proposed.directElevationDegrees,false))return false;
#else
    // Never falsely acknowledge 3D JSON in a public v2-only binary.
    if(!values.getProperty("directElevationDegrees",{}).isVoid())return false;
#endif
    if(!armix::sonku::valid(proposed))return false;
    out=proposed;
    return true;
}
inline juce::var acceptedSettings(const armix::sonku::Settings& s) {
    auto values=std::make_unique<juce::DynamicObject>();
    values->setProperty("startAzimuthDegrees",s.startAzimuthDegrees);
    values->setProperty("revolutionSeconds",s.revolutionSeconds);
    values->setProperty("stereoSpreadDegrees",s.stereoSpreadDegrees);
    values->setProperty("spatialMix",s.spatialMix);
    values->setProperty("outputTrim",s.outputTrim);
    values->setProperty("moving",s.moving);
    values->setProperty("clockwise",s.clockwise);
    values->setProperty("spacePreset",s.spacePreset);
    values->setProperty("sourceDistanceMeters",s.sourceDistanceMeters);
    values->setProperty("roomAmount",s.roomAmount);
    values->setProperty("ambience",s.ambience);
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
    values->setProperty("directElevationDegrees",s.directElevationDegrees);
#endif
    return juce::var(values.release());
}
} // namespace armix::builtin_fx::sonku_bridge
