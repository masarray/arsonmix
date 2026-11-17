// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuSpatialCore.h"

// The measured binaural DSP core accepts settings needed by both products.
// This is the control-plane contract for the original 8D-SonKu product,
// NOT another DSP, scene serializer or callback-time check. The original
// SonKu P9.9 room/distance ranges and scene IDs 0..3 remain unchanged.
// ImmerSIB has its separate, explicitly opted-in product admission rule.
namespace armix::sonku {
inline bool valid8DSonKuInsertSettings(const Settings& next) noexcept {
    if(!valid(next) || next.sourceDistanceMeters<1.6f ||
       next.roomAmount>1.f)return false;
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    if(next.sceneVoicing>p98::SceneVoicing::Cinema)return false;
#endif
    return true;
}
} // namespace armix::sonku
