// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../src/NativeBuiltInFx.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <type_traits>

// M4: a future native FX must extend the real host registry, not require
// new branches in Main, the route graph, WebView or the JUCE callback.
class FixtureGainFx final : public juce::AudioProcessor {
public:
    FixtureGainFx():juce::AudioProcessor(BusesProperties()
        .withInput("Main",juce::AudioChannelSet::stereo(),true)
        .withOutput("Main",juce::AudioChannelSet::stereo(),true)){}
    const juce::String getName() const override {return "Fixture Gain";}
    void prepareToPlay(double rate,int frames) override {
        ready_=(rate>=8000. && frames>=32);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    bool restoreUnpublishedState(const void* data,int bytes) noexcept {
        if(!data || bytes!=static_cast<int>(sizeof(float)))return false;
        float candidate=0.f;
        std::memcpy(&candidate,data,sizeof(candidate));
        if(!std::isfinite(candidate)||candidate<0.f||candidate>4.f)return false;
        gain_=candidate;
        return true;
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        pcm.applyGain(gain_);
    }
    double getTailLengthSeconds() const override {return 0.;}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool hasEditor() const override {return false;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override {
        dest.append(&gain_,sizeof(gain_));
    }
    void setStateInformation(const void* data,int bytes) override {
        restoreUnpublishedState(data,bytes);
    }
private:
    float gain_=1.f;
    bool ready_=false;
};

using Entry=armix::builtin_fx::NativeBuiltInFx;
using armix::builtin_fx::prepareNativeBuiltInFx;
using armix::builtin_fx::validNativeBuiltInFxCatalog;
using armix::builtin_fx::findNativeBuiltInFxIn;

constexpr Entry fixture{
    "armix.test.fixture-gain.v1","Fixture Gain","Test",false,
    8000.,192000.,
    "UNAVAILABLE","DEVICE_CHANGED","FORMAT_REJECTED",
    "PREPARE_REJECTED","PREPARE_EXCEPTION","RESTORE_REJECTED",
    prepareNativeBuiltInFx<FixtureGainFx>
};
constexpr std::array<Entry,1> injected{fixture};
constexpr std::array<Entry,2> duplicate{fixture,fixture};
static_assert(validNativeBuiltInFxCatalog(injected));
static_assert(!validNativeBuiltInFxCatalog(duplicate));
static_assert(validNativeBuiltInFxCatalog(armix::builtin_fx::nativeBuiltInFx));
static_assert(std::is_same_v<Entry::Prepare,decltype(fixture.prepare)>);

int main() {
    const auto* found=findNativeBuiltInFxIn(injected,"armix.test.fixture-gain.v1");
    assert(found==&injected[0]);
    assert(!findNativeBuiltInFxIn(injected,""));
    assert(!findNativeBuiltInFxIn(injected,"armix.test.fixture-gain.v2"));
    // Production IDs still resolve through the exact SAME lookup implementation.
    for(const auto& production:armix::builtin_fx::nativeBuiltInFx)
        assert(armix::builtin_fx::findNativeBuiltInFx(production.id)==&production);

    constexpr float gain=1.5f;
    auto prepared=found->prepare(48000.,128,&gain,sizeof(gain));
    assert(prepared!=nullptr);
    assert(prepared->getLatencySamples()==0);
    juce::AudioBuffer<float> stereo(2,32);
    stereo.clear();
    stereo.setSample(0,0,0.6f);
    stereo.setSample(1,0,-0.4f);
    juce::MidiBuffer midi;
    prepared->processBlock(stereo,midi);
    assert(std::abs(stereo.getSample(0,0)-0.9f)<0.00001f);
    assert(std::abs(stereo.getSample(1,0)+0.6f)<0.00001f);

    const float bad=std::numeric_limits<float>::quiet_NaN();
    assert(!found->prepare(48000.,128,&bad,sizeof(bad)));
    assert(!found->prepare(48000.,128,&gain,0));
    assert(!found->prepare(48000.,128,nullptr,sizeof(gain)));
    // A new type is registered by adding a descriptor only: the production
    // graph's FX lease, bypass and VST3 editor lifetime remain untouched.
    return 0;
}
