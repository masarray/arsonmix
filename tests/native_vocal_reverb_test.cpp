// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/VocalReverbProcessor.h"
#include "../src/NativeBuiltInFx.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
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
    // Test the real native JUCE faceplate, not a WebView mock or a fake
    // static rendered image. Dimensions and all param IDs are contract.
    // This host builds a private AudioProcessor::createEditor() facade.
    // Reverb's concrete override is public: test the actual native editor,
    // not the inaccessible base-class method.
    auto* uiOwner=dynamic_cast<VocalReverbProcessor*>(created.get());
    assert(uiOwner);
    std::unique_ptr<juce::AudioProcessorEditor> ui(uiOwner->createEditor());
    assert(ui && ui->getWidth()==1120 && ui->getHeight()==430);
    auto* mainDecay=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.2"));
    auto* mainMix=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.0"));
    auto* presetSelect=dynamic_cast<juce::ComboBox*>(
        ui->findChildWithID("armix.reverb.preset"));
    auto* algorithmSelect=dynamic_cast<juce::ComboBox*>(
        ui->findChildWithID("armix.reverb.algorithm"));
    assert(mainDecay&&mainMix&&presetSelect&&algorithmSelect);
    assert(mainDecay->isVisible()&&mainMix->isVisible());
    assert(!mainDecay->getBounds().intersects(mainMix->getBounds()));
    assert(presetSelect->getNumItems()==9 && algorithmSelect->getNumItems()==4);
    auto* firstPage=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.1"));
    auto* motionPage=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.8"));
    assert(firstPage&&firstPage->isVisible()&&motionPage&&!motionPage->isVisible());
    for(int id=0;id<13;++id) {
        auto* knob=dynamic_cast<juce::Slider*>(
            ui->findChildWithID("armix.reverb.param."+juce::String(id)));
        assert(knob);
        assert(knob->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag);
        if(knob->isVisible())assert(ui->getLocalBounds().contains(knob->getBounds()));
    }
    auto snapshot=ui->createComponentSnapshot(ui->getLocalBounds());
    assert(snapshot.isValid()&&snapshot.getWidth()==1120&&snapshot.getHeight()==430);
    assert(snapshot.getPixelAt(16,16)!=snapshot.getPixelAt(568,353));
    // These are snapshots of the ACTUAL JUCE AudioProcessorEditor canvas.
    // No HTML mockup, screenshot reference, or image synthesis involved.
    const auto saveImage=[](const juce::Image& picture,const char* name) {
        const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(name);
        juce::FileOutputStream out(file);
        assert(out.openedOk());
        juce::PNGImageFormat png;
        assert(png.writeImageToStream(picture,out));
        out.flush();
        assert(file.existsAsFile() && file.getSize()>10000);
    };
    saveImage(snapshot,"ArSonMix_Vocal_Reverb_Native_MIX_100.png");
    auto* motion=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.tab.3"));
    assert(motion&&motion->onClick);
    motion->onClick(); // same page handler as a real click, no audio change
    assert(motionPage->isVisible()&&!firstPage->isVisible());
    assert(mainDecay->isVisible()&&mainMix->isVisible());
    auto motionImage=ui->createComponentSnapshot(ui->getLocalBounds(),true,1.5f);
    assert(motionImage.isValid()&&motionImage.getWidth()==1680&&
           motionImage.getHeight()==645);
    saveImage(motionImage,"ArSonMix_Vocal_Reverb_Native_MOTION_150.png");
    assert(!ui->findChildWithID("armix.reverb.param.12")->getBounds().isEmpty());
    auto* nextPreset=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.preset.next"));
    assert(nextPreset&&nextPreset->onClick);
    nextPreset->onClick(); // control-side draft update, not an ASIO ACK
    assert(presetSelect->getSelectedId()==2);
    ui.reset(); // owns and releases all knob look-and-feel references

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
    // Zero added sample latency is distinct from intentionally delayed WET
    // reflections: 0% Mix must pass the very first dry sample unchanged.
    auto zeroMix=vocalReverbFactoryPreset(0);
    zeroMix.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=0.f;
    zeroMix.preset=-1;
    auto drySeed=entry->prepare(48000.,64,nullptr,0);
    auto* drySeedTyped=dynamic_cast<VocalReverbProcessor*>(drySeed.get());
    assert(drySeedTyped);
    assert(drySeedTyped->queueSettings(zeroMix)>0);
    drySeedTyped->consumeRealtimeEdits();
    juce::MemoryBlock dryState;
    drySeed->getStateInformation(dryState);
    auto coldDry=entry->prepare(48000.,64,dryState.getData(),
                                static_cast<int>(dryState.getSize()));
    assert(coldDry && coldDry->getLatencySamples()==0);
    juce::AudioBuffer<float> dryPcm(2,64);
    dryPcm.clear();
    dryPcm.setSample(0,0,.7f);
    dryPcm.setSample(1,0,-.5f);
    coldDry->processBlock(dryPcm,midi);
    assert(std::fabs(dryPcm.getSample(0,0)-.7f)<2.e-6f);
    assert(std::fabs(dryPcm.getSample(1,0)+.5f)<2.e-6f);
    for(int i=1;i<64;++i) {
        assert(std::fabs(dryPcm.getSample(0,i))<1.e-6f);
        assert(std::fabs(dryPcm.getSample(1,i))<1.e-6f);
    }

    // Each singing preset must be an ACTUAL native C++ stereo wet tail.
    // Stage the preset BEFORE prepare() so its first ASIO callback does not
    // need to regenerate expensive reverb coefficients.
    for(int program=0;program<8;++program) {
        auto seed=entry->prepare(48000.,64,nullptr,0);
        auto* seedTyped=dynamic_cast<VocalReverbProcessor*>(seed.get());
        assert(seedTyped);
        auto preset=vocalReverbFactoryPreset(program);
        preset.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=1.f;
        preset.preset=-1;
        assert(seedTyped->queueSettings(preset)>0);
        seedTyped->consumeRealtimeEdits();
        juce::MemoryBlock wetState;
        seed->getStateInformation(wetState);
        auto wet=entry->prepare(48000.,64,wetState.getData(),
                                static_cast<int>(wetState.getSize()));
        assert(wet && wet->getLatencySamples()==0);
        juce::AudioBuffer<float> p(2,128);
        double eLeft=0.,eRight=0.,eFold=0.;
        for(int block=0;block<256;++block) {
            p.clear();
            if(block==0) {p.setSample(0,0,.8f);p.setSample(1,0,.8f);}
            wet->processBlock(p,midi);
            if(block==0) {
                // 100% wet with nonzero pre-delay: no dry first-sample leak.
                assert(std::fabs(p.getSample(0,0))<1.e-6f);
                assert(std::fabs(p.getSample(1,0))<1.e-6f);
            }
            for(int j=0;j<128;++j) {
                const double a=p.getSample(0,j),b=p.getSample(1,j);
                assert(std::isfinite(a)&&std::isfinite(b));
                assert(std::fabs(a)<12. && std::fabs(b)<12.);
                if(block>32) {eLeft+=a*a;eRight+=b*b;eFold+=(a+b)*(a+b)*.25;}
            }
        }
        assert(eLeft>1.e-10 && eRight>1.e-10 && eFold>1.e-11);
    }

    // Native rack bypass must NOT freeze an audible room tail. Drain only
    // after the fade finishes, feeding silence into the same FDN, then idle
    // with zero DSP work. No ring-buffer reset spike in the PCM callback.
    const auto beforeDrain=typed->processedBlocks();
    typed->onRealtimeBypass(true,true);
    for(int i=0;i<4000;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
    }
    const auto parked=typed->processedBlocks();
    assert(parked>beforeDrain);
    for(int i=0;i<20;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
    }
    assert(typed->processedBlocks()==parked);
    // Simulate the SAME FxInstance full-bypass contract: exactly one
    // mailbox edit is ACKed by the host hook per callback, not a hidden
    // second pop from the continuous tail-rendering processBlock().
    auto updateA=vocalReverbFactoryPreset(1),updateB=vocalReverbFactoryPreset(2);
    const auto idA=typed->queueSettings(updateA);
    const auto idB=typed->queueSettings(updateB);
    assert(idA && idB>idA && !typed->stateSnapshotQuiescent());
    typed->consumeRealtimeEdits();
    buffer.clear();
    created->processBlock(buffer,midi);
    assert(typed->appliedRequestId()==idA);
    assert(!typed->stateSnapshotQuiescent());
    typed->consumeRealtimeEdits();
    buffer.clear();
    created->processBlock(buffer,midi);
    assert(typed->appliedRequestId()==idB);
    assert(typed->stateSnapshotQuiescent());
    typed->onRealtimeBypass(false,false);
    float worstGhost=0.f;
    for(int i=0;i<40;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
        for(int sample=0;sample<128;++sample)
            worstGhost=std::max(worstGhost,
                std::max(std::fabs(buffer.getSample(0,sample)),
                         std::fabs(buffer.getSample(1,sample))));
    }
    assert(worstGhost<1.e-3f);

    // True zero-cost bypass before first audio render: initial BYPASS does
    // not start a needless empty 8-line delay network on a live mixer.
    auto coldBypass=entry->prepare(48000.,64,nullptr,0);
    auto* coldTyped=dynamic_cast<VocalReverbProcessor*>(coldBypass.get());
    assert(coldTyped);
    coldTyped->onRealtimeBypass(true,true);
    dryPcm.clear();
    coldBypass->processBlock(dryPcm,midi);
    assert(coldTyped->processedBlocks()==0);
    coldTyped->onRealtimeBypass(false,false);

    // Prepare/automation smoke across common monitoring sample rates;
    // the native FX cannot secretly add a plugin delay or create a second
    // PCM processing pipeline.
    for(const double rate : {44100.,48000.,96000.,192000.}) {
        auto instance=entry->prepare(rate,32,nullptr,0);
        assert(instance && instance->getLatencySamples()==0);
        juce::AudioBuffer<float> small(2,32);
        for(int block=0;block<32;++block) {
            small.clear();
            if(block==0)small.setSample(0,0,.5f);
            instance->processBlock(small,midi);
            for(int ch=0;ch<2;++ch)for(int j=0;j<32;++j)
                assert(std::isfinite(small.getSample(ch,j)));
        }
    }
    return 0;
}
