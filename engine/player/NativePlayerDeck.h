// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PreparedStereoPcmRing.h"
#include "PlayerPitchTempoProcessor.h"
#include "PlayerWaveformIndex.h"
#include "../core/RealtimeSlew64.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

// ArSon Player A: JUCE decoder + rate conversion on ONE background worker.
// PCM is handed to the existing ChannelId render callback, not mixed here.
// No disk read, allocation, sleep, mutex, JNI, WebAudio or resampler on RT.
namespace armix::player {
class NativePlayerDeck final {
public:
    enum class State : std::uint8_t {
        empty, loading, ready, playing, paused, stopped, ended, error
    };
    struct Status {
        State state=State::empty;
        std::uint64_t played=0,duration=0,underruns=0,generation=0;
        std::uint64_t mediaPlayed=0,mediaDuration=0;
        bool outputReady=false;
        juce::String title,error;
        double deviceRate=0;
        int keySemitones=0,tempoPercent=100;
        int requestedKey=0,requestedTempo=100;
        bool processingChangePending=false;
        std::array<std::uint16_t,PlayerWaveformIndex::bins> waveform{};
    };
    NativePlayerDeck() {
        // 2.7 seconds at 48k, bounded independent of song length.
        if(!ring_.prepare(131072))state_.store(State::error);
        worker_=std::thread([this] { workerLoop(); });
    }
    ~NativePlayerDeck() {
        exit_.store(true,std::memory_order_release);
        wake_.notify_one();
        if(worker_.joinable())worker_.join(); // only CONTROL after callback detach
    }
    NativePlayerDeck(const NativePlayerDeck&)=delete;
    NativePlayerDeck& operator=(const NativePlayerDeck&)=delete;

