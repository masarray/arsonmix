// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/NativePlayerDeck.h"
#include "../engine/player/PlayerSetlistPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cmath>
#include <memory>
#include <thread>
#include <chrono>

int main() {
    using Policy=armix::player::SetlistPolicy;
    // Live stage semantics: queued NEXT must not hijack pause/resume.
    assert(Policy::resumeCurrentOnPlay("PAUSED",3));
    assert(Policy::resumeCurrentOnPlay("PREPARING",3));
    assert(Policy::resumeCurrentOnPlay("READY",3));
    assert(!Policy::resumeCurrentOnPlay("PLAYING",3));
    assert(!Policy::resumeCurrentOnPlay("ERROR",3));
    assert(!Policy::resumeCurrentOnPlay("PAUSED",-1));
    assert(Policy::nextTrack(3,8,10)==8); // explicit queued song
    assert(Policy::nextTrack(3,3,10)==4); // ordinary sequential
    assert(Policy::nextTrack(9,9,10)==-1); // STOP after last
    assert(Policy::nextTrack(-1,2,10)==2); // first song
    assert(Policy::nextTrack(3,99,10)==4); // invalid queue ignored
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("arson-player-decoder-test",".wav",false);
    juce::WavAudioFormat wave;
    {
        std::unique_ptr<juce::AudioFormatWriter> writer(wave.createWriterFor(
            file.createOutputStream().release(),44100.0,2,16,{},0));
        assert(writer!=nullptr);
        juce::AudioBuffer<float> song(2,16384);
        for(int i=0;i<song.getNumSamples();++i) {
            song.setSample(0,i,.25f);
            song.setSample(1,i,-.15f);
        }
        assert(writer->writeFromAudioSampleBuffer(song,0,song.getNumSamples()));
    }
    {
        armix::player::NativePlayerDeck deck;
        deck.deviceRate(48000.0);
        deck.load(file,true);
        bool ready=false;
        for(int i=0;i<600;++i) {
            if(deck.isPlayingRequested()) {ready=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(ready);
        std::array<float,256> left{},right{};
        // No WebAudio/mock: decode WAV on worker, resample to actual device
        // clock, transfer to real preallocated realtime PCM ring.
        for(int i=0;i<12;++i)
            deck.pull(left.data(),right.data(),256);
        assert(std::abs(left[100]-.25f)<.055f);
        assert(std::abs(right[100]+.15f)<.055f);
        auto snapshot=deck.snapshot();
        assert(snapshot.played>=3072 && snapshot.duration>16384);
        assert(snapshot.deviceRate==48000.0);
        deck.pause();
        left.fill(999.f);right.fill(999.f);
        deck.pull(left.data(),right.data(),256);
        for(int i=0;i<256;++i)
            assert(left[i]==0.f && right[i]==0.f);
        deck.play();
        assert(deck.isPlayingRequested());
        deck.pull(left.data(),right.data(),256);
        assert(std::abs(left[255])>.01f);
        deck.stop();
        left.fill(999.f);right.fill(999.f);
        deck.pull(left.data(),right.data(),256);
        assert(left[0]==0.f && right[0]==0.f);
        deck.play();
        bool restarted=false;
        for(int i=0;i<600;++i) {
            if(deck.isPlayingRequested()) {restarted=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(restarted);
        deck.pull(left.data(),right.data(),256);
        assert(deck.snapshot().played>0);
        // A reopen at identical 48k Hz is still a NEW hardware epoch.
        // Decoder prebuffer may complete, but the source must stay stopped.
        deck.deviceRate(48000.0);
        assert(!deck.isPlayingRequested());
        bool buffered=false;
        for(int i=0;i<600;++i) {
            if(deck.snapshot().outputReady) {buffered=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(buffered && !deck.isPlayingRequested());
        const auto beforeResume=deck.snapshot().played;
        assert(beforeResume==0); // new hardware epoch starts fresh
        deck.pull(left.data(),right.data(),256);
        assert(deck.snapshot().played==beforeResume);
        deck.play(); // first explicit gesture after verified device reopen
        assert(deck.isPlayingRequested());
        deck.pull(left.data(),right.data(),256);
        assert(deck.snapshot().played>beforeResume);
        // Device loss immediately fails silent; no stale PCM at another port.
        deck.deviceRate(0.0);
        assert(!deck.isPlayingRequested());
        left.fill(9.f);right.fill(9.f);
        deck.pull(left.data(),right.data(),256);
        assert(left[0]==0.f && right[0]==0.f);
    }
    assert(file.deleteFile());
}
