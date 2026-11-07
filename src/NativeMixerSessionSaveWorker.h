// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionStore.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// Snapshot ownership moves from JUCE CONTROL to this single IO thread.
// No file IO or SHA-256 in the realtime callback. An immutable generation is
// promoted only after all bytes verify, with a message-thread completion.
namespace armix::session {
class NativeMixerSessionSaveWorker final : private juce::Thread {
public:
    using Completion=std::function<void(juce::String)>;
    NativeMixerSessionSaveWorker():juce::Thread("ArSonMix MixerSession IO"){}
    ~NativeMixerSessionSaveWorker() override {
        alive_->store(false,std::memory_order_release);
        signalThreadShouldExit();
        stopThread(-1);
    }
    bool busy() const noexcept {return busy_.load(std::memory_order_acquire);}
    bool save(juce::File studio,MixerSessionDocument document,
              std::vector<armix::shows::BinaryState> binary,
              Completion completion) {
        if(busy_.exchange(true,std::memory_order_acq_rel))return false;
        studio_=std::move(studio);
        doc_=std::move(document);
        states_=std::move(binary);
        done_=std::move(completion);
        if(!startThread()){
            busy_.store(false,std::memory_order_release);
            done_={};states_.clear();
            return false;
        }
        return true;
    }
private:
    void run() override {
        juce::String error;
        try {
            NativeMixerSessionStore store(studio_);
            error=store.commit(doc_,states_);
        }catch(...) {error="Mixer session IO worker exception";}
        // Release potentially large VST3 binary buffers before callback.
        states_.clear();
        auto alive=alive_;
        juce::MessageManager::callAsync([this,alive,error]() mutable {
            if(!alive->load(std::memory_order_acquire))return;
            busy_.store(false,std::memory_order_release);
            auto callback=std::move(done_);
            done_={};
            if(callback)callback(std::move(error));
        });
    }
    juce::File studio_;
    MixerSessionDocument doc_;
    std::vector<armix::shows::BinaryState> states_;
    Completion done_;
    std::atomic<bool> busy_{false};
    std::shared_ptr<std::atomic<bool>> alive_=
        std::make_shared<std::atomic<bool>>(true);
};
} // namespace armix::session
