// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// ArSonMix LMS M11: versioned, DEVICE-INDEPENDENT offline speaker design.
// This is exclusively session/CONTROL data. There is intentionally no
// callback ordinal, ASIO writer, calibration authority or ARM capability.
#include "PreparedSpeakerSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace armix::lms {
struct OfflineSpeakerDraft final {
    static constexpr int formatVersion=1;
    int version=formatVersion;
    double designRateHz=48000.; // reference for cutoff/EQ validity, never rebinds device
    int ways=2;
    bool stereo=true;
    std::vector<double> crossoverHz;
    // OutputPatch reused only as a familiar *unassigned* DSP data carrier:
    // portIdentity MUST be blank, all outputs muted and voltageModel empty.
    std::vector<OutputPatch> outputs;
};
class OfflineSpeakerDraftPolicy final {
    static bool token(const std::string& s,std::size_t max) noexcept {
        return !s.empty()&&s.size()<=max&&
            std::any_of(s.begin(),s.end(),[](unsigned char c){return c!=32;})&&
            std::all_of(s.begin(),s.end(),
                [](unsigned char c){return c>=32&&c<=126;});
    }
public:
    [[nodiscard]] static bool valid(const OfflineSpeakerDraft& d) {
        if(d.version!=OfflineSpeakerDraft::formatVersion||
           !std::isfinite(d.designRateHz)||d.designRateHz<8000.||
           d.designRateHz>384000.||d.ways<1||d.ways>16||
           d.crossoverHz.size()!=static_cast<std::size_t>(d.ways-1)||
           d.outputs.size()>256)return false;
        double previous=15.;
        for(const auto hz:d.crossoverHz) {
            if(!std::isfinite(hz)||hz<20.||hz<=previous*1.01||
               hz>=d.designRateHz*.45)return false;
            previous=hz;
        }
        std::unordered_set<std::string> seen;
        for(const auto& p:d.outputs) {
            if(!token(p.outputId,256)||!seen.insert(p.outputId).second||
               !p.portIdentity.empty()||!p.mute||
               p.confirmedSpeakerProfile||p.voltageModel.has_value()||
               p.sourceSide<0||p.sourceSide>(d.stereo?1:0)||
               p.section<0||p.section>=d.ways||
               !std::isfinite(p.gainDb)||p.gainDb< -60.||p.gainDb>6.||
               !std::isfinite(p.delayMs)||p.delayMs<0.||p.delayMs>500.||
               !std::isfinite(p.ceilingDbfs)||p.ceilingDbfs< -60.||
               p.ceilingDbfs>0.||p.peq.size()>8)return false;
            for(const auto& b:p.peq) {
                if(!std::isfinite(b.frequencyHz)||b.frequencyHz<20.||
                   b.frequencyHz>=d.designRateHz*.45||
                   !std::isfinite(b.q)||b.q<.1||b.q>18.||
                   !std::isfinite(b.gainDb)||b.gainDb< -18.||
                   b.gainDb>18.)return false;
            }
        }
        return true;
    }

    // The accepted native draft is the source of parameters. Never serialize
    // its device epoch, graph revision, output pins or claimed calibration:
    // opening .smix always yields an unassigned and fully muted snapshot.
    static bool detach(const SpeakerSystemRequest& source,
                       OfflineSpeakerDraft& destination) {
        if(!source.deviceEpoch||!source.graphRevision)return false;
        OfflineSpeakerDraft next;
        next.designRateHz=source.sampleRate;
        next.ways=source.ways;next.stereo=source.stereo;
        next.crossoverHz=source.crossoverHz;
        next.outputs=source.outputs;
        for(std::size_t i=0;i<next.outputs.size();++i) {
            auto& output=next.outputs[i];
            // Current GUI identifies an output by its *physical port label*.
            // Store an independent logical identity when they are the same:
            // no ASIO/device path can later masquerade as a reconnect token.
            if(output.outputId==output.portIdentity)
                output.outputId="OUT-"+std::to_string(i+1);
            output.portIdentity.clear();
            output.mute=true;
            output.confirmedSpeakerProfile=false;
            output.voltageModel.reset();
        }
        if(!valid(next))return false;
        destination=std::move(next);
        return true;
    }

    // Pure logical reuse. A zero-epoch, zero-pin request is deliberately
    // UNPREPARABLE and cannot be passed to hardware output reservation.
    // Only explicit verified-device re-selection may create a fresh request.
    static bool restoreDisconnected(const OfflineSpeakerDraft& saved,
                                    SpeakerSystemRequest& destination) {
        if(!valid(saved))return false;
        SpeakerSystemRequest next;
        next.sampleRate=saved.designRateHz;
        next.ways=saved.ways;
        next.stereo=saved.stereo;
        next.crossoverHz=saved.crossoverHz;
        next.outputs=saved.outputs;
        // deviceEpoch, graphRevision, activeOutputChannels remain 0;
        // verifiedPorts empty; every output remains mute and UNASSIGNED.
        destination=std::move(next);
        return true;
    }
};
} // namespace armix::lms
