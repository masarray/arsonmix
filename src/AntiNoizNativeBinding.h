// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/builtin_fx/AntiNoizProcessor.h"
#include "NativeFxControlRequest.h"
#include "AntiNoizPortableSettings.h"
#include <cmath>
namespace armix::builtin_fx {
// CONTROL only: strict typed parameters, durable readback and actual observed
// audio telemetry. No graph authority or callback is owned here.
struct AntiNoizNativeBinding final : AntiNoizPortableSettings {
    using Core=AntiNoizProcessor::Core;
    using Snapshot=AntiNoizProcessor::Snapshot;
    static const char* learnName(AntiNoizProcessor::LearnStatus s) noexcept {
        switch(s){
            case AntiNoizProcessor::LearnStatus::Learning:return "Learning";
            case AntiNoizProcessor::LearnStatus::Accepted:return "Accepted";
            case AntiNoizProcessor::LearnStatus::Rejected:return "Rejected";
            case AntiNoizProcessor::LearnStatus::Cancelled:return "Cancelled";
            default:return "Idle";
        }
    }
    static juce::var observation(Snapshot s) {
        auto p=std::make_unique<juce::DynamicObject>();
        p->setProperty("serial",juce::String(std::to_string(s.serial)));
        p->setProperty("profile",s.profile);p->setProperty("learning",s.learning);
        p->setProperty("learnStatus",learnName(s.learnStatus));
        p->setProperty("rackBypassed",s.rackBypassed);
        p->setProperty("effectiveMode",modeName(s.effectiveMode));
        p->setProperty("latency",s.latency);
        // Minimum spectral gain is not broadband reduction. Label it
        // explicitly; residual gain is the linked expander gain.
        p->setProperty("spectralGain",s.spectralGain);p->setProperty("residualGain",s.residualGain);p->setProperty("detectorLevel",s.detectorLevel);p->setProperty("audition",s.audition);
        p->setProperty("sidechainReference",s.sidechainReference);p->setProperty("sidechainNoisePeak",s.sidechainNoisePeak);p->setProperty("sidechainThreshold",s.sidechainThreshold);
        // CONTROL thread: logarithms and JSON allocation never reach audio.
        const auto& spec=s.spectrum;
        if(spec.bins==129||spec.bins==257){
            auto graph=std::make_unique<juce::DynamicObject>();
            graph->setProperty("binHz",spec.binHz);graph->setProperty("bins",spec.bins);
            graph->setProperty("candidate",spec.candidate);graph->setProperty("progress",spec.progress);
            auto curve=[&](const auto& data,bool valid){
                juce::Array<juce::var> points;
                if(valid){points.ensureStorageAllocated(spec.bins);
                    for(int k=0;k<spec.bins;++k){
                        const float db=std::isfinite(data[k])?10.f*std::log10(std::max(1.e-12f,data[k])):-120.f;
                        points.add(juce::var(double(std::clamp(db,-120.f,48.f))));
                    }
                }
                return juce::var(points);
            };
            graph->setProperty("input",curve(spec.input,s.serial!=0&&spec.inputValid&&!s.rackBypassed));
            graph->setProperty("noise",curve(spec.noise,s.serial!=0&&spec.noiseValid));
            p->setProperty("spectrum",juce::var(graph.release()));
        }
        return juce::var(p.release());
    }
};
}