    // CONTROL only; the audio device callback may concurrently run for
    // earlier generation. Worker atomically invalidates that generation.
    void deviceRate(double hz) noexcept {
        const auto rounded=static_cast<int>(std::lround(hz));
        // Every aboutToStart/stopped callback is a new device epoch, even
        // when the sample rate is unchanged. Never replay stale decoded PCM
        // or silently resume a singer's backing track on hardware reopen.
        ready_.store(false,std::memory_order_release);
        wantedPlay_.store(false,std::memory_order_release);
        rate_.store(rounded>0?rounded:0,std::memory_order_release);
        const bool selected=hasFile_.load(std::memory_order_acquire);
        played_.store(0,std::memory_order_release);
        mediaPlayed_.store(0,std::memory_order_release);
        mediaDuration_.store(0,std::memory_order_release);
        requestedSeekSeconds_.store(cueInSeconds_.load(
            std::memory_order_acquire),std::memory_order_release);
        error_.store(0,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        state_.store(rounded<=0?State::stopped:
                         selected?State::loading:State::empty,
                     std::memory_order_release);
        wake_.notify_one();
    }
    bool setCueRange(double in,double out) noexcept {
        if(!std::isfinite(in) || !std::isfinite(out) ||
           in<0. || out<0. || in>86400. || out>86400. ||
           (out>0. && out<=in+0.05))return false;
        cueInSeconds_.store(in,std::memory_order_release);
        cueOutSeconds_.store(out,std::memory_order_release);
        return true;
    }
    bool setPitchTempo(int keySemitones,int tempoPercent) noexcept {
        const PlayerPitchTempoProcessor::Controls c{keySemitones,tempoPercent};
        if(!PlayerPitchTempoProcessor::valid(c))return false;
        requestedControls_.store(packControls(c),std::memory_order_release);
        return true; // accepted intent; actual DSP readback in snapshot()
    }
    void load(const juce::File& file,bool autoPlay=true) {
        ready_.store(false,std::memory_order_release);
        wantedPlay_.store(autoPlay,std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            selectedFile_=file; // worker owns the reader, not the callback
        }
        hasFile_.store(file!=juce::File{},std::memory_order_release);
        played_.store(0,std::memory_order_release);
        mediaPlayed_.store(0,std::memory_order_release);
        mediaDuration_.store(0,std::memory_order_release);
        requestedSeekSeconds_.store(cueInSeconds_.load(
            std::memory_order_acquire),std::memory_order_release);
        error_.store(0,std::memory_order_release);
        state_.store(State::loading,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        wake_.notify_one();
    }
    // GUI/CONTROL only. The decoder worker owns opening/repositioning the
    // reader and resampler. Every seek supersedes prior queued PCM via the
    // existing generation + quiescence fence; ASIO never calls seek/file IO.
    bool seekSeconds(double seconds) noexcept {
        if(!std::isfinite(seconds) || seconds<0. ||
           !hasFile_.load(std::memory_order_acquire) ||
           rate_.load(std::memory_order_acquire)<=0)return false;
        requestedSeekSeconds_.store(seconds,std::memory_order_release);
        ready_.store(false,std::memory_order_release);
        state_.store(State::loading,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        wake_.notify_one();
        return true;
    }
    bool isPlayingRequested() const noexcept {
        return ready_.load(std::memory_order_acquire) &&
               wantedPlay_.load(std::memory_order_acquire);
    }
    void play() noexcept {
        wantedPlay_.store(true,std::memory_order_release);
        if(ready_.load(std::memory_order_acquire)) {
            if(state_.load(std::memory_order_relaxed)==State::ended)restart();
            else state_.store(State::ready,std::memory_order_release);
        }
    }
    void pause() noexcept {
        wantedPlay_.store(false,std::memory_order_release);
        if(ready_.load(std::memory_order_acquire))
            state_.store(State::paused,std::memory_order_release);
    }
    void stop() noexcept {
        // Stop is an immediate fail-silent transport state. The worker owns
        // the next seek-to-start; never reset/reallocate a ring from RT.
        ready_.store(false,std::memory_order_release);
        wantedPlay_.store(false,std::memory_order_release);
        requestedSeekSeconds_.store(cueInSeconds_.load(
            std::memory_order_acquire),std::memory_order_release);
        state_.store(State::stopped,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        played_.store(0,std::memory_order_relaxed);
        mediaPlayed_.store(0,std::memory_order_relaxed);
        wake_.notify_one();
    }
    Status snapshot() const {
        Status s;
        s.state=state_.load(std::memory_order_acquire);
        s.played=played_.load(std::memory_order_relaxed);
        s.duration=length_.load(std::memory_order_relaxed);
        s.mediaPlayed=mediaPlayed_.load(std::memory_order_relaxed);
        s.mediaDuration=mediaDuration_.load(std::memory_order_relaxed);
        s.underruns=ring_.underrunFrames();
        s.generation=generation_.load(std::memory_order_acquire);
        s.deviceRate=rate_.load(std::memory_order_acquire);
        s.outputReady=ready_.load(std::memory_order_acquire);
        const auto applied=unpackControls(
            appliedControls_.load(std::memory_order_acquire));
        const auto requested=unpackControls(
            requestedControls_.load(std::memory_order_acquire));
        s.keySemitones=applied.keySemitones;
        s.tempoPercent=applied.tempoPercent;
        s.requestedKey=requested.keySemitones;
        s.requestedTempo=requested.tempoPercent;
        s.processingChangePending=(s.keySemitones!=s.requestedKey ||
                                   s.tempoPercent!=s.requestedTempo);
        s.waveform=waveform_.snapshot();
        switch(error_.load(std::memory_order_relaxed)) {
            case 1:s.error="File missing or unreadable";break;
            case 2:s.error="Unsupported or corrupt music format";break;
            case 3:s.error="Audio output not configured";break;
            case 4:s.error="Music pitch/tempo processing failed";break;
            case 5:s.error="Song IN marker exceeds file duration";break;
            default:break;
        }
        {std::lock_guard<std::mutex> lock(commandMutex_);
            s.title=selectedFile_.getFileNameWithoutExtension();}
        return s;
    }

    // AUDIO CALLBACK ONLY; caller supplies 2 disjoint preallocated buffers.
    // The caller passes one resulting DynamicPlayerPcmBlock for *all* mixer
    // strips using Deck A, then calls ONE native graph render.
    void pull(float* left,float* right,int frames) noexcept {
        if(!left||!right||left==right||frames<=0)return;
        const auto silence=[&]() noexcept {
            std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
        };
        if(!ready_.load(std::memory_order_acquire) ||
           !wantedPlay_.load(std::memory_order_acquire)) {
            gain_.reset(0.f); // reset only by AUDIO when inactive
            silence();return;
        }
        readers_.fetch_add(1,std::memory_order_acq_rel);
        if(!ready_.load(std::memory_order_acquire) ||
           !wantedPlay_.load(std::memory_order_acquire)) {
            readers_.fetch_sub(1,std::memory_order_release);
            silence();return;
        }
        // New song/device generation restarts the de-click envelope.
        // AUDIO owns the gain state; the worker only publishes generation.
        const auto generation=generation_.load(std::memory_order_acquire);
        if(generation!=audioGeneration_) {
            audioGeneration_=generation;
            mediaFraction_=0.;
            gain_.reset(0.f);
        }
        const auto n=ring_.read(left,right,static_cast<std::size_t>(frames));
        if(n>0) {
            played_.fetch_add(n,std::memory_order_relaxed);
            // Original media clock is independent of DSP-stretched PCM.
            // The bounded producer lookahead can make this a small estimate,
            // but never treats 80% tempo as a 25%-longer source song.
            const auto control=unpackControls(appliedControls_.load(
                std::memory_order_relaxed));
            const double advance=static_cast<double>(n)*
                control.tempoPercent/100.0+mediaFraction_;
            const auto mediaFrames=static_cast<std::uint64_t>(
                std::floor(advance));
            mediaFraction_=advance-mediaFrames;
            const auto length=mediaDuration_.load(std::memory_order_relaxed);
            const auto present=mediaPlayed_.load(std::memory_order_relaxed);
            mediaPlayed_.store(std::min(length,present+mediaFrames),
                               std::memory_order_relaxed);
            state_.store(State::playing,std::memory_order_relaxed);
        }
        // A short ramp makes decoder/prebuffer start click-free. RT-owned
        // state is not reset by worker, and applies only to this one deck.
        gain_.retarget(1.f);
        for(int i=0;i<frames;++i) {
            const float gain=gain_.next();
            left[i]*=gain;right[i]*=gain;
        }
        if(eof_.load(std::memory_order_acquire) &&
           ring_.bufferedFrames()==0 &&
           played_.load(std::memory_order_relaxed)>=
               length_.load(std::memory_order_relaxed)) {
            wantedPlay_.store(false,std::memory_order_release);
            state_.store(State::ended,std::memory_order_release);
        }
        readers_.fetch_sub(1,std::memory_order_release);
    }

private:
    using Controls=PlayerPitchTempoProcessor::Controls;
    static constexpr std::uint32_t packControls(Controls c) noexcept {
        return (static_cast<std::uint32_t>(c.tempoPercent)<<8) |
               static_cast<std::uint32_t>(c.keySemitones+6);
    }
    static constexpr Controls unpackControls(std::uint32_t bits) noexcept {
        return {static_cast<int>(bits&255u)-6,
                static_cast<int>((bits>>8)&255u)};
    }
    void restart() noexcept {
        requestedSeekSeconds_.store(cueInSeconds_.load(
            std::memory_order_acquire),std::memory_order_release);
        ready_.store(false,std::memory_order_release);
        played_.store(0,std::memory_order_relaxed);
        state_.store(State::loading,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        wake_.notify_one();
    }
    bool obsolete(std::uint64_t generation) const noexcept {
        return exit_.load(std::memory_order_acquire) ||
               generation_.load(std::memory_order_acquire)!=generation;
    }
    void workerLoop() {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::uint64_t seen=0;
        while(!exit_.load(std::memory_order_acquire)) {
            const auto version=generation_.load(std::memory_order_acquire);
            if(version==seen) {
                // Sleep until a real command/device event. No 4Hz idle
                // timer and no locks/waits whatsoever in JUCE's callback.
                std::unique_lock<std::mutex> lock(wakeMutex_);
                wake_.wait(lock,[&] {
                    return exit_.load(std::memory_order_acquire) ||
                           generation_.load(std::memory_order_acquire)!=seen;
                });
                continue;
            }
            seen=version;
            ready_.store(false,std::memory_order_release);
            // Audio may be completing the earlier source generation. Wait
            // OFF realtime before resetting SPSC cursor/index ownership.
            while(readers_.load(std::memory_order_acquire)!=0 &&
                  !exit_.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(obsolete(version))continue;
            ring_.resetQuiescent();
            eof_.store(false,std::memory_order_release);
            length_.store(0,std::memory_order_relaxed);
            juce::File file;
            {std::lock_guard<std::mutex> lock(commandMutex_);
                file=selectedFile_;}
            if(file==juce::File{})continue;
            const int deviceHz=rate_.load(std::memory_order_acquire);
            if(deviceHz<=0) {
                error_.store(3,std::memory_order_relaxed);
                continue;
            }
            if(!file.existsAsFile()) {
                error_.store(1,std::memory_order_relaxed);
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if(!reader || reader->sampleRate<8000.0 ||
               reader->numChannels<1 || reader->numChannels>2 ||
               reader->lengthInSamples<=0) {
                error_.store(2,std::memory_order_relaxed);
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            // A saved cue may belong to an older version of a relinked
            // file. Never silently clamp IN to the last audio sample and
            // report a successful (almost silent) playback.
            const double cueIn=cueInSeconds_.load(std::memory_order_acquire);
            const double mediaSeconds=static_cast<double>(
                reader->lengthInSamples)/reader->sampleRate;
            if(!std::isfinite(cueIn) || cueIn>=mediaSeconds) {
                error_.store(5,std::memory_order_release);
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            const bool mono=reader->numChannels==1;
            const double sourceRate=reader->sampleRate;
            const auto sourceLength=reader->lengthInSamples;
            const double ratio=sourceRate/static_cast<double>(deviceHz);
            const auto sourceTotal=static_cast<std::uint64_t>(std::ceil(
                static_cast<double>(reader->lengthInSamples)/ratio));
            if(!sourceTotal) {
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            if(file!=waveformFile_) {
                waveformFile_=file; // worker owns identity
                waveform_.reset(sourceTotal);
            }
            const double positionSeconds=requestedSeekSeconds_.load(
                std::memory_order_acquire);
            const juce::int64 readerPosition=static_cast<juce::int64>(
                std::clamp(std::floor(positionSeconds*sourceRate),0.0,
                           static_cast<double>(sourceLength-1)));
            using Controls=PlayerPitchTempoProcessor::Controls;
            Controls controls=unpackControls(
                requestedControls_.load(std::memory_order_acquire));
            PlayerPitchTempoProcessor pitch;
            if(!pitch.prepare(deviceHz,controls)) {
                error_.store(4,std::memory_order_release);
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            appliedControls_.store(packControls(controls),
                                   std::memory_order_release);
            juce::AudioFormatReaderSource source(reader.release(),true);
            source.setNextReadPosition(readerPosition); // worker-only seek
            juce::ResamplingAudioSource resampled(&source,false,2);
            resampled.setResamplingRatio(ratio);
            resampled.prepareToPlay(1536,static_cast<double>(deviceHz));
            // Source is device-rate stereo, but independent KEY/TEMPO DSP is
            // computed ONLY here on the decoder worker; not in JUCE/ASIO RT.
            juce::AudioBuffer<float> input(2,1536),output(2,1024);
            std::uint64_t consumed=static_cast<std::uint64_t>(
                std::floor(static_cast<double>(readerPosition)/ratio));
            const auto startPlayed=static_cast<std::uint64_t>(
                std::floor(consumed*100.0/controls.tempoPercent));
            std::uint64_t written=startPlayed;
            played_.store(startPlayed,std::memory_order_release);
            mediaPlayed_.store(consumed,std::memory_order_release);
            mediaDuration_.store(sourceTotal,std::memory_order_release);
            // OUT=0 means physical media end. Reader/decoder owns this
            // bound; the audio callback never edits an I/O source.
            const auto readEnd=[&]() -> std::uint64_t {
                const double out=cueOutSeconds_.load(
                    std::memory_order_acquire);
                if(out<=0.)return sourceTotal;
                return std::min(sourceTotal,static_cast<std::uint64_t>(
                    std::ceil(out*deviceHz)));
            };
            const auto estimateTotal=[&]() -> std::uint64_t {
                // Keep M4's cue-OUT media boundary, but also account for
                // the analysis pre-roll and synthesis tail when KEY/TEMPO
                // requires spectral processing. Never extend mediaPlayed
                // or decode the source past the non-destructive OUT marker.
                return written+static_cast<std::uint64_t>(std::ceil(
                    (readEnd()>consumed?readEnd()-consumed:0)*
                    100.0/controls.tempoPercent))
                    +static_cast<std::uint64_t>(std::ceil(
                        pitch.inputLatency()*100.0/controls.tempoPercent))
                    +static_cast<std::uint64_t>(pitch.outputLatency());
            };
            length_.store(estimateTotal(),std::memory_order_release);
            bool failed=false;
            // Keep only ~250ms of queued music. A 750ms prebuffer made
            // live KEY/TEMPO gestures audibly late, and could decode short
            // songs to EOF before an edit was seen. The ring's 2.7s storage
            // still provides a bounded fault barrier; only its fill target
            // changes, leaving the vocal ASIO callback entirely untouched.
            // Scale with native rate so 44.1/48/96k behave consistently.
            const auto highWater=std::min<std::size_t>(
                ring_.capacityFrames()-2048,
                std::max<std::size_t>(8192,static_cast<std::size_t>(deviceHz/4)));
            while(!obsolete(version) && consumed<readEnd()) {
                if(ring_.bufferedFrames()>highWater) {
                    // A parked/preloaded NEXT has no callback consumer yet.
                    // Yield longer while standby so a singer browsing the
                    // setlist does not spin a 333Hz decoder polling loop;
                    // the 250ms PCM cushion still covers normal Play start.
                    std::this_thread::sleep_for(std::chrono::milliseconds(
                        wantedPlay_.load(std::memory_order_acquire)?3:20));
                    continue;
                }
                Controls requested=unpackControls(
                    requestedControls_.load(std::memory_order_acquire));
                if(requested!=controls && PlayerPitchTempoProcessor::valid(requested)) {
                    pitch.setControls(requested); // DSP worker, no callback code
                    controls=requested;
                    appliedControls_.store(packControls(controls),
                                           std::memory_order_release);
                }
                int nOutput=1024;
                int nInput=pitch.nextInputFrames(nOutput);
                const auto remaining=readEnd()-consumed;
                if(remaining<static_cast<std::uint64_t>(nInput)) {
                    nOutput=std::max(1,static_cast<int>(std::ceil(
                        remaining*100.0/controls.tempoPercent)));
                    nOutput=std::min(1024,nOutput);
                    nInput=static_cast<int>(remaining);
                }
                if(nInput<=0 || nInput>1536 || nOutput<=0) {
                    failed=true;break;
                }
                input.clear();output.clear();
                resampled.getNextAudioBlock({&input,0,nInput});
                if(mono)input.copyFrom(1,0,input,0,0,nInput);
                if(obsolete(version))break;
                // Construct waveform on the SAME decode pass. No second
                // filesystem scan, peak generation in RT, or UI-heap work.
                waveform_.observe(input.getReadPointer(0),
                                  input.getReadPointer(1),nInput,consumed);
                const float* const sourcePcm[]={input.getReadPointer(0),
                                                input.getReadPointer(1)};
                float* const outputPcm[]={output.getWritePointer(0),
                                          output.getWritePointer(1)};
                if(!pitch.process(sourcePcm,nInput,outputPcm,nOutput)) {
                    failed=true;break;
                }
                const auto transferred=ring_.write(outputPcm[0],outputPcm[1],
                                                    static_cast<std::size_t>(nOutput));
                if(transferred!=static_cast<std::size_t>(nOutput)) {
                    failed=true;break;
                }
                consumed+=static_cast<std::uint64_t>(nInput);
                written+=transferred;
                mediaDuration_.store(readEnd(),std::memory_order_release);
                length_.store(estimateTotal(),std::memory_order_release);
                if(!ready_.load(std::memory_order_relaxed) &&
                   (ring_.bufferedFrames()>=4096 || consumed==sourceTotal)) {
                    ready_.store(true,std::memory_order_release);
                    if(!wantedPlay_.load(std::memory_order_acquire) &&
                       state_.load(std::memory_order_relaxed)!=State::stopped)
                        state_.store(State::ready,std::memory_order_release);
                }
            }
            // M5: finish the analysis window with silence, THEN flush the
            // synthesis window. Both are worker-only; preserves M4 cue OUT,
            // waveform, dual-deck transitions, and the one PLAYER A PCM path.
            if(!failed && !obsolete(version) && consumed>=readEnd() &&
               pitch.active()) {
                int analysisToDrain=pitch.inputLatency();
                if(analysisToDrain<0 || analysisToDrain>deviceHz)
                    failed=true;
                while(!failed && !obsolete(version) && analysisToDrain>0) {
                    if(ring_.bufferedFrames()>highWater) {
                        // Standby NEXT is never allowed to spin at 333 Hz.
                        std::this_thread::sleep_for(std::chrono::milliseconds(
                            wantedPlay_.load(std::memory_order_acquire)?3:20));
                        continue;
                    }
                    // Use the actual smoothed rate, not the abrupt requested
                    // TEMPO, to bound the source/drain ratio after a gesture.
                    const auto rate=std::clamp(
                        pitch.smoothedTempoPercent(),80.0,120.0);
                    const int nOutput=std::min(1024,std::max(1,
                        static_cast<int>(std::ceil(
                            analysisToDrain*100.0/rate))));
                    const int nInput=std::clamp(
                        pitch.nextInputFrames(nOutput),1,analysisToDrain);
                    input.clear();output.clear();
                    const float* const silencePcm[]={
                        input.getReadPointer(0),input.getReadPointer(1)};
                    float* const tailPcm[]={
                        output.getWritePointer(0),output.getWritePointer(1)};
                    if(!pitch.process(silencePcm,nInput,tailPcm,nOutput)) {
                        failed=true;break;
                    }
                    const auto moved=ring_.write(
                        tailPcm[0],tailPcm[1],
                        static_cast<std::size_t>(nOutput));
                    if(moved!=static_cast<std::size_t>(nOutput)) {
                        failed=true;break;
                    }
                    analysisToDrain-=nInput;
                    written+=moved;
                }
                const int tail=pitch.outputLatency();
                if(!failed && tail>0 && tail<deviceHz*2) {
                    juce::AudioBuffer<float> ending(2,tail);
                    float* const ends[]={ending.getWritePointer(0),
                                         ending.getWritePointer(1)};
                    pitch.flush(ends,tail);
                    for(int offset=0;offset<tail && !obsolete(version);) {
                        if(ring_.bufferedFrames()>highWater) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(3));
                            continue;
                        }
                        const int n=std::min(1024,tail-offset);
                        const auto copied=ring_.write(ends[0]+offset,ends[1]+offset,
                                                      static_cast<std::size_t>(n));
                        if(copied!=static_cast<std::size_t>(n)) {
                            failed=true;break;
                        }
                        written+=copied;offset+=n;
                    }
                }
            }
            if(!obsolete(version)) {
                if(failed) {
                    ready_.store(false,std::memory_order_release);
                    wantedPlay_.store(false,std::memory_order_release);
                    error_.store(4,std::memory_order_release);
                    state_.store(State::error,std::memory_order_release);
                } else if(consumed>=readEnd()) {
                    length_.store(written,std::memory_order_release);
                    eof_.store(true,std::memory_order_release);
                    ready_.store(true,std::memory_order_release);
                }
            }
            resampled.releaseResources();
        }
    }
    PreparedStereoPcmRing ring_;
    PlayerWaveformIndex waveform_;
    juce::File waveformFile_; // worker thread only, across seek epochs
    std::atomic<double> requestedSeekSeconds_{0.0};
    std::atomic<double> cueInSeconds_{0.0},cueOutSeconds_{0.0};
    mutable std::mutex commandMutex_; // CONTROL/worker only, NEVER RT
    juce::File selectedFile_;
    std::atomic<bool> exit_{false},ready_{false},wantedPlay_{false},eof_{false};
    std::atomic<bool> hasFile_{false}; // lifecycle read: no control mutex
    std::atomic<std::uint64_t> generation_{0},played_{0},length_{0};
    std::atomic<std::uint64_t> mediaPlayed_{0},mediaDuration_{0};
    std::atomic<std::uint32_t> readers_{0};
    std::atomic<int> rate_{0},error_{0};
    // Packed atomic makes KEY+TEMPO a single coherent command, avoiding
    // a half-new pitch with half-old speed on concurrent UI/worker threads.
    std::atomic<std::uint32_t> requestedControls_{(100u<<8)|6u};
    std::atomic<std::uint32_t> appliedControls_{(100u<<8)|6u};
    std::atomic<State> state_{State::empty};
    armix::dsp::RealtimeSlew64 gain_; // AUDIO only
    std::uint64_t audioGeneration_=0; // AUDIO only, no race with file thread
    double mediaFraction_=0.; // AUDIO only, fractional original-media frame
    std::mutex wakeMutex_; // only decoding worker
    std::condition_variable wake_; // never accessed by audio callback
    // Construct LAST: workerLoop must never observe unconstructed atomics/
    // mutex/File, even if the OS schedules the thread immediately.
    std::thread worker_;
};
} // namespace armix::player
