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
    inline static int constructed=0; // CONTROL-only fixture
    FixtureGainFx():juce::AudioProcessor(BusesProperties()
        .withInput("Main",juce::AudioChannelSet::stereo(),true)
        .withOutput("Main",juce::AudioChannelSet::stereo(),true)){++constructed;}
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

static void verifyFirstSoundGuitarProcessor() {
    // This is the REAL production native registry path, not a fixture
    // or browser mock. The resulting processor is inserted as one FxInstance.
    const auto* guitar=armix::builtin_fx::findNativeBuiltInFx(
        armix::builtin_fx::GuitarRigProcessor::catalogIdentity);
    assert(guitar && guitar->showInPicker);
    assert(juce::String(guitar->category)=="Guitar");
    assert(!guitar->prepare(16000.,128,nullptr,0));
    auto live=guitar->prepare(48000.,128,nullptr,0);
    assert(live && live->getName()=="Guitar FX Rig");
    assert(live->getLatencySamples()==48);
    auto* guitarTyped=dynamic_cast<armix::builtin_fx::GuitarRigProcessor*>(live.get());
    assert(guitarTyped && guitarTyped->isReady());
    juce::MemoryBlock snapshot;
    live->getStateInformation(snapshot);
    assert(snapshot.getSize()==12u+2u*4u+2u*4u*44u); // two durable banks
    auto restored=guitar->prepare(44100.,128,
        snapshot.getData(),static_cast<int>(snapshot.getSize()));
    assert(restored && restored->getLatencySamples()==48);
    auto broken=snapshot;
    // Corrupt active-bank index; the whole restore must fail closed.
    broken[8]=0xff;broken[9]=0xff;broken[10]=0xff;broken[11]=0xff;
    assert(!guitar->prepare(48000.,128,broken.getData(),
        static_cast<int>(broken.getSize())));
    assert(!guitar->prepare(48000.,128,
        snapshot.getData(),static_cast<int>(snapshot.getSize())-1));

    auto badSecondBank=snapshot;
    // v2: 12-byte header + countA + 4*44-byte A + countB.
    // Set Bank B's first block identity to zero, preserving exact length.
    constexpr std::size_t secondBankFirstId=12u+4u+4u*44u+4u;
    for(std::size_t i=0;i<4;++i)badSecondBank[secondBankFirstId+i]=0;
    assert(!guitar->prepare(48000.,128,badSecondBank.getData(),
        static_cast<int>(badSecondBank.getSize())));
    // GRX1 v1 single-program snapshots remain loadable, migrated to both
    // bank slots without changing the approved tone or DSP.
    juce::MemoryBlock legacy;
    juce::MemoryOutputStream legacyWriter(legacy,false);
    legacyWriter.writeInt(static_cast<int>(
        armix::builtin_fx::GuitarRigProcessor::stateMagic));
    legacyWriter.writeInt(1);
    const auto legacyTone=armix::guitarfx::factoryTone(2);
    legacyWriter.writeInt(static_cast<int>(legacyTone.count));
    for(std::size_t i=0;i<legacyTone.count;++i){
        const auto& block=legacyTone.blocks[i];
        legacyWriter.writeInt(static_cast<int>(block.id));
        legacyWriter.writeInt(static_cast<int>(block.module));
        legacyWriter.writeInt(block.enabled?1:0);
        for(const float v:block.values)legacyWriter.writeFloat(v);
    }
    // JUCE MemoryOutputStream reserves spare capacity in MemoryBlock until
    // flush/destruction; getSize() is NOT the encoded payload length yet.
    // This previously failed on MSVC after 91/92 other CTests passed.
    assert(legacyWriter.getDataSize()==12u+4u*44u);
    legacyWriter.flush();
    assert(legacy.getSize()==12u+4u*44u);
    auto legacyRestored=guitar->prepare(48000.,128,
        legacy.getData(),static_cast<int>(legacy.getSize()));
    auto* legacyRig=dynamic_cast<armix::builtin_fx::GuitarRigProcessor*>(
        legacyRestored.get());
    assert(legacyRig && legacyRig->activeBank()==0);
    assert(armix::guitarfx::sameTone(legacyRig->acceptedBankProgram(0),legacyTone));
    assert(armix::guitarfx::sameTone(legacyRig->acceptedBankProgram(1),legacyTone));

    juce::AudioBuffer<float> pcm(2,1024);
    for(int i=0;i<pcm.getNumSamples();++i) {
        const float source=0.12f*std::sin(2.f*3.14159265358979323846f*
                                         330.f*static_cast<float>(i)/48000.f);
        pcm.setSample(0,i,source);
        pcm.setSample(1,i,source);
    }
    juce::MidiBuffer midi;
    live->processBlock(pcm,midi);
    double outputEnergy=0.,difference=0.;
    for(int i=0;i<pcm.getNumSamples();++i){
        const float source=0.12f*std::sin(2.f*3.14159265358979323846f*
                                         330.f*static_cast<float>(i)/48000.f);
        const float result=pcm.getSample(0,i);
        assert(std::isfinite(result));
        assert(std::fabs(result-pcm.getSample(1,i))<0.00001f);
        outputEnergy+=static_cast<double>(result)*result;
        difference+=std::fabs(result-source);
    }
    assert(outputEnergy>0.00001 && difference>0.01);
    assert(guitarTyped->observedBlockSerial()==1);
    assert(guitarTyped->observedOutputPeak()>0.f);
    assert(guitarTyped->hasEditor()); // same generic routed editor host
    // Active and fully bypassed rack paths share an authenticated mailbox.
    // No PCM processing is necessary to acknowledge edits at full bypass:
    // the existing FxInstance fast path calls consumeRealtimeEdits().
    auto bypassed=guitar->prepare(48000.,128,nullptr,0);
    auto* bypassRig=dynamic_cast<armix::builtin_fx::GuitarRigProcessor*>(
        bypassed.get());
    assert(bypassRig && bypassRig->observedBlockSerial()==0);
    auto bypassTone=armix::guitarfx::factoryTone(1);
    assert(bypassRig->queueProgram(bypassTone,99,0));
    assert(!bypassRig->stateSnapshotQuiescent());
    bypassRig->consumeRealtimeEdits(); // same callback hook as host bypass
    assert(bypassRig->stateSnapshotQuiescent());
    assert(bypassRig->lastCompletion().requestId==99);
    assert(armix::guitarfx::sameTone(bypassRig->acceptedProgram(),bypassTone));
    assert(bypassRig->observedBlockSerial()==0); // no fake wet PCM claim
    // A second adjustment while fully bypassed must ACK on the next native
    // host callback, even though processBlock is not invoked at all.
    bypassRig->onRealtimeBypass(true,true);
    auto secondBypassTone=armix::guitarfx::factoryTone(3);
    assert(bypassRig->queueProgram(secondBypassTone,100,1));
    bypassRig->consumeRealtimeEdits();
    assert(bypassRig->stateSnapshotQuiescent());
    assert(bypassRig->activeBank()==1);
    assert(bypassRig->pendingProgramChanges()==0);
    assert(bypassRig->observedBlockSerial()==0);
    assert(armix::guitarfx::sameTone(
        bypassRig->acceptedBankProgram(0),bypassTone));
    assert(armix::guitarfx::sameTone(
        bypassRig->acceptedBankProgram(1),secondBypassTone));
    // More severe: wet output starts transitioning, then the host becomes
    // fully bypassed before all 128 crossfade samples have been rendered.
    // Its next tone MUST NOT wait forever for skipped wet samples.
    auto partiallyWet=guitar->prepare(48000.,128,nullptr,0);
    auto* partiallyWetRig=dynamic_cast<
        armix::builtin_fx::GuitarRigProcessor*>(partiallyWet.get());
    assert(partiallyWetRig);
    juce::AudioBuffer<float> tiny(2,32);
    tiny.clear();
    juce::MidiBuffer tinyMidi;
    assert(partiallyWetRig->queueProgram(bypassTone,1,0));
    partiallyWet->processBlock(tiny,tinyMidi); // 96 samples of fade remain
    assert(partiallyWetRig->stateSnapshotQuiescent());
    partiallyWetRig->onRealtimeBypass(true,true);
    assert(partiallyWetRig->queueProgram(secondBypassTone,2,1));
    partiallyWetRig->consumeRealtimeEdits();
    assert(partiallyWetRig->stateSnapshotQuiescent());
    assert(partiallyWetRig->activeBank()==1);
    assert(partiallyWetRig->lastCompletion().requestId==2);
    // Wet reactivation never resurrects an old transition or fake meter.
    partiallyWetRig->onRealtimeBypass(false,false);
    tiny.clear();
    partiallyWet->processBlock(tiny,tinyMidi);
    assert(partiallyWetRig->pendingProgramChanges()==0);
    // Cabinet impulse history must not leak after the 128-frame host
    // rack-bypass envelope reaches fully dry. No old note on wet return.
    auto noTail=guitar->prepare(48000.,128,nullptr,0);
    auto* noTailRig=dynamic_cast<
        armix::builtin_fx::GuitarRigProcessor*>(noTail.get());
    assert(noTailRig);
    tiny.clear();
    tiny.setSample(0,0,0.6f);
    tiny.setSample(1,0,0.6f);
    noTail->processBlock(tiny,tinyMidi); // populate FIR's 32-sample history
    noTailRig->onRealtimeBypass(true,true);
    noTailRig->onRealtimeBypass(false,false);
    tiny.clear();
    noTail->processBlock(tiny,tinyMidi);
    for(int i=0;i<tiny.getNumSamples();++i){
        assert(tiny.getSample(0,i)==0.f);
        assert(tiny.getSample(1,i)==0.f);
    }
    auto edit=guitarTyped->acceptedProgram();
    edit.blocks[2].values[0]=0.78f;
    edit.blocks[1].enabled=true;
    edit.blocks[1].values[0]=0.64f;
    assert(guitarTyped->queueProgram(edit,101));
    // A queued GUI intent is NOT durable/accepted until audio callback ACK.
    assert(guitarTyped->acceptedProgram().blocks[2].values[0]!=0.78f);
    assert(!guitarTyped->stateSnapshotQuiescent());
    assert(guitarTyped->pendingProgramChanges()==1);
    pcm.clear();
    pcm.setSample(0,0,0.15f);
    pcm.setSample(1,0,0.15f);
    live->processBlock(pcm,midi); // LIVE block drains edit and crossfades
    assert(guitarTyped->stateSnapshotQuiescent());
    assert(guitarTyped->pendingProgramChanges()==0);
    assert(guitarTyped->acceptedProgram().blocks[2].values[0]==0.78f);
    assert(guitarTyped->lastCompletion().requestId==101);
    assert(guitarTyped->lastCompletion().applied);
    juce::MemoryBlock acceptedBytes;
    live->getStateInformation(acceptedBytes);
    auto acceptedRestore=guitar->prepare(96000.,128,
        acceptedBytes.getData(),static_cast<int>(acceptedBytes.getSize()));
    assert(acceptedRestore);
    auto* acceptedGuitar=dynamic_cast<
        armix::builtin_fx::GuitarRigProcessor*>(acceptedRestore.get());
    assert(acceptedGuitar && acceptedGuitar->acceptedProgram().blocks[2].values[0]==0.78f);
    edit.blocks[2].id=0;
    assert(!guitarTyped->queueProgram(edit,102)); // invalid ID -> no graph edit
    edit=guitarTyped->acceptedProgram();
    edit.blocks[2].values[0]=0.49f;
    assert(guitarTyped->queueProgram(edit,103));
    assert(guitarTyped->pendingProgramChanges()==1);
    // Device rate changes cancel stale prepared packets and preserve last
    // applied program; a 48kHz FIR must never be applied to a 44.1k graph.
    live->prepareToPlay(44100.,128);
    assert(guitarTyped->stateSnapshotQuiescent());
    assert(guitarTyped->lastCompletion().requestId==103);
    assert(!guitarTyped->lastCompletion().applied);
    assert(guitarTyped->pendingProgramChanges()==0);
    assert(guitarTyped->acceptedProgram().blocks[2].values[0]==0.78f);
    assert(guitarTyped->getLatencySamples()==48);

    // Two accepted live edits must never interrupt a 128-frame crossfade:
    // a second intent waits in the queue until the first transition ends.
    auto first=guitarTyped->acceptedProgram();
    first.blocks[2].values[0]=0.33f;
    auto second=first;
    second.blocks[2].values[0]=0.62f;
    assert(guitarTyped->queueProgram(first,105));
    juce::AudioBuffer<float> shortBlock(2,32);
    shortBlock.clear();
    shortBlock.setSample(0,0,0.25f);
    shortBlock.setSample(1,0,0.25f);
    live->processBlock(shortBlock,midi); // first intent becomes audio-owned
    assert(guitarTyped->queueProgram(second,106));
    assert(guitarTyped->pendingProgramChanges()==2);
    // Save must be blocked while the second edit waits and the first ACK
    // has not yet been consumed on CONTROL.
    assert(!guitarTyped->stateSnapshotQuiescent());
    for(int block=0;block<3;++block){
        shortBlock.clear();
        live->processBlock(shortBlock,midi);
    }
    assert(guitarTyped->pendingProgramChanges()==1);
    assert(!guitarTyped->stateSnapshotQuiescent());
    shortBlock.clear();
    live->processBlock(shortBlock,midi); // now the next block starts second fade
    assert(guitarTyped->stateSnapshotQuiescent());
    assert(guitarTyped->pendingProgramChanges()==0);
    assert(guitarTyped->lastCompletion().requestId==106);
    assert(guitarTyped->acceptedProgram().blocks[2].values[0]==0.62f);
    // Helix-style live A/B comparison is not a second processor/FX slot.
    // Bank A retains the accepted 0.62 tone. Applying B changes only its
    // own bank and persists both choices through the exact same .smix blob.
    auto alternate=armix::guitarfx::factoryTone(3);
    assert(guitarTyped->queueProgram(alternate,107,1));
    for(int block=0;block<8 && guitarTyped->pendingProgramChanges();++block){
        shortBlock.clear();
        live->processBlock(shortBlock,midi); // previous fade then Bank B
    }
    assert(guitarTyped->stateSnapshotQuiescent());
    assert(guitarTyped->lastCompletion().requestId==107);
    assert(guitarTyped->lastCompletion().bank==1);
    assert(guitarTyped->activeBank()==1);
    assert(guitarTyped->acceptedBankProgram(0).blocks[2].values[0]==0.62f);
    assert(armix::guitarfx::sameTone(
        guitarTyped->acceptedBankProgram(1),alternate));
    assert(!guitarTyped->queueProgram(alternate,108,3)); // invalid bank
    juce::MemoryBlock banksSaved;
    live->getStateInformation(banksSaved);
    auto twoBanks=guitar->prepare(48000.,128,
        banksSaved.getData(),static_cast<int>(banksSaved.getSize()));
    auto* twoBankRig=dynamic_cast<armix::builtin_fx::GuitarRigProcessor*>(
        twoBanks.get());
    assert(twoBankRig && twoBankRig->activeBank()==1);
    assert(twoBankRig->acceptedBankProgram(0).blocks[2].values[0]==0.62f);
    assert(armix::guitarfx::sameTone(
        twoBankRig->acceptedBankProgram(1),alternate));
    // State captured before publication is a reliable round-trip.
    juce::MemoryBlock roundtrip;
    restored->getStateInformation(roundtrip);
    assert(roundtrip==snapshot);
    guitarTyped->sealForRealtime();
    assert(!guitarTyped->restoreUnpublishedState(snapshot.getData(),
        static_cast<int>(snapshot.getSize())));
}

