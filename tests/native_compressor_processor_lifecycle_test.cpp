// SPDX-License-Identifier: AGPL-3.0-only
// Real JUCE processor lifecycle, ACK, Show state and PCM output verification.
// No hardware device required: reproduces the stop/restart sequence directly.
#include "../engine/builtin_fx/ChannelCompressorProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
using armix::builtin_fx::ChannelCompressor;
using armix::builtin_fx::ChannelCompressorProcessor;
using armix::builtin_fx::CompressorSettings;
#define check(x) do {if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",\
    __FILE__,__LINE__,#x);std::abort();}}while(false)

static bool equals(const CompressorSettings& a,const CompressorSettings& b) {
    return a.thresholdDb==b.thresholdDb && a.ratio==b.ratio &&
        a.attackMs==b.attackMs && a.releaseMs==b.releaseMs &&
        a.kneeDb==b.kneeDb && a.makeupDb==b.makeupDb && a.mix==b.mix;
}
static void compareBlock(ChannelCompressorProcessor& p,ChannelCompressor& reference,
                         int frames) {
    juce::AudioBuffer<float> block(2,frames);
    std::array<float,257> l{},r{};
    check(frames>0 && frames<=static_cast<int>(l.size()));
    for(int k=0;k<frames;++k) {
        const float v=static_cast<float>(.41+.2*std::sin(k*.17));
        l[static_cast<std::size_t>(k)]=v;
        r[static_cast<std::size_t>(k)]=-v*.55f;
        block.setSample(0,k,v);
        block.setSample(1,k,-v*.55f);
    }
    juce::MidiBuffer midi;
    p.processBlock(block,midi);
    reference.processStereo(l.data(),r.data(),static_cast<std::size_t>(frames));
    for(int k=0;k<frames;++k){
        check(std::abs(block.getSample(0,k)-l[static_cast<std::size_t>(k)])<2.e-6f);
        check(std::abs(block.getSample(1,k)-r[static_cast<std::size_t>(k)])<2.e-6f);
    }
    check(std::abs(p.observedInputPeakDbfs()-
        armix::builtin_fx::peakToDbfs(reference.inputPeak()))<.001f);
    check(std::abs(p.observedOutputPeakDbfs()-
        armix::builtin_fx::peakToDbfs(reference.outputPeak()))<.001f);
    check(std::abs(p.observedGainReductionDb()-reference.gainReductionDb())<.001f);
}
static void testUnpolledAckNeverRevertsAppliedDSP() {
    for(double startRate:{44100.,48000.,96000.}) {
        ChannelCompressorProcessor processor;
        processor.prepareToPlay(startRate,128);
        check(processor.isReady());
        processor.sealForRealtime();
        CompressorSettings loud{};
        loud.thresholdDb=-48.f;loud.ratio=14.f;
        loud.attackMs=.1f;loud.releaseMs=80.f;loud.kneeDb=18.f;
        loud.makeupDb=4.f;loud.mix=.82f;
        check(processor.queueSettings(loud,101));
        juce::AudioBuffer<float> first(2,128);
        first.clear();first.applyGain(.5f);
        for(int i=0;i<128;++i){first.setSample(0,i,.61f);first.setSample(1,i,-.31f);}
        juce::MidiBuffer midi;
        processor.processBlock(first,midi);
        // The edit has reached DSP, but completion 101 is deliberately
        // still in the SPSC queue and the UI ACK shadow remains old.
        check(processor.stateSnapshotQuiescent()==false);
        check(processor.acceptedSettings().ratio!=loud.ratio);
        const auto serial=processor.observedBlockSerial();
        processor.releaseResources();
        const double nextRate=startRate==96000.?44100.:96000.;
        processor.prepareToPlay(nextRate,128);
        check(processor.isReady() && processor.acceptedSampleRate()==nextRate);
        check(processor.observedBlockSerial()==serial);
        ChannelCompressor expected;
        check(expected.prepare(nextRate)&&expected.resetUnpublished(loud));
        compareBlock(processor,expected,128);
        // Pending prior-device acknowledgement remains ordered and valid.
        ChannelCompressorProcessor::Completion completion{};
        check(processor.pollCompletion(completion));
        check(completion.requestId==101 && completion.applied);
        check(equals(completion.settings,loud));
        check(equals(processor.acceptedSettings(),loud));
        check(!processor.pollCompletion(completion));
        check(processor.stateSnapshotQuiescent());
        // An edit still QUEUED at old sample rate must fail explicitly,
        // not be silently applied with stale detector coefficients.
        CompressorSettings newer=loud;
        newer.ratio=2.f;newer.thresholdDb=-15.f;newer.mix=.45f;
        check(processor.queueSettings(newer,102));
        processor.releaseResources();
        const double anotherRate=nextRate==48000.?44100.:48000.;
        processor.prepareToPlay(anotherRate,257);
        check(processor.acceptedSampleRate()==anotherRate);
        expected.prepare(anotherRate);check(expected.resetUnpublished(loud));
        compareBlock(processor,expected,257);
        check(processor.pollCompletion(completion));
        check(completion.requestId==102 && !completion.applied);
        check(!processor.pollCompletion(completion));
        check(equals(processor.acceptedSettings(),loud));
        // Retry via freshly prepared coefficients at the current rate.
        check(processor.queueSettings(newer,103));
        processor.processBlock(first,midi);
        check(processor.pollCompletion(completion));
        check(completion.requestId==103 && completion.applied);
        check(equals(processor.acceptedSettings(),newer));
        processor.releaseResources();
        processor.prepareToPlay(96000.,128);
        check(processor.isReady());
        expected.prepare(96000.);check(expected.resetUnpublished(newer));
        compareBlock(processor,expected,128);
    }
}
static void testColdShowRestoreAndRestart() {
    CompressorSettings scene{};
    scene.thresholdDb=-38.f;scene.ratio=8.5f;scene.attackMs=2.f;
    scene.kneeDb=21.f;scene.mix=.35f;scene.makeupDb=11.f;
    juce::MemoryBlock memory;
    {
        ChannelCompressorProcessor donor;
        // This source is not published; state writes go from CONTROL.
        donor.prepareToPlay(44100.,128);
        juce::MemoryOutputStream writer(memory,false);
        writer.writeInt(0x41434d50);writer.writeInt(1);
        writer.writeFloat(scene.thresholdDb);writer.writeFloat(scene.ratio);
        writer.writeFloat(scene.attackMs);writer.writeFloat(scene.releaseMs);
        writer.writeFloat(scene.kneeDb);writer.writeFloat(scene.makeupDb);
        writer.writeFloat(scene.mix);
    }
    check(memory.getSize()==36);
    ChannelCompressorProcessor cold;
    check(cold.restoreUnpublishedState(memory.getData(),static_cast<int>(memory.getSize())));
    cold.prepareToPlay(48000.,128);
    check(cold.isReady());
    check(equals(cold.acceptedSettings(),scene));
    ChannelCompressor expected;
    check(expected.prepare(48000.)&&expected.resetUnpublished(scene));
    compareBlock(cold,expected,128);
    cold.releaseResources();
    cold.prepareToPlay(96000.,128);
    check(expected.prepare(96000.)&&expected.resetUnpublished(scene));
    compareBlock(cold,expected,128);
    cold.sealForRealtime();
    check(!cold.restoreUnpublishedState(memory.getData(),static_cast<int>(memory.getSize())));
    juce::MemoryBlock snapshot;
    cold.getStateInformation(snapshot);
    check(snapshot.getSize()==36);
    ChannelCompressorProcessor recovered;
    check(recovered.restoreUnpublishedState(snapshot.getData(),static_cast<int>(snapshot.getSize())));
    recovered.prepareToPlay(44100.,128);
    check(equals(recovered.acceptedSettings(),scene));
    check(expected.prepare(44100.)&&expected.resetUnpublished(scene));
    compareBlock(recovered,expected,128);
}
int main() {
    testUnpolledAckNeverRevertsAppliedDSP();
    testColdShowRestoreAndRestart();
    std::puts("PASS: native Compressor JUCE restart / ACK / Show / PCM parity");
}
