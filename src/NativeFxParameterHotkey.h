// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <vector>

// H2 host-side parameter identity: never persist JUCE parameter array indexes.
// JUCE's HostedAudioProcessorParameter::getParameterID() is the VST3 host's
// stable vendor-facing parameter ID; plugins without this surface cannot be
// safely mapped, even if their GUI displays a recognizable knob.
namespace armix::hotkey {
struct FxParameterTarget final {
    juce::String id,name;
    int index=-1,steps=0;
    bool toggle=false;
    float normalized=0.f;
};
class FxParameterPolicy final {
public:
    static constexpr int maxParameters=512;
    static constexpr int maxIdLength=96;
    static bool validId(const juce::String& value) noexcept {
        return value.isNotEmpty() && value.length()<=maxIdLength &&
               !value.containsAnyOf("\r\n\t");
    }
    static juce::String hostedId(const juce::AudioProcessorParameter* parameter) {
        if(!parameter||!parameter->isAutomatable()||parameter->isMetaParameter())
            return {};
        const auto* hosted=dynamic_cast<const juce::HostedAudioProcessorParameter*>(parameter);
        if(!hosted)return {};
        const auto id=hosted->getParameterID();
        return validId(id)?id:juce::String{};
    }
    static bool isToggle(const juce::AudioProcessorParameter& parameter) {
        return parameter.isDiscrete()&&parameter.getNumSteps()==2;
    }
    static bool operationAllowed(const juce::AudioProcessorParameter& parameter,
                                 const juce::String& operation) {
        if(!parameter.isAutomatable()||parameter.isMetaParameter())return false;
        if(operation=="SET_FX_PARAM_TOGGLE")return isToggle(parameter);
        if(operation=="SET_FX_PARAM_UP"||operation=="SET_FX_PARAM_DOWN")
            return !isToggle(parameter);
        return false;
    }
    // The caller supplies a verified, leased live processor. Each candidate ID
    // must be unique; duplicate / missing stable IDs are never matched by index.
    static juce::AudioProcessorParameter* find(
            const juce::Array<juce::AudioProcessorParameter*>& parameters,
            const juce::String& id, int* position=nullptr) {
        if(!validId(id)||parameters.size()>maxParameters)return nullptr;
        juce::AudioProcessorParameter* found=nullptr;
        int foundIndex=-1;
        for(int i=0;i<parameters.size();++i) {
            auto* p=parameters.getUnchecked(i);
            if(hostedId(p)!=id)continue;
            if(found)return nullptr; // duplicate ID: cannot prove identity
            found=p;foundIndex=i;
        }
        if(found&&position)*position=foundIndex;
        return found;
    }
    static std::vector<FxParameterTarget> browse(
            const juce::Array<juce::AudioProcessorParameter*>& parameters) {
        std::vector<FxParameterTarget> result;
        if(parameters.size()>maxParameters)return result;
        result.reserve(static_cast<std::size_t>(parameters.size()));
        for(int i=0;i<parameters.size();++i) {
            auto* p=parameters.getUnchecked(i);
            const auto id=hostedId(p);
            if(id.isEmpty()||find(parameters,id)!=p)continue;
            const float value=p->getValue();
            if(!std::isfinite(value)||value<0.f||value>1.f)continue;
            result.push_back({id,p->getName(100),i,p->getNumSteps(),
                              isToggle(*p),value});
        }
        return result;
    }
    // Continuous controls take 5% of the *normalized* vendor range per key
    // press; discrete controls take one actual vendor step. No key held repeat.
    static bool desired(const juce::AudioProcessorParameter& p,
                        const juce::String& operation,float& next) {
        if(!operationAllowed(p,operation))return false;
        const float before=p.getValue();
        if(!std::isfinite(before)||before<0.f||before>1.f)return false;
        if(operation=="SET_FX_PARAM_TOGGLE")
            next=before>=0.5f?0.f:1.f;
        else {
            const int n=p.getNumSteps();
            const float step=p.isDiscrete()&&n>2 && n<100000?
                1.f/static_cast<float>(n-1):0.05f;
            next=std::clamp(before+(operation=="SET_FX_PARAM_UP"?step:-step),0.f,1.f);
        }
        return std::isfinite(next);
    }
};
} // namespace armix::hotkey
