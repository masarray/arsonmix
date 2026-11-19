// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "MultiDeviceInputPlan.h"
#include "../core/DynamicPreparedMixerBlock.h"
#include <algorithm>
#include <utility>

// IO-2B: CONTROL-only, non-destructive conversion between one accepted
// verified DeviceKey/pin namespace and the EXISTING ChannelId PCM graph.
// No AudioDeviceManager, IO callback, DSP, VST3 instance or JS truth owner.
// The caller must publish both this namespace and the graph through ONE
// immutable native render lease. stage() alone NEVER authorizes live PCM.
namespace armix::device {

class QualifiedInputGraphBridge final {
public:
    enum class Fault : std::uint8_t {
        none, invalidPlan, unqualifiedExistingSource, playerSourceCollision,
        invalidPatch, duplicateSource
    };

    static bool stage(const MultiDeviceInputPlan& plan,
                      const armix::mixer::DynamicGraphRequest& accepted,
                      armix::mixer::DynamicGraphRequest& destination,
                      Fault& fault) {
        using namespace armix::mixer;
        fault=Fault::none;
        if(plan.primaryEpoch()==0 || plan.deviceCount()<1 ||
           plan.virtualInputCount()<0 ||
           plan.virtualInputCount()>MultiDeviceInputPlan::maxVirtualInputs) {
            fault=Fault::invalidPlan;return false;
        }
        // CONTROL only. Keep the entire old request, including Player decks,
        // maxFrames, scratch admission, and exact saved FX/channel semantics.
        auto proposed=accepted;
        proposed.physicalInputs=plan.virtualInputCount();
        for(auto& source:proposed.sources) {
            if(source.kind!=DynamicSourceKind::physical)
                continue;
            const PreparedChannelSource* qualified=nullptr;
            for(int i=0;i<plan.sourceCount();++i)
                if(plan.source(i).channel==source.id) {
                    qualified=&plan.source(i);break;
                }
            // Never silently remove a previously accepted microphone patch
            // when a new device-map draft omits its ChannelId.
            if(!qualified) {
                fault=Fault::unqualifiedExistingSource;return false;
            }
            source.inputL=qualified->inputL;
            source.inputR=qualified->inputR;
            source.stereo=qualified->stereo;
        }
        for(int i=0;i<plan.sourceCount();++i) {
            const auto& mapped=plan.source(i);
            const auto found=std::find_if(proposed.sources.begin(),
                proposed.sources.end(),[&](const auto& source) {
                    return source.id==mapped.channel;
                });
            if(found!=proposed.sources.end()) {
                if(found->kind!=DynamicSourceKind::physical) {
                    fault=Fault::playerSourceCollision;return false;
                }
                continue; // already replaced the SAME physical ChannelId
            }
            proposed.sources.push_back({mapped.channel,mapped.inputL,
                mapped.inputR,mapped.stereo,DynamicSourceKind::physical,0});
        }
        // Validation is against the virtual namespace, NOT primary driver
        // callback count. These are callback ordinals, not physical pin IDs.
        for(std::size_t i=0;i<proposed.sources.size();++i) {
            const auto& source=proposed.sources[i];
            if(!DynamicPreparedMixerBlock::validSourcePatch(proposed,source)) {
                fault=Fault::invalidPatch;return false;
            }
            for(std::size_t j=0;j<i;++j)
                if(proposed.sources[j].id==source.id) {
                    fault=Fault::duplicateSource;return false;
                }
        }
        destination=std::move(proposed); // only detached candidate
        return true;
    }
};
} // namespace armix::device
