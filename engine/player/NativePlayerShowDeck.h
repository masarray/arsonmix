// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativePlayerDeck.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Two independent decoder workers, ONE native PLAYER A PCM mixer source.
// Only this wrapper blends the two preprepared streams; neither worker owns
// audio hardware, channels, FX or the JUCE callback.  Neither seek/reload nor
// disk reads happen inside pull().  The retired deck is stopped by service()
// on CONTROL, never by the realtime thread.
namespace armix::player {
class NativePlayerShowDeck final {
public:
    using Status=NativePlayerDeck::Status;
    using Controls=PlayerPitchTempoProcessor::Controls;
    static constexpr int maxRenderFrames=8192;
    static constexpr int fadeTableResolution=1024;

    NativePlayerShowDeck() {
        constexpr double pi=3.14159265358979323846;
        for(int i=0;i<=fadeTableResolution;++i) {
            const double phase=(pi/2)*i/fadeTableResolution;
            fadeOut_[static_cast<std::size_t>(i)]=
                static_cast<float>(std::cos(phase));
            fadeIn_[static_cast<std::size_t>(i)]=
                static_cast<float>(std::sin(phase));
        }
    }
    NativePlayerShowDeck(const NativePlayerShowDeck&)=delete;
    NativePlayerShowDeck& operator=(const NativePlayerShowDeck&)=delete;

