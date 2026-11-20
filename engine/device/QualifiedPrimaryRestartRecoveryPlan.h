// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "MultiDeviceInputPlan.h"
#include "../core/DynamicPreparedMixerBlock.h"
#include <algorithm>
#include <utility>

// IO-2D CONTROL-only, non-destructive restart fallback. Keep the entire
// ChannelId/WIRE/VST3 graph, but remove B's old physical source assignments
// from a DETACHED request after the primary ASIO clock changes. Never render
// or authorize an ordinal from the previous secondary device lease.
namespace armix::device {
class QualifiedPrimaryRestartRecoveryPlan final {
public:
    static bool stage(const MultiDeviceInputPlan& prior,
                      const armix::mixer::DynamicGraphRequest& accepted,
                      int verifiedPrimaryInputs,
                      armix::mixer::DynamicGraphRequest& destination) {
        using namespace armix::mixer;
        if(prior.primaryEpoch()==0 || prior.deviceCount()!=2 ||
           !prior.device(0).primary || prior.device(0).firstVirtual!=0 ||
           prior.device(0).activePins<=0 ||
           verifiedPrimaryInputs!=prior.device(0).activePins ||
           prior.virtualInputCount()!=accepted.physicalInputs ||
           prior.virtualInputCount()<=verifiedPrimaryInputs)
            return false;
        int sourceCount=0;
        for(const auto& source:accepted.sources) {
            if(source.kind!=DynamicSourceKind::physical)continue;
            ++sourceCount;
            bool matched=false;
            for(int i=0;i<prior.sourceCount();++i) {
                const auto& bound=prior.source(i);
                if(source.id==bound.channel &&
                   source.inputL==bound.inputL &&
                   source.inputR==bound.inputR &&
                   source.stereo==bound.stereo) {
                    matched=true;break;
                }
            }
            if(!matched || source.inputL<0 ||
               source.inputL>=prior.virtualInputCount() ||
               (source.stereo &&
                (source.inputR<0 ||
                 source.inputR>=prior.virtualInputCount())))
                return false;
            const auto device=prior.slot(source.inputL).device;
            if((device==0 && source.inputL>=verifiedPrimaryInputs) ||
               (device!=0 && source.inputL<verifiedPrimaryInputs) ||
               (source.stereo &&
                prior.slot(source.inputR).device!=device))
                return false;
        }
        if(sourceCount!=prior.sourceCount())return false;
        auto candidate=accepted; // only CONTROL, never the live graph
        std::erase_if(candidate.sources,[&](const auto& source){
            return source.kind==DynamicSourceKind::physical &&
                   prior.slot(source.inputL).device!=0;
        });
        candidate.physicalInputs=verifiedPrimaryInputs;
        destination=std::move(candidate); // atomic detached result
        return true;
    }
};
} // namespace armix::device
