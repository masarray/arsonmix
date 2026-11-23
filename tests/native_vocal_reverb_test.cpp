// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/VocalReverbProcessor.h"
#include "../src/NativeBuiltInFx.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
    using namespace armix::builtin_fx;
    static_assert(VocalReverbProcessor::stateBytes==68);
    for(int i=0;i<8;++i)assert(validVocalReverbSettings(vocalReverbFactoryPreset(i)));
    const auto* entry=findNativeBuiltInFx(VocalReverbProcessor::catalogIdentity);
    assert(entry&&entry->showInPicker);
    assert(juce::String(entry->category)=="Reverb");
    assert(!entry->prepare(384000.,128,nullptr,0)); // pinned DSP max=192k
    assert(!entry->prepare(48000.,4,nullptr,0));
    auto created=entry->prepare(48000.,128,nullptr,0);
    assert(created && created->getName()=="Vocal Reverb" && created->hasEditor());
    assert(created->getLatencySamples()==0);
    auto* typed=dynamic_cast<VocalReverbProcessor*>(created.get());
    assert(typed&&typed->isReady());
    juce::MemoryBlock state;
    created->getStateInformation(state);
    assert(static_cast<int>(state.getSize())==VocalReverbProcessor::stateBytes);
    auto restored=entry->prepare(44100.,64,state.getData(),static_cast<int>(state.getSize()));
    assert(restored);
    auto invalid=state;
    invalid[0]=0;
    assert(!entry->prepare(48000.,128,invalid.getData(),static_cast<int>(invalid.getSize())));
    assert(!entry->prepare(48000.,128,state.getData(),static_cast<int>(state.getSize())-1));
    invalid=state;
    // Reject algorithm outside 4-way native catalog.
    invalid[12]=99;invalid[13]=0;invalid[14]=0;invalid[15]=0;
    assert(!entry->prepare(48000.,128,invalid.getData(),static_cast<int>(invalid.getSize())));
    auto edited=vocalReverbFactoryPreset(5);
    edited.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=0.5f;
    edited.preset=-1;
    const auto request=typed->queueSettings(edited);
    assert(request>0 && !typed->stateSnapshotQuiescent());
    // Shared rack bypass still consumes one edit on audio callback.
    typed->consumeRealtimeEdits();
    assert(typed->appliedRequestId()==request);
    assert(typed->stateSnapshotQuiescent());
    VocalReverbSettings acknowledged{};
    assert(typed->acceptedSettings(acknowledged));
    assert(acknowledged.preset==-1);
    assert(acknowledged.values[0]==.5f);
    created->getStateInformation(state);
    auto copy=entry->prepare(48000.,128,state.getData(),static_cast<int>(state.getSize()));
    assert(copy);
    auto* copyTyped=dynamic_cast<VocalReverbProcessor*>(copy.get());
    assert(copyTyped&&copyTyped->acceptedSettings(acknowledged));
    assert(acknowledged.values[0]==.5f && acknowledged.algorithm==3);
    auto bad=edited;
    bad.values[1]=std::numeric_limits<float>::quiet_NaN();
    assert(typed->queueSettings(bad)==0);
    assert(typed->appliedRequestId()==request);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2,128);
    double tail=0.;
    for(int block=0;block<440;++block) {
        buffer.clear();
        if(block==0) {buffer.setSample(0,0,.65f);buffer.setSample(1,0,.65f);}
        created->processBlock(buffer,midi);
        for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i) {
            const float y=buffer.getSample(ch,i);
            assert(std::isfinite(y) && std::abs(y)<12.f);
            if(block>15)tail+=static_cast<double>(y)*y;
        }
    }
    assert(tail>1.e-8 && typed->processedBlocks()==440);
    // Fail-closed state after seal: an audio-owned FX cannot be mutated by
    // old session bytes while published.
    typed->sealForRealtime();
    assert(!typed->restoreUnpublishedState(state.getData(),static_cast<int>(state.getSize())));
    return 0;
}
