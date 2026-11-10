// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../src/NativeFxParameterHotkey.h"

using armix::hotkey::FxParameterPolicy;

int main() {
    juce::AudioParameterBool bypass(juce::ParameterID{"vendor-bypass",1},
                                    "Vendor bypass",false);
    juce::AudioParameterFloat amount(juce::ParameterID{"vendor-amount",1},
                                     "Amount",{0.f,1.f},0.50f);
    juce::Array<juce::AudioProcessorParameter*> params;
    params.add(&bypass);
    params.add(&amount);
    const auto listed=FxParameterPolicy::browse(params);
    assert(listed.size()==2);
    assert(listed[0].id=="vendor-bypass"&&listed[0].toggle);
    assert(listed[1].id=="vendor-amount"&&!listed[1].toggle);
    assert(FxParameterPolicy::find(params,"vendor-bypass")==&bypass);
    assert(FxParameterPolicy::find(params,"vendor-amount")==&amount);
    assert(FxParameterPolicy::find(params,"unknown")==nullptr);
    assert(FxParameterPolicy::find(params,"vendor-amount\n")==nullptr);
    assert(FxParameterPolicy::operationAllowed(bypass,"SET_FX_PARAM_TOGGLE"));
    assert(!FxParameterPolicy::operationAllowed(amount,"SET_FX_PARAM_TOGGLE"));
    assert(FxParameterPolicy::operationAllowed(amount,"SET_FX_PARAM_UP"));
    assert(!FxParameterPolicy::operationAllowed(bypass,"SET_FX_PARAM_DOWN"));
    float next=0.f;
    assert(FxParameterPolicy::desired(bypass,"SET_FX_PARAM_TOGGLE",next));
    assert(next==1.f);
    assert(FxParameterPolicy::desired(amount,"SET_FX_PARAM_UP",next));
    assert(std::abs(next-0.55f)<0.00001f);
    assert(FxParameterPolicy::desired(amount,"SET_FX_PARAM_DOWN",next));
    assert(std::abs(next-0.45f)<0.00001f);
    assert(!FxParameterPolicy::desired(amount,"DELETE_FX",next));

    // A vendor changed its parameter order across versions: the same ID
    // must still resolve the same parameter, not the old positional index.
    params.clear();params.add(&amount);params.add(&bypass);
    int position=-1;
    assert(FxParameterPolicy::find(params,"vendor-bypass",&position)==&bypass);
    assert(position==1);
    assert(FxParameterPolicy::find(params,"vendor-amount",&position)==&amount);
    assert(position==0);

    // Duplicate hosted ID is ambiguous: no binding or automatic fallback.
    juce::AudioParameterFloat duplicate(juce::ParameterID{"vendor-amount",1},
                                         "Duplicated ID",{0.f,1.f},0.1f);
    params.add(&duplicate);
    assert(FxParameterPolicy::find(params,"vendor-amount")==nullptr);
    assert(FxParameterPolicy::browse(params).size()==1);

    // Bound before touching any JUCE processor vendor callback.
    juce::Array<juce::AudioProcessorParameter*> tooMany;
    for(int i=0;i<FxParameterPolicy::maxParameters+1;++i)
        tooMany.add(&bypass);
    assert(FxParameterPolicy::browse(tooMany).empty());
    assert(FxParameterPolicy::find(tooMany,"vendor-bypass")==nullptr);
    return 0;
}
