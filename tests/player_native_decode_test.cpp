// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/NativePlayerDeck.h"
#include "../engine/player/NativePlayerShowDeck.h"
#include "../engine/player/PlayerSetlistPolicy.h"
#include "../engine/player/PlayerLibraryPolicy.h"
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
    // A singer can select a folder containing nested backing tracks.
    // Import order is natural, bounded and excludes unrelated files.
    const auto folder=juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "arson-player-library-policy","",false);
    assert(folder.createDirectory().wasOk());
    const auto nested=folder.getChildFile("subfolder");
    assert(nested.createDirectory().wasOk());
    assert(folder.getChildFile("10-outro.FLAC").create().wasOk());
    assert(folder.getChildFile("02-verse.mp3").create().wasOk());
    assert(folder.getChildFile("01-intro.wav").create().wasOk());
    assert(folder.getChildFile("garbage.txt").create().wasOk());
    assert(nested.getChildFile("03-chorus.aiff").create().wasOk());
    const auto scanned=armix::player::PlayerLibraryPolicy::scanFolder(folder);
    assert(!scanned.truncated && scanned.files.size()==4);
    assert(scanned.files[0].getFileName()=="01-intro.wav");
    assert(scanned.files[1].getFileName()=="02-verse.mp3");
    assert(scanned.files[2].getFileName()=="03-chorus.aiff");
    assert(scanned.files[3].getFileName()=="10-outro.FLAC");
    assert(!armix::player::PlayerLibraryPolicy::supported(
        folder.getChildFile("garbage.txt")));
    // Missing track relink requires explicit matching filename; never
    // silently substitute a different backing song or change its preset.
    const auto missing=folder.getChildFile("old").getChildFile("02-verse.mp3");
    assert(!missing.existsAsFile());
    assert(armix::player::PlayerLibraryPolicy::sameSongName(
        missing,folder.getChildFile("02-verse.mp3")));
    assert(!armix::player::PlayerLibraryPolicy::sameSongName(
        missing,folder.getChildFile("01-intro.wav")));
    // An operator selects the NEW USB folder once. Only an unambiguous
    // exact full filename can repair an old missing file path; a different
    // extension with the same stem does not silently replace the song.
    using Library=armix::player::PlayerLibraryPolicy;
    const auto lost1=folder.getChildFile("old").getChildFile("01-intro.wav");
    const auto lost2=folder.getChildFile("old").getChildFile("02-verse.mp3");
    const auto lost3=folder.getChildFile("old").getChildFile("03-chorus.aiff");
    std::vector<juce::File> saved={lost1,lost2,lost3};
    auto repaired=Library::planMissingRelinks(saved,scanned);
    assert(repaired.size()==3);
    auto truncated=scanned;
    truncated.truncated=true;
    assert(Library::planMissingRelinks(saved,truncated).empty());
    assert(repaired[0].index==0 && repaired[0].replacement==
           folder.getChildFile("01-intro.wav"));
    assert(repaired[1].index==1 && repaired[1].replacement==
           folder.getChildFile("02-verse.mp3"));
    assert(repaired[2].index==2 && repaired[2].replacement==
           nested.getChildFile("03-chorus.aiff"));
    // Preserve CURRENT live source even if its old file was unplugged.
    repaired=Library::planMissingRelinks(saved,scanned,1);
    assert(repaired.size()==2 && repaired[0].index==0 &&
           repaired[1].index==2);
    // A song already represented elsewhere must not gain a second owner.
    saved.push_back(folder.getChildFile("01-intro.wav"));
    repaired=Library::planMissingRelinks(saved,scanned);
    assert(repaired.size()==2);
    // Ambiguous same-name alternatives in subfolders are never guessed.
    const auto duplicate=nested.getChildFile("02-verse.mp3");
    assert(duplicate.create().wasOk());
    const auto ambiguous=Library::scanFolder(folder);
    repaired=Library::planMissingRelinks(saved,ambiguous);
    assert(repaired.size()==1 && repaired.front().index==2);
    // Two missing IDs with the same basename must not collapse into one.
    saved.push_back(folder.getChildFile("another").getChildFile("03-chorus.aiff"));
    repaired=Library::planMissingRelinks(saved,ambiguous);
    assert(repaired.empty());
    // Extension and name must match exactly even when a stem matches.
    assert(Library::planMissingRelinks(
        {folder.getChildFile("old").getChildFile("02-verse.wav")},
        Library::Scan{{folder.getChildFile("02-verse.mp3")},false}).empty());
    assert(folder.deleteRecursively());
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
        // Relinking an edited/shorter file must reject a stale IN marker,
        // not play a misleading 1-sample tail as if music were valid.
        {
            armix::player::NativePlayerDeck badCue;
            badCue.deviceRate(48000.0);
            assert(badCue.setCueRange(9.0,0.0)); // valid metadata, not this WAV
            badCue.load(longFile,true);         // genuine file is 8 seconds
            bool rejected=false;
            for(int i=0;i<600;++i) {
                const auto state=badCue.snapshot();
                if(state.state==armix::player::NativePlayerDeck::State::error) {
                    assert(state.error.containsIgnoreCase("IN marker"));
                    rejected=true;break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(rejected && !badCue.snapshot().outputReady);
            assert(badCue.setCueRange(1.0,4.0));
            badCue.load(longFile,true);
            bool recovered=false;
            for(int i=0;i<600;++i) {
                if(badCue.isPlayingRequested()){recovered=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(recovered);
            badCue.stop();
        }
        // M2: genuine JUCE WAV -> resampler -> generation-fenced seek,
        // with progressive waveform indexed ONLY during decoding.
        {
            armix::player::NativePlayerDeck seekDeck;
            seekDeck.deviceRate(48000.0);
            seekDeck.load(longFile,true);
            for(int attempt=0;attempt<600 && !seekDeck.isPlayingRequested();++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(seekDeck.isPlayingRequested());
            std::array<float,512> l{},r{};
            for(int i=0;i<12;++i)seekDeck.pull(l.data(),r.data(),512);
            const auto beforeSeek=seekDeck.snapshot();
            assert(beforeSeek.played>0);
            assert(seekDeck.seekSeconds(3.25));
            bool seekReady=false;
            for(int i=0;i<600;++i) {
                const auto state=seekDeck.snapshot();
                if(state.outputReady &&
                   state.played>static_cast<std::uint64_t>(3*48000)) {
                    seekReady=true;break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(seekReady);
            seekDeck.pull(l.data(),r.data(),512);
            const auto afterSeek=seekDeck.snapshot();
            assert(afterSeek.played>static_cast<std::uint64_t>(3*48000));
            int waveformEvidence=0;
            for(const auto value:afterSeek.waveform)
                if(value>1000)++waveformEvidence;
            assert(waveformEvidence>0);
            for(int i=0;i<512;++i)
                assert(std::isfinite(l[i]) && std::isfinite(r[i]));
            seekDeck.stop();
        }
        // M3: preloaded B comes from a SECOND DECODER WORKER, yet A and B
        // merge as ONE existing PLAYER A stereo PCM signal. No second
        // hardware callback, device, browser track or routing authority.
        {
            armix::player::NativePlayerShowDeck show;
            show.deviceRate(48000.0);
            show.load(longFile,true);
            for(int attempt=0;attempt<600 && !show.isPlayingRequested();++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(show.isPlayingRequested());
            assert(show.preload(file,0,100));
            std::array<float,256> left{},right{};
            bool queued=false;
            for(int i=0;i<600;++i) {
                if(i==0) {
                    assert(show.queueTransition(file,0,100,180));
                }
                if(show.service()){queued=true;break;}
                show.pull(left.data(),right.data(),256);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(queued && show.transitionActive());
            bool heardSound=false;
            for(int i=0;i<70;++i) {
                show.pull(left.data(),right.data(),256);
                for(int n=0;n<256;++n) {
                    assert(std::isfinite(left[n]) && std::isfinite(right[n]));
                    if(std::abs(left[n])>0.01f)heardSound=true;
                }
            }
            assert(heardSound && !show.transitionActive());
            show.service(); // retire old decoder off realtime
            assert(show.snapshot().title==file.getFileNameWithoutExtension());
            show.deviceRate(0.0);
            assert(!show.isPlayingRequested());
        }
        // M2 + M3 real PCM: A-B rehearsal uses TWO decoder instances
        // of the SAME file, fades between them without a callback seek,
        // preserves original media clock, and cancels NEXT on STOP.
        {
            armix::player::NativePlayerShowDeck loop;
            loop.deviceRate(48000.0);
            assert(loop.setCueRange(1.0,4.0));
            loop.load(longFile,true);
            bool ready=false;
            for(int i=0;i<600;++i) {
                if(loop.isPlayingRequested()){ready=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(ready);
            const auto first=loop.snapshot();
            assert(first.mediaPlayed>=48000); // cue IN, not zero
            std::array<float,256> left{},right{};
            // Deliberately do NOT call service() between fades: a live
            // operator may queue NEXT immediately on callback completion.
            // preload() must retire the old decoder before reusing it.
            for(int pass=0;pass<4;++pass) {
                assert(loop.preload(longFile,0,100,1.,4.));
                assert(loop.queueTransition(longFile,0,100,180,1.,4.));
                bool started=false;
                for(int tries=0;tries<600;++tries) {
                    if(loop.service()){started=true;break;}
                    loop.pull(left.data(),right.data(),256);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                assert(started && loop.transitionActive());
                double energy=0.;
                for(int b=0;b<50;++b) {
                    loop.pull(left.data(),right.data(),256);
                    for(int i=0;i<256;++i) {
                        assert(std::isfinite(left[i]) &&
                               std::isfinite(right[i]));
                        energy+=left[i]*left[i];
                    }
                }
                assert(energy>0.01);
                assert(!loop.transitionActive());
                const auto now=loop.snapshot();
                assert(now.mediaPlayed>=48000);
                assert(now.mediaDuration<=4*48000+1);
                assert(now.title==longFile.getFileNameWithoutExtension());
            }
            // A user STOP during standby must prevent any stale queue from
            // becoming a different current after the decoder completes.
            assert(loop.queueTransition(longFile,0,100,180,1.,4.));
            assert(loop.transitionPending());
            loop.stop();
            assert(!loop.transitionPending());
            assert(!loop.isPlayingRequested());
            loop.pull(left.data(),right.data(),256);
            for(int i=0;i<256;++i)
                assert(left[i]==0.f && right[i]==0.f);
            // Restart across 96 -> disconnected -> 44.1 kHz. Pending
            // standby PCM must not leak into the next hardware epoch,
            // and device reopen itself must NEVER auto-play into MAIN.
            loop.deviceRate(96000.0);
            loop.load(longFile,true);
            bool warm96=false;
            for(int i=0;i<600;++i) {
                if(loop.isPlayingRequested()){warm96=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(warm96);
            assert(loop.queueTransition(longFile,0,100,500,1.,4.));
            assert(loop.transitionPending());
            loop.deviceRate(0.0);
            assert(!loop.transitionPending());
            assert(!loop.transitionActive());
            assert(!loop.isPlayingRequested());
            loop.deviceRate(44100.0);
            bool ready44=false;
            for(int i=0;i<600;++i) {
                if(loop.snapshot().outputReady){ready44=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            assert(ready44);
            assert(!loop.isPlayingRequested()); // explicit Play required
            loop.pull(left.data(),right.data(),256);
            for(int i=0;i<256;++i)
                assert(left[i]==0.f && right[i]==0.f);
            loop.play();
            assert(loop.isPlayingRequested());
            loop.pull(left.data(),right.data(),256);
            for(int i=0;i<256;++i)
                assert(std::isfinite(left[i]) && std::isfinite(right[i]));
            loop.stop();
        }
        // M5 integration: a pitch-shifted, tempo-stretched real JUCE WAV
        // must drain BOTH spectral latencies when M4 IN/OUT trims apply.
        // The media clock and WAV source must remain bounded by cue OUT.
        {
            armix::player::NativePlayerDeck cuePitch;
            cuePitch.deviceRate(48000.0);
            assert(cuePitch.setCueRange(1.0,1.4));
            assert(cuePitch.setPitchTempo(3,80));
            cuePitch.load(longFile,true);
            for(int attempt=0;attempt<600 &&
                !cuePitch.isPlayingRequested();++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(cuePitch.isPlayingRequested());
            std::array<float,512> l{},r{};
            double energy=0.,endingEnergy=0.;
            bool ended=false;
            for(int block=0;block<800 && !ended;++block) {
                cuePitch.pull(l.data(),r.data(),512);
                const auto state=cuePitch.snapshot();
                assert(state.state!=armix::player::NativePlayerDeck::State::error);
                for(int i=0;i<512;++i) {
                    assert(std::isfinite(l[i]) && std::isfinite(r[i]));
                    energy+=static_cast<double>(l[i])*l[i];
                    if(state.mediaDuration>0 &&
                       state.mediaPlayed>=state.mediaDuration)
                        endingEnergy+=static_cast<double>(l[i])*l[i];
                }
                ended=state.state==armix::player::NativePlayerDeck::State::ended;
                if(!ended)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto last=cuePitch.snapshot();
            assert(ended && last.played==last.duration);
            assert(last.mediaDuration>=static_cast<std::uint64_t>(1.39*48000));
            assert(last.mediaDuration<=static_cast<std::uint64_t>(1.41*48000));
            assert(last.mediaPlayed<=last.mediaDuration);
            assert(energy>0.1 && endingEnergy>0.0001);
        }
        assert(longFile.deleteFile());
    }
    assert(file.deleteFile());
}
