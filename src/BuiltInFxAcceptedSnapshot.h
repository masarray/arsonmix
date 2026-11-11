// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "BuiltInFxUserPresetStore.h"
#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include "../engine/builtin_fx/ChannelCompressorProcessor.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#include "../engine/builtin_fx/VocalThrowProcessor.h"

// CONTROL THREAD ONLY. One portable, catalog-scoped snapshot adapter for all
// built-in insert types. A native lease + quiescent accepted DSP state MUST be
// checked by the owner before calling acceptedSettings(). This adapter never
// constructs an insert, queues audio, touches playback histories or serializes
// session-specific source ChannelIds.
namespace armix::storage {
struct BuiltInFxAcceptedSnapshot final {
    static bool matchesReady(juce::AudioProcessor* p,const juce::String& catalog) noexcept {
        if(!p)return false;
        if(catalog==armix::builtin_fx::ChannelEqProcessor::catalogIdentity) {
            const auto* eq=dynamic_cast<armix::builtin_fx::ChannelEqProcessor*>(p);
            return eq&&eq->isReady();
        }
        if(catalog==armix::builtin_fx::ChannelCompressorProcessor::catalogIdentity) {
            const auto* comp=dynamic_cast<armix::builtin_fx::ChannelCompressorProcessor*>(p);
            return comp&&comp->isReady();
        }
        if(catalog==armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity) {
            const auto* harmony=dynamic_cast<armix::builtin_fx::ChannelHarmonyProcessor*>(p);
            return harmony&&harmony->isReady();
        }
        if(catalog==armix::builtin_fx::VocalThrowProcessor::catalogIdentity) {
            const auto* vthrow=dynamic_cast<armix::builtin_fx::VocalThrowProcessor*>(p);
            return vthrow&&vthrow->isReady();
        }
        return false; // External VST3/factory identity is never admitted.
    }

    static juce::var acceptedSettings(juce::AudioProcessor* p,
                                      const juce::String& catalog) {
        if(!matchesReady(p,catalog))return {};
        auto data=std::make_unique<juce::DynamicObject>();
        if(catalog==armix::builtin_fx::ChannelEqProcessor::catalogIdentity) {
            const auto bands=static_cast<armix::builtin_fx::ChannelEqProcessor*>(p)
                ->acceptedBands();
            juce::var items(juce::Array<juce::var>{});
            for(const auto& band:bands) {
                auto b=std::make_unique<juce::DynamicObject>();
                b->setProperty("enabled",band.enabled);
                b->setProperty("frequencyHz",band.frequencyHz);
                b->setProperty("gainDb",band.gainDb);
                b->setProperty("q",band.q);
                b->setProperty("slope",band.slope==armix::builtin_fx::Slope::db24?24:12);
                items.append(juce::var(b.release()));
            }
            data->setProperty("bands",items);
        }else if(catalog==armix::builtin_fx::ChannelCompressorProcessor::catalogIdentity) {
            const auto v=static_cast<armix::builtin_fx::ChannelCompressorProcessor*>(p)
                ->acceptedSettings();
            data->setProperty("thresholdDb",v.thresholdDb);
            data->setProperty("ratio",v.ratio);
            data->setProperty("attackMs",v.attackMs);
            data->setProperty("releaseMs",v.releaseMs);
            data->setProperty("kneeDb",v.kneeDb);
            data->setProperty("makeupDb",v.makeupDb);
            data->setProperty("mix",v.mix);
        }else if(catalog==armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity) {
            const auto v=static_cast<armix::builtin_fx::ChannelHarmonyProcessor*>(p)
                ->acceptedSettings();
            data->setProperty("key",v.key);
            data->setProperty("scale",v.scale);
            data->setProperty("voices",v.voices);
            data->setProperty("voiceMode",v.voiceMode);
            data->setProperty("correction",v.correction);
            data->setProperty("wet",v.wet);
            data->setProperty("space",v.space);
            data->setProperty("color",v.color);
            data->setProperty("gainDb",v.gainDb);
            // Music source ChannelId is session-bound and excluded.
        }else if(catalog==armix::builtin_fx::VocalThrowProcessor::catalogIdentity) {
            const auto v=static_cast<armix::builtin_fx::VocalThrowProcessor*>(p)
                ->acceptedSettings();
            data->setProperty("mode",v.mode);
            data->setProperty("bpm",v.bpm);
            data->setProperty("division",v.division);
            data->setProperty("repeats",v.repeats);
            data->setProperty("captureMs",v.captureMs);
            data->setProperty("silenceMs",v.silenceMs);
            data->setProperty("thresholdDb",v.thresholdDb);
            data->setProperty("wet",v.wet);
            data->setProperty("decay",v.decay);
            data->setProperty("ducking",v.ducking);
            // Music source ChannelId, detected BPM and echo history excluded.
        }else return {};
        const juce::var result(data.release());
        return BuiltInFxUserPresetStore::validateSettings(catalog,result)?
               result:juce::var{};
    }
};
} // namespace armix::storage
