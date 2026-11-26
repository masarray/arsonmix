// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <bit>
#include <cstdint>

namespace armix::rt {
// Policy is prepared from explicit processor capability, never name, cost,
// zero input, reported tail length, or vendor metadata alone.
enum class FxIdleCapability : std::uint8_t { conservative, exactNative, qualifiedTail };
enum class FxRenderAction : std::uint8_t { process, alignedDry, probeExact, probeTail };
constexpr FxRenderAction chooseFxRenderAction(FxIdleCapability capability,
        bool continuousBypass,bool requestedBypass,bool fullyBypassed,
        bool fading,bool latencyFault,bool parkedBypass=false) noexcept {
    if(latencyFault||(fullyBypassed&&!continuousBypass))return FxRenderAction::alignedDry;
    if(continuousBypass&&requestedBypass&&fullyBypassed&&!fading&&parkedBypass)
        return FxRenderAction::alignedDry;
    if(continuousBypass||requestedBypass||fullyBypassed||fading)return FxRenderAction::process;
    if(capability==FxIdleCapability::exactNative)return FxRenderAction::probeExact;
    if(capability==FxIdleCapability::qualifiedTail)return FxRenderAction::probeTail;
    return FxRenderAction::process;
}
// Sampled diagnostic evidence only. Never grant sleep from this observation.
// Signed zero is silent; subnormal, NaN and infinity are never silent, even
// under FTZ/DAZ. No extra scan on unsampled processor invocations.
inline bool exactSilentStereo(const float* l,const float* r,int n) noexcept {
    if(!l||!r||n<=0)return false;
    for(int i=0;i<n;++i)
        if(((std::bit_cast<std::uint32_t>(l[i])|
             std::bit_cast<std::uint32_t>(r[i]))&0x7fffffffu)!=0)return false;
    return true;
}
enum class FxReview : std::uint8_t { noEvidence, keep, reviewBudget, reviewSilentCost, reviewLatentBypass, latencyFault };
constexpr FxReview reviewFx(bool valid,std::uint64_t calls,std::uint64_t over,
        std::uint64_t worst,std::uint64_t observed,std::uint64_t silent,
        int latency,bool bypass,bool fault) noexcept {
    if(fault)return FxReview::latencyFault;
    if(latency>0&&bypass)return FxReview::reviewLatentBypass;
    if(!valid||!calls)return FxReview::noEvidence;
    // 25% of one quantum is a review threshold, NOT a guaranteed overload
    // or suspend eligibility. A quiet sample is not proof of drained state.
    if(observed>=64&&silent==observed&&worst>=250)return FxReview::reviewSilentCost;
    if(over||worst>=250)return FxReview::reviewBudget;
    return FxReview::keep;
}
// Stable CONTROL ranking: faults first, then latent bypass, then
// repeated pressure in the last complete window, then historical peak.
// A sampled window is not a deadline prediction or suspension permission.
constexpr std::uint64_t fxReviewPriority(FxReview action,unsigned windowOver,
        unsigned windowMean,std::uint64_t worst) noexcept {
    const std::uint64_t category=action==FxReview::latencyFault?4:
        action==FxReview::reviewLatentBypass?3:
        (action==FxReview::reviewBudget||action==FxReview::reviewSilentCost)?2:0;
    const auto over=windowOver>32?32:windowOver;
    const auto mean=windowMean>0xfffffu?0xfffffu:windowMean;
    return (category<<58)|(static_cast<std::uint64_t>(over)<<52)|
        (static_cast<std::uint64_t>(mean)<<32)|(worst>0xffffffffu?0xffffffffu:worst);
}
inline const char* fxReviewName(FxReview v) noexcept {
    switch(v){
        case FxReview::latencyFault:return "REBUILD_LATENCY_WHILE_DISARMED";
        case FxReview::reviewLatentBypass:return "REVIEW_LATENT_BYPASS";
        case FxReview::reviewSilentCost:return "REVIEW_SAMPLED_SILENT_COST";
        case FxReview::reviewBudget:return "REVIEW_BLOCK_BUDGET";
        case FxReview::keep:return "KEEP";
        default:return "NO_EVIDENCE";
    }
}
} // namespace armix::rt
