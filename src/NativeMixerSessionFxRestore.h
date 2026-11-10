// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionDocument.h"
#include "VstBusLayout.h"
#include "Vst3DescriptionCache.h"
#include "NativeBuiltInFx.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>

// Prepare ALL owned VST3 processors on CONTROL before the one ChannelId PCM
// publisher commit. Never re-use a previous device epoch's processor.
namespace armix::session {
class NativeMixerSessionFxRestore final {
public:
    using Bank=armix::mixer::DynamicChannelFxLeaseBank;
    using Lease=armix::mixer::LiveChannelFxGraph::InstanceLease;
    // One factory for persisted inserts and hardware-independent editor instances.
    static juce::String prepareProcessor(const StoredInsert& insert,
            const armix::shows::BinaryState* state,
            juce::AudioPluginFormatManager& formats,double sampleRate,int frames,
            std::unique_ptr<juce::AudioProcessor>& out) {
        if(const auto* builtIn=armix::builtin_fx::findNativeBuiltInFx(insert.catalogId)) {
            auto processor=builtIn->prepare(sampleRate,frames,
                state?state->bytes.getData():nullptr,
                state?static_cast<int>(state->bytes.getSize()):0);
            // An explicitly supplied empty blob was rejected before the
            // registry was introduced, even though its data pointer is null.
            if(!processor || (state && state->bytes.isEmpty()))return builtIn->restoreRejected;
            out=std::move(processor);
            return {};
        }
        auto* format=formats.getFormat(0);
        if(!format || !format->fileMightContainThisPluginType(insert.path))
            return "Saved VST3 unavailable: "+insert.catalogId;
        const auto chosen=armix::vst::DescriptionCache::resolve(*format,insert.path,insert.uid);
        if(!chosen)return "Saved VST3 identity not found: "+insert.catalogId;
        juce::String pluginError;
        auto processor=formats.createPluginInstance(*chosen,sampleRate,frames,pluginError);
        if(!processor)return "VST3 instance unavailable: "+pluginError;
        const auto layout=armix::vst::prepareStereoEffectLayout(*processor);
        if(layout.isNotEmpty())return "Saved VST3 stereo layout rejected: "+layout;
        processor->prepareToPlay(sampleRate,frames);
        if(state)processor->setStateInformation(state->bytes.getData(),
            static_cast<int>(state->bytes.getSize()));
        out=std::move(processor);
        return {};
    }
    static juce::String prepare(const MixerSessionDocument& doc,
             const std::vector<armix::shows::BinaryState>& states,
             juce::AudioPluginFormatManager& formats,double sampleRate,
             int frames,std::uint64_t epoch,std::uint64_t pcmRevision,
             std::shared_ptr<Bank>& result) {
        if(!(sampleRate>=8000.&&sampleRate<=384000.)||frames<32||
           frames>armix::mixer::PreparedMixerBlock::hardFrameLimit||
           epoch==0||pcmRevision==0)
            return "Invalid driver/epoch for VST3 restoration";
        std::vector<Bank::Assignment> assignments;
        assignments.reserve(doc.roster.size());
        // Index ONCE. A large dynamic mixer must not rescan every channel
        // against all VST3 inserts and binary states at every restart.
        // Manifest validity is established by Codec; preserve slot order.
        std::unordered_map<std::uint64_t,std::vector<const StoredInsert*>> byChannel;
        std::unordered_map<std::string,const armix::shows::BinaryState*> byHash;
        byChannel.reserve(doc.roster.size());
        byHash.reserve(states.size());
        for(const auto& insert:doc.inserts)
            byChannel[insert.channel.value].push_back(&insert);
        for(const auto& state:states)
            if(!byHash.emplace(state.sha256.toStdString(),&state).second)
                return "Duplicate mixer vendor-state blob identity";
        try {
            for(const auto& ch:doc.roster.channels()) {
                Bank::Assignment lane;
                lane.id=ch.id;lane.format=ch.format;
                if(std::find(doc.rackBypassed.begin(),doc.rackBypassed.end(),ch.id)
                   !=doc.rackBypassed.end())
                    lane.rackBypassed=std::make_shared<std::atomic<bool>>(true);
                const auto laneIt=byChannel.find(ch.id.value);
                if(laneIt!=byChannel.end())
                for(const StoredInsert* stored:laneIt->second) {
                    const auto& insert=*stored;
                    if(insert.slot!=lane.inserts.size())
                        return "Saved FX slots are not contiguous";
                    const auto found=byHash.find(insert.sha256.toStdString());
                    if(found==byHash.end())
                        return "Missing saved VST3 state";
                    const auto& state=*found->second;
                    if(state.bytes.getSize()>8U*1024U*1024U||
                       juce::SHA256(state.bytes).toHexString()!=insert.sha256)
                        return "Missing or corrupt saved VST3 state";
                    std::unique_ptr<juce::AudioProcessor> processor;
                    if(const auto error=prepareProcessor(insert,&state,formats,
                            sampleRate,frames,processor);error.isNotEmpty())return error;
                    const int latency=processor->getLatencySamples();
                    if(latency<0||
                       latency>armix::dsp::StereoLatencyDelay::maximumSamples)
                        return "Saved FX latency exceeds graph PDC budget";
                    // The durable .smix catalog ID and plugin state are unchanged.
                    // Old sessions may still contain the historic visible label;
                    // show the current built-in product name after each restore.
                    // Do not canonicalize external VST3 names or other inserts.
                    const juce::String displayName=insert.catalogId==
                        armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity
                            ? processor->getName() : insert.name;
                    auto owner=std::make_shared<armix::mixer::LiveChannelFxGraph::FxInstance>(
                        std::move(processor),insert.catalogId,displayName,
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
