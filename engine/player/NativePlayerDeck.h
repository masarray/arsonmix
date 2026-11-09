// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PreparedStereoPcmRing.h"
#include "../core/RealtimeSlew64.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
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
        bool outputReady=false;
        juce::String title,error;
        double deviceRate=0;
    };
    NativePlayerDeck() {
        // 2.7 seconds at 48k, bounded independent of song length.
        if(!ring_.prepare(131072))state_.store(State::error);
        worker_=std::thread([this] { workerLoop(); });
    }
    ~NativePlayerDeck() {
        exit_.store(true,std::memory_order_release);
        if(worker_.joinable())worker_.join(); // only CONTROL after callback detach
    }
    NativePlayerDeck(const NativePlayerDeck&)=delete;
    NativePlayerDeck& operator=(const NativePlayerDeck&)=delete;

    // CONTROL only; the audio device callback may concurrently run for
    // earlier generation. Worker atomically invalidates that generation.
    void deviceRate(double hz) noexcept {
        const auto rounded=static_cast<int>(std::lround(hz));
        if(rounded==rate_.load(std::memory_order_acquire))return;
        ready_.store(false,std::memory_order_release);
        rate_.store(rounded>0?rounded:0,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        state_.store(rounded>0?State::loading:State::stopped,
                     std::memory_order_release);
    }
    void load(const juce::File& file,bool autoPlay=true) {
        ready_.store(false,std::memory_order_release);
        wantedPlay_.store(autoPlay,std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(commandMutex_);
            selectedFile_=file; // worker owns the reader, not the callback
        }
        played_.store(0,std::memory_order_release);
        error_.store(0,std::memory_order_release);
        state_.store(State::loading,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
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
        state_.store(State::stopped,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
        played_.store(0,std::memory_order_relaxed);
    }
    Status snapshot() const {
        Status s;
        s.state=state_.load(std::memory_order_acquire);
        s.played=played_.load(std::memory_order_relaxed);
        s.duration=length_.load(std::memory_order_relaxed);
        s.underruns=ring_.underrunFrames();
        s.generation=generation_.load(std::memory_order_acquire);
        s.deviceRate=rate_.load(std::memory_order_acquire);
        s.outputReady=ready_.load(std::memory_order_acquire);
        switch(error_.load(std::memory_order_relaxed)) {
            case 1:s.error="File missing or unreadable";break;
            case 2:s.error="Unsupported or corrupt music format";break;
            case 3:s.error="Audio output not configured";break;
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
        const auto n=ring_.read(left,right,static_cast<std::size_t>(frames));
        if(n>0) {
            played_.fetch_add(n,std::memory_order_relaxed);
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
    void restart() noexcept {
        ready_.store(false,std::memory_order_release);
        played_.store(0,std::memory_order_relaxed);
        state_.store(State::loading,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_acq_rel);
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
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
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
            const bool mono=reader->numChannels==1;
            const double ratio=reader->sampleRate/static_cast<double>(deviceHz);
            const auto total=static_cast<std::uint64_t>(std::ceil(
                static_cast<double>(reader->lengthInSamples)/ratio));
            if(!total) {
                state_.store(State::error,std::memory_order_release);
                continue;
            }
            juce::AudioFormatReaderSource source(reader.release(),true);
            juce::ResamplingAudioSource resampled(&source,false,2);
            resampled.setResamplingRatio(ratio);
            resampled.prepareToPlay(1024,static_cast<double>(deviceHz));
            juce::AudioBuffer<float> staging(2,1024);
            length_.store(total,std::memory_order_release);
            std::uint64_t written=0;
            while(!obsolete(version) && written<total) {
                if(ring_.bufferedFrames()>ring_.capacityFrames()-2048) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                    continue;
                }
                const int n=static_cast<int>(std::min<std::uint64_t>(1024,total-written));
                staging.clear();
                resampled.getNextAudioBlock({&staging,0,n});
                if(mono)staging.copyFrom(1,0,staging,0,0,n);
                if(obsolete(version))break;
                const auto transferred=ring_.write(
                    staging.getReadPointer(0),staging.getReadPointer(1),
                    static_cast<std::size_t>(n));
                written+=transferred;
                if(!ready_.load(std::memory_order_relaxed) &&
                   (ring_.bufferedFrames()>=4096 || written==total)) {
                    ready_.store(true,std::memory_order_release);
                    if(!wantedPlay_.load(std::memory_order_acquire) &&
                       state_.load(std::memory_order_relaxed)!=State::stopped)
                        state_.store(State::ready,std::memory_order_release);
                }
            }
            if(!obsolete(version) && written==total) {
                eof_.store(true,std::memory_order_release);
                ready_.store(true,std::memory_order_release);
            }
            resampled.releaseResources();
        }
    }
    PreparedStereoPcmRing ring_;
    mutable std::mutex commandMutex_; // CONTROL/worker only, NEVER RT
    juce::File selectedFile_;
    std::atomic<bool> exit_{false},ready_{false},wantedPlay_{false},eof_{false};
    std::atomic<std::uint64_t> generation_{0},played_{0},length_{0};
    std::atomic<std::uint32_t> readers_{0};
    std::atomic<int> rate_{0},error_{0};
    std::atomic<State> state_{State::empty};
    armix::dsp::RealtimeSlew64 gain_; // AUDIO only
    // Construct LAST: workerLoop must never observe unconstructed atomics/
    // mutex/File, even if the OS schedules the thread immediately.
    std::thread worker_;
};
} // namespace armix::player
