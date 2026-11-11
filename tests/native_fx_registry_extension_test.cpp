// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../src/NativeBuiltInFx.h"
#include "../src/NativeFxControlRequest.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
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
static_assert(std::is_same_v<Entry::Prepare,std::remove_cv_t<decltype(fixture.prepare)>>);

static void verifySharedNativeFxControlRequest() {
    // Same lease/epoch/revision envelope across all four built-in FX editors.
    auto request=std::make_unique<juce::DynamicObject>();
    request->setProperty("channelId","9007199254740993"); // exact >JS-safe uint64
    request->setProperty("deviceEpoch","12");
    request->setProperty("graphRevision","19");
    request->setProperty("instanceId","g5c-707");
    request->setProperty("slot",3);
    request->setProperty("requestId","comp-1844674407370955161");
    juce::var source(request.release());
    using Target=armix::builtin_fx::NativeFxControlRequest;
    const auto good=Target::parse(source,8);
    assert(good.valid && !good.unbound && good.channel==9007199254740993ULL);
    assert(good.epoch==12 && good.revision==19 && good.slot==3);
    assert(good.sequence("comp-")==1844674407370955161ULL);
    assert(good.sequence("harm-")==0 && good.sequence("throw-")==0);
    auto alter=[](const juce::var& before,const char* name,juce::var value) {
        auto copy=juce::JSON::parse(juce::JSON::toString(before));
        copy.getDynamicObject()->setProperty(name,value);
        return copy;
    };
    assert(!Target::parse(alter(source,"channelId",juce::var(9007199254740993.0)),8).valid);
    assert(!Target::parse(alter(source,"channelId",juce::var("00")),8).valid);
    assert(!Target::parse(alter(source,"deviceEpoch",juce::var("0")),8).valid);
    assert(!Target::parse(alter(source,"graphRevision",juce::var("18446744073709551616")),8).valid);
    assert(!Target::parse(alter(source,"instanceId",juce::var("")),8).valid);
    assert(!Target::parse(alter(source,"instanceId",juce::var(1)),8).valid);
    assert(!Target::parse(alter(source,"slot",juce::var(8)),8).valid);
    assert(!Target::parse(alter(source,"slot",juce::var(-1)),8).valid);
    // A native editor slot is an integer JSON value, not a coercible string,
    // floating-point value, bool or 64-bit guest pointer/index.
    for(const auto& invalid: {juce::var("3"),juce::var(3.5),juce::var(3.0),
                               juce::var(true),juce::var(static_cast<juce::int64>(3))}) {
        const auto bad=Target::parse(alter(source,"slot",invalid),8);
        assert(!bad.valid && bad.slot==-1);
    }
    assert(!Target::parse(source,0).valid);
    assert(!Target::parse(source,-1).valid);
    assert(!Target::parse(alter(source,"requestId",juce::var(12345)),8).valid);
    assert(!Target::parse(alter(source,"requestId",juce::var(true)),8).valid);
    // A foreign/non-string workspace token must never select a logical
    // authority path or silently fall into an installed processor lease.
    for(const auto& invalid: {juce::var(1),juce::var(true),
                               juce::var(2.5),juce::var(juce::Array<juce::var>{})}) {
        const auto bad=Target::parse(alter(source,"workspaceToken",invalid),8);
        assert(!bad.valid && !bad.unbound);
    }
    // Device epoch "0" is intentional when the *separate* saved logical
    // workspace owns the FX. It is NOT a valid LIVE device identity; its
    // mailbox sequence still serves offline EQ/COMP/Harmony/Throw edits.
    auto logicalArgs=alter(source,"deviceEpoch",juce::var("0"));
    logicalArgs.getDynamicObject()->setProperty("workspaceToken","saved-mixer");
    const auto unbound=Target::parse(logicalArgs,8);
    assert(!unbound.valid && unbound.unbound);
    assert(unbound.sequence("comp-")==good.sequence("comp-"));
    // Harmonice's new user-preset command reuses the SAME host target
    // envelope, but must never be interpreted as a realtime DSP mailbox.
    const auto preset=Target::parse(alter(source,"requestId",
        juce::var("hpres-1844674407370955161-7")),8);
    assert(preset.valid && !preset.unbound && preset.sequence("harm-")==0);
    assert(!Target::parse(alter(source,"requestId",juce::var("")),8).valid);
    assert(!Target::parse(alter(source,"requestId",juce::var(juce::String::repeatedString("a",73))),8).valid);
    const auto logical=Target::parse(alter(source,"workspaceToken",juce::var("persisted-mixer")),8);
    assert(logical.valid && logical.unbound && logical.sequence("comp-")==good.sequence("comp-"));
    for(const auto& id:{"eq-1","harm-17","throw-99","comp-0008"}) {
        const auto candidate=Target::parse(alter(source,"requestId",juce::var(id)),8);
        assert(candidate.valid);
        assert(candidate.sequence(juce::String(id).upToFirstOccurrenceOf("-",true,false).toRawUTF8())>0);
    }
    for(const auto& id:{"comp-","comp--1","comp-1x","comp-1.0",
                          "comp-12345678901234567890","comp-0000000000000000000"}) {
        const auto candidate=Target::parse(alter(source,"requestId",juce::var(id)),8);
        assert(candidate.sequence("comp-")==0);
    }
    const auto max19=Target::parse(alter(source,"requestId",
        juce::var("comp-9999999999999999999")),8);
    assert(max19.valid && max19.sequence("comp-")==9999999999999999999ULL);
}

int main() {
    verifySharedNativeFxControlRequest();
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
