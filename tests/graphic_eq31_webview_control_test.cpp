// SPDX-License-Identifier: AGPL-3.0-only
// Real JUCE processor behind the ONE /preview/ WebView2 Graphic EQ 31 GUI.
// The UI protocol never alters PCM on the browser thread: CONTROL publishes
// APVTS targets and AUDIO acknowledges only after its accepted block.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/GraphicEq31Processor.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <array>
#include <cstdio>

int main(){
    juce::ScopedJuceInitialiser_GUI init;
    armix::builtin_fx::GraphicEq31Processor fx;
    fx.prepareToPlay(48000.,128);
    assert(fx.isReady());
    assert(fx.getLatencySamples()==0);
    assert(fx.stateSnapshotQuiescent());
    auto step=[&](std::uint64_t expected){
        juce::AudioBuffer<float> audio(2,128);
        audio.clear();
        juce::MidiBuffer midi;
        fx.processBlock(audio,midi);
        armix::builtin_fx::GraphicEq31Processor::Completion done{};
        assert(fx.pollCompletion(done));
        assert(done.applied&&done.requestId==expected);
        assert(!fx.pollCompletion(done));
        assert(fx.stateSnapshotQuiescent());
    };
    assert(!fx.queueBandEdit(31,4.f,1));
    assert(!fx.queueBandEdit(0,std::numeric_limits<float>::quiet_NaN(),1));
    assert(!fx.queueOutputEdit(-25.f,1));
    assert(!fx.queueBandEdit(0,5.f,0));
    assert(fx.queueBandEdit(0,5.f,101));
    assert(!fx.stateSnapshotQuiescent());
    step(101);
    assert(std::abs(fx.acceptedBands()[0]-5.f)<.11f);
    assert(fx.queueOutputEdit(-6.f,102));
    step(102);
    assert(std::abs(fx.acceptedLevelDb()+6.f)<.11f);
    std::array<float,armix::builtin_fx::GraphicEq31Core::bandCount> curve{};
    curve[0]=12.f;curve[15]=2.5f;curve[30]=-12.f;
    assert(fx.queueCurveEdit(curve,-3.f,103));
    step(103);
    const auto values=fx.acceptedBands();
    assert(std::abs(values[0]-12.f)<.11f);
    assert(std::abs(values[15]-2.5f)<.11f);
    assert(std::abs(values[30]+12.f)<.11f);
    assert(std::abs(fx.acceptedLevelDb()+3.f)<.11f);
    const auto before=fx.acceptedBands();
    curve[15]=99.f;
    assert(!fx.queueCurveEdit(curve,0.f,104));  // all-or-nothing
    const auto after=fx.acceptedBands();
    assert(before==after);
    // 48000->96000 device restart must not silently lose accepted GEQ.
    fx.releaseResources();
    fx.prepareToPlay(96000.,256);
    assert(fx.isReady());
    assert(std::abs(fx.acceptedBands()[30]+12.f)<.11f);
    assert(fx.queueBandEdit(30,-2.f,105));
    step(105);
    assert(std::abs(fx.acceptedBands()[30]+2.f)<.11f);
    assert(fx.getLatencySamples()==0);
    std::puts("GraphicEq31WebViewAudioAckAndAtomic31Bands PASS");
}
