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
    // PREPARE NEXT must not retune the CURRENT backing track, even while
    // the performer presses +/- repeatedly on the queued song's controls.
    assert(Policy::pitchTempoTarget(0,1,3).index==1);
    assert(!Policy::pitchTempoTarget(0,1,3).updateLiveDeck);
    assert(Policy::pitchTempoTarget(0,0,3).updateLiveDeck);
    assert(Policy::pitchTempoTarget(2,-1,3).updateLiveDeck);
    assert(Policy::pitchTempoTarget(-1,1,3).index==1);
    assert(!Policy::pitchTempoTarget(-1,1,3).updateLiveDeck);
    assert(Policy::pitchTempoTarget(-1,99,3).index==-1);
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
    // M2 real JUCE media → sample-rate converter → independent stereo DSP
    // → prepared FIFO; this runs without a physical audio device.
    {
        armix::player::NativePlayerDeck player;
        player.deviceRate(48000.0);
        assert(player.setPitchTempo(3,80));
        assert(!player.setPitchTempo(7,80)); // invalid cannot mutate DSP
        assert(!player.setPitchTempo(3,121));
        player.load(file,true);
        bool prepared=false;
        for(int i=0;i<700;++i) {
            if(player.isPlayingRequested() &&
               !player.snapshot().processingChangePending) {
                prepared=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(prepared);
        const auto preparedStatus=player.snapshot();
        assert(preparedStatus.keySemitones==3);
        assert(preparedStatus.tempoPercent==80);
        assert(preparedStatus.duration>18000); // slower: more output frames
        std::array<float,256> l{},r{};
        double energy=0,stereo=0;
        for(int i=0;i<48;++i) {
            player.pull(l.data(),r.data(),256);
            for(int n=0;n<256;++n) {
                assert(std::isfinite(l[n]) && std::isfinite(r[n]));
                energy+=static_cast<double>(l[n])*l[n];
                stereo+=static_cast<double>(l[n])*r[n];
            }
        }
        assert(energy>0.1 && stereo<0);
        // Independent KEY and TEMPO changes are accepted by the decoder
        // worker without resetting or manipulating the ASIO device clock.
        assert(player.setPitchTempo(-2,120));
        const auto request=player.snapshot();
        assert(request.requestedKey==-2 && request.requestedTempo==120);
        assert(request.deviceRate==48000.0);
        player.stop();
        player.play();
        bool newSettings=false;
        for(int i=0;i<700;++i) {
            const auto st=player.snapshot();
            if(st.keySemitones==-2 && st.tempoPercent==120 &&
               player.isPlayingRequested()) {
                newSettings=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(newSettings);
    }
    // M2 LIVE edits must be accepted and processed WHILE the same song
    // is still playing. A previous test only checked Stop→Play, which
    // could conceal stalled edits after the decoder had read to EOF.
    {
        const auto longFile=juce::File::getSpecialLocation(
            juce::File::tempDirectory).getNonexistentChildFile(
                "arson-player-live-tempo-test",".wav",false);
        {
            std::unique_ptr<juce::AudioFormatWriter> w(
                wave.createWriterFor(longFile.createOutputStream().release(),
                                     44100.0,2,16,{},0));
            assert(w!=nullptr);
            juce::AudioBuffer<float> music(2,44100*8);
            for(int n=0;n<music.getNumSamples();++n) {
                const auto v=static_cast<float>(.3*std::sin(
                    2.0*3.14159265358979323846*330.0*n/44100.0));
                music.setSample(0,n,v);
                music.setSample(1,n,-v);
            }
            assert(w->writeFromAudioSampleBuffer(music,0,
                                                 music.getNumSamples()));
        }
        {
        armix::player::NativePlayerDeck player;
        player.deviceRate(48000.0);
        player.load(longFile,true);
        bool started=false;
        for(int i=0;i<600;++i) {
            if(player.isPlayingRequested()) {started=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(started);
        std::array<float,256> left{},right{};
        for(int i=0;i<24;++i) {
            player.pull(left.data(),right.data(),256);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto original=player.snapshot();
        assert(original.played>0 && original.deviceRate==48000);
        const auto originalGeneration=original.generation;
        const auto changeDuringPlayback=[&](int key,int tempo) {
            assert(player.setPitchTempo(key,tempo));
            bool applied=false;
            for(int i=0;i<750;++i) {
                player.pull(left.data(),right.data(),256);
                const auto current=player.snapshot();
                for(int n=0;n<256;++n) {
                    assert(std::isfinite(left[n]));
                    assert(std::isfinite(right[n]));
                }
                if(current.keySemitones==key &&
                   current.tempoPercent==tempo &&
                   !current.processingChangePending) {
                    assert(current.generation==originalGeneration);
                    assert(current.deviceRate==48000);
                    applied=true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            assert(applied); // no Stop, Play or hardware rebind required
        };
        changeDuringPlayback(3,80);
        changeDuringPlayback(-2,120);
        changeDuringPlayback(0,100); // normal 0/100 passthrough restored
        assert(player.snapshot().played>original.played);
        } // ensure native decoder closes the file before Windows deletion
        assert(longFile.deleteFile());
    }
    assert(file.deleteFile());
}