int main() {
    verifySharedNativeFxControlRequest(); 
    verifyFirstSoundGuitarProcessor();
    const auto* found=findNativeBuiltInFxIn(injected,"armix.test.fixture-gain.v1");
    assert(found==&injected[0]);
    assert(!findNativeBuiltInFxIn(injected,""));
    assert(!findNativeBuiltInFxIn(injected,"armix.test.fixture-gain.v2"));
    // Production IDs still resolve through the exact SAME lookup implementation.
    for(const auto& production:armix::builtin_fx::nativeBuiltInFx)
        assert(armix::builtin_fx::findNativeBuiltInFx(production.id)==&production);

    constexpr float gain=1.5f;
    // M4: malformed device rate/quantum must fail before even creating a
    // processor. This prevents huge DSP buffers or bad fractional clocks
    // from a corrupted saved Show/driver request.
    const int beforeInvalid=FixtureGainFx::constructed;
    for(const double rate:{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        0.,7999.,384001.}) {
        assert(!found->prepare(rate,128,&gain,sizeof(gain)));
    }
    for(int frames:{-1,0,1,15,8193,std::numeric_limits<int>::max()}) {
        assert(!found->prepare(48000.,frames,&gain,sizeof(gain)));
    }
    assert(!found->prepare(48000.,128,nullptr,1));
    assert(!found->prepare(48000.,128,&gain,-1));
    assert(!found->prepare(48000.,128,&gain,8*1024*1024+1));
    assert(FixtureGainFx::constructed==beforeInvalid);
    // Host accepts the minimum legitimate 16-frame boundary. The fixture
    // itself deliberately asks for >=32 and may reject after construction.
    assert(!found->prepare(48000.,16,&gain,sizeof(gain)));
    assert(FixtureGainFx::constructed==beforeInvalid+1);
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
