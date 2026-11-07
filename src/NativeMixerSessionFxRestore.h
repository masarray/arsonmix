// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionDocument.h"
#include "VstBusLayout.h"
#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>
#include <algorithm>
#include <vector>

// Prepare ALL owned VST3 processors on CONTROL before the one ChannelId PCM
// publisher commit. Never re-use a previous device epoch's processor.
namespace armix::session {
class NativeMixerSessionFxRestore final {
public:
    using Bank=armix::mixer::DynamicChannelFxLeaseBank;
    using Lease=armix::mixer::LiveChannelFxGraph::InstanceLease;
    static juce::String prepare(const MixerSessionDocument& doc,
             const std::vector<armix::shows::BinaryState>& states,
             juce::AudioPluginFormatManager& formats,double sampleRate,
             int frames,std::uint64_t epoch,std::uint64_t pcmRevision,
             std::shared_ptr<Bank>& result) {
        if(!(sampleRate>=8000.&&sampleRate<=384000.)||frames<32||
           frames>armix::mixer::PreparedMixerBlock::hardFrameLimit||
           epoch==0||pcmRevision==0)
            return "Invalid driver/epoch for VST3 restoration";
        auto* format=formats.getFormat(0);
        std::vector<Bank::Assignment> assignments;
        assignments.reserve(doc.roster.size());
        try {
            for(const auto& ch:doc.roster.channels()) {
                Bank::Assignment lane;
                lane.id=ch.id;lane.format=ch.format;
                if(std::find(doc.rackBypassed.begin(),doc.rackBypassed.end(),ch.id)
                   !=doc.rackBypassed.end())
                    lane.rackBypassed=std::make_shared<std::atomic<bool>>(true);
                for(const auto& insert:doc.inserts) {
                    if(insert.channel!=ch.id)continue;
                    if(insert.slot!=lane.inserts.size())
                        return "Saved FX slots are not contiguous";
                    auto state=std::find_if(states.begin(),states.end(),
                        [&](const armix::shows::BinaryState& v){
                            return v.sha256==insert.sha256;
                        });
                    if(state==states.end()||
                       state->bytes.getSize()>8U*1024U*1024U||
                       juce::SHA256(state->bytes).toHexString()!=insert.sha256)
                        return "Missing or corrupt saved VST3 state";
                    std::unique_ptr<juce::AudioProcessor> processor;
                    if(insert.catalogId==
                       armix::builtin_fx::ChannelEqProcessor::catalogIdentity) {
                        auto eq=std::make_unique<armix::builtin_fx::ChannelEqProcessor>();
                        eq->prepareToPlay(sampleRate,frames);
                        if(!eq->isReady())return "Built-in EQ could not prepare";
                        processor=std::move(eq);
                    } else {
                        if(!format||
                           !format->fileMightContainThisPluginType(insert.path))
                            return "Saved VST3 unavailable: "+insert.catalogId;
                        juce::OwnedArray<juce::PluginDescription> descriptions;
                        format->findAllTypesForFile(descriptions,insert.path);
                        juce::PluginDescription* chosen=nullptr;
                        for(auto* d:descriptions)
                            if(d&&d->uniqueId==insert.uid&&!d->isInstrument)
                                {chosen=d;break;}
                        if(!chosen)return "Saved VST3 identity not found: "+insert.catalogId;
                        juce::String pluginError;
                        processor=formats.createPluginInstance(*chosen,sampleRate,frames,pluginError);
                        if(!processor)
                            return "VST3 instance unavailable: "+pluginError;
                        const auto layout=armix::vst::prepareStereoEffectLayout(*processor);
                        if(layout.isNotEmpty())
                            return "Saved VST3 stereo layout rejected: "+layout;
                        processor->prepareToPlay(sampleRate,frames);
                    }
                    processor->setStateInformation(state->bytes.getData(),
                         static_cast<int>(state->bytes.getSize()));
                    const int latency=processor->getLatencySamples();
                    if(latency<0||
                       latency>armix::dsp::StereoLatencyDelay::maximumSamples)
                        return "Saved FX latency exceeds graph PDC budget";
                    auto owner=std::make_shared<armix::mixer::LiveChannelFxGraph::FxInstance>(
                        std::move(processor),insert.catalogId,insert.name,
                        armix::mixer::PreparedMixerBlock::hardFrameLimit);
                    owner->bypassRequested.store(insert.bypass,std::memory_order_release);
                    lane.inserts.push_back(std::move(owner));
                }
                assignments.push_back(std::move(lane));
            }
            Bank prepared;
            const auto fault=Bank::prepare(epoch,pcmRevision,assignments,prepared);
            if(fault!=Bank::Fault::none)
                return "Saved native FX lease rejected: "+
                     juce::String(static_cast<int>(fault));
            result=std::make_shared<Bank>(std::move(prepared));
            return {};
        }catch(const std::exception& ex) {
            return "VST3 restore rejected: "+juce::String(ex.what());
        }catch(...) {return "VST3 restore aborted without PCM mutation";}
    }
};
} // namespace armix::session