    void deviceRate(double hz) noexcept {
        // Device generations invalidate BOTH cached PCM rings. This is
        // not permission to switch outputs or restart playback.
        fading_.store(false,std::memory_order_release);
        retired_.store(-1,std::memory_order_release);
        rate_=hz;
        pending_=false;preloadedFile_={};preloadedControls_={};
        for(auto& deck:decks_)deck.deviceRate(hz);
    }
    void load(const juce::File& file,bool autoPlay=true) {
        // Explicit new-song load for idle transport, not live NEXT:
        // live transitions always go through queueTransition().
        fading_.store(false,std::memory_order_release);
        pending_=false;preloadedFile_={};
        const int index=active_.load(std::memory_order_acquire);
        decks_[static_cast<std::size_t>(1-index)].stop();
        decks_[static_cast<std::size_t>(index)].load(file,autoPlay);
        retired_.store(-1,std::memory_order_release);
    }
    bool setPitchTempo(int key,int tempo) noexcept {
        return decks_[static_cast<std::size_t>(
            active_.load(std::memory_order_acquire))]
            .setPitchTempo(key,tempo);
    }
    bool seekSeconds(double seconds) noexcept {
        if(fading_.load(std::memory_order_acquire)||pending_)return false;
        return decks_[static_cast<std::size_t>(
            active_.load(std::memory_order_acquire))].seekSeconds(seconds);
    }
    bool isPlayingRequested() const noexcept {
        if(fading_.load(std::memory_order_acquire))return true;
        return decks_[static_cast<std::size_t>(
            active_.load(std::memory_order_acquire))].isPlayingRequested();
    }
    void play() noexcept {
        decks_[static_cast<std::size_t>(
            active_.load(std::memory_order_acquire))].play();
    }
    void pause() noexcept {
        pending_=false;
        for(auto& deck:decks_)deck.pause();
    }
    void stop() noexcept {
        // STOP supersedes all queued transitions and no stale PCM may play
        // after an output/device restart.
        pending_=false;preloadedFile_={};
        fading_.store(false,std::memory_order_release);
        for(auto& deck:decks_)deck.stop();
    }
    Status snapshot() const {
        return decks_[static_cast<std::size_t>(
            active_.load(std::memory_order_acquire))].snapshot();
    }
    bool transitionPending() const noexcept {return pending_;}
    bool transitionActive() const noexcept {
        return fading_.load(std::memory_order_acquire);
    }
    bool preload(const juce::File& file,int key,int tempo) {
        if(file==juce::File{} || !file.existsAsFile() ||
           !PlayerPitchTempoProcessor::valid({key,tempo}) ||
           fading_.load(std::memory_order_acquire))return false;
        const int inactive=1-active_.load(std::memory_order_acquire);
        if(preloadedFile_==file &&
           preloadedControls_==Controls{key,tempo})return true;
        // Reuse a bounded standby decoder and SPSC FIFO. Repeated NEXT
        // changes supersede old read work through NativePlayerDeck generation.
        auto& standby=decks_[static_cast<std::size_t>(inactive)];
        if(!standby.setPitchTempo(key,tempo))return false;
        preloadedFile_=file;preloadedControls_={key,tempo};
        standby.load(file,false);
        return true;
    }
    bool queueTransition(const juce::File& file,int key,int tempo,
                         int fadeMilliseconds=180) {
        if(fading_.load(std::memory_order_acquire))return false;
        if(!preload(file,key,tempo))return false;
        fadeMilliseconds_=std::clamp(fadeMilliseconds,0,3000);
        pending_=true;
        (void)service();
        return true;
    }
    // CONTROL/TIMER only. Returns true only when a NEW crossfade is
    // atomically armed after genuine standby PCM is buffered.
    bool service() {
        const int old=retired_.exchange(-1,std::memory_order_acq_rel);
        if(old>=0)decks_[static_cast<std::size_t>(old)].stop();
        if(!pending_||fading_.load(std::memory_order_acquire))return false;
        const int from=active_.load(std::memory_order_acquire);
        const int to=1-from;
        const auto standby=decks_[static_cast<std::size_t>(to)].snapshot();
        if(standby.state==NativePlayerDeck::State::error) {
            pending_=false;preloadedFile_={};
            return false;
        }
        if(!standby.outputReady)return false;
        // All transport control remains outside the realtime callback.
        decks_[static_cast<std::size_t>(to)].play();
        fadeFrom_.store(from,std::memory_order_relaxed);
        fadeTo_.store(to,std::memory_order_relaxed);
        fadePosition_.store(0,std::memory_order_relaxed);
        fadeFrames_.store(std::max(1,static_cast<int>(
            std::lround(rate_*fadeMilliseconds_/1000.))),
            std::memory_order_relaxed);
        fading_.store(true,std::memory_order_release);
        active_.store(to,std::memory_order_release);
        pending_=false;preloadedFile_={};
        return true;
    }
    void pull(float* left,float* right,int frames) noexcept {
        if(!left||!right||left==right||frames<=0)return;
        if(frames>maxRenderFrames) {
            std::fill_n(left,frames,0.f);std::fill_n(right,frames,0.f);
            return;
        }
        if(!fading_.load(std::memory_order_acquire)) {
            decks_[static_cast<std::size_t>(
                active_.load(std::memory_order_acquire))]
                .pull(left,right,frames);
            return;
        }
        const int from=fadeFrom_.load(std::memory_order_relaxed);
        const int to=fadeTo_.load(std::memory_order_relaxed);
        decks_[static_cast<std::size_t>(to)].pull(left,right,frames);
        decks_[static_cast<std::size_t>(from)].pull(
            scratch_[0].data(),scratch_[1].data(),frames);
        const int total=fadeFrames_.load(std::memory_order_relaxed);
        const int start=fadePosition_.load(std::memory_order_relaxed);
        for(int i=0;i<frames;++i) {
            const auto index=std::clamp(static_cast<int>(
                (static_cast<std::int64_t>(start+i)*fadeTableResolution)/
                std::max(1,total)),0,fadeTableResolution);
            const auto oldGain=fadeOut_[static_cast<std::size_t>(index)];
            const auto newGain=fadeIn_[static_cast<std::size_t>(index)];
            left[i]=scratch_[0][static_cast<std::size_t>(i)]*oldGain
                    +left[i]*newGain;
            right[i]=scratch_[1][static_cast<std::size_t>(i)]*oldGain
                     +right[i]*newGain;
        }
        const int advanced=start+frames;
        if(advanced>=total) {
            fadePosition_.store(total,std::memory_order_relaxed);
            fading_.store(false,std::memory_order_release);
            retired_.store(from,std::memory_order_release);
        } else fadePosition_.store(advanced,std::memory_order_relaxed);
    }
private:
    std::array<NativePlayerDeck,2> decks_;
    std::array<std::array<float,maxRenderFrames>,2> scratch_{};
    std::array<float,fadeTableResolution+1> fadeOut_{},fadeIn_{};
    std::atomic<int> active_{0},fadeFrom_{0},fadeTo_{1};
    std::atomic<int> fadePosition_{0},fadeFrames_{1},retired_{-1};
    std::atomic<bool> fading_{false};
    // CONTROL-only, not borrowed by the realtime callback.
    juce::File preloadedFile_;
    Controls preloadedControls_{};
    double rate_=0.;
    int fadeMilliseconds_=180;
    bool pending_=false;
};
} // namespace armix::player
