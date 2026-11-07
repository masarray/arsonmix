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
    using ReadCompletion=std::function<void(juce::String,MixerSessionDocument,
        std::vector<armix::shows::BinaryState>)>;
    NativeMixerSessionSaveWorker():juce::Thread("ArSonMix MixerSession IO"){}
    ~NativeMixerSessionSaveWorker() override {
        alive_->store(false,std::memory_order_release);
        signalThreadShouldExit();
        stopThread(-1);
    }
    bool busy() const noexcept {return busy_.load(std::memory_order_acquire);}
    void finishForShutdown() {
        waitForThreadToExit(-1);
        busy_.store(false,std::memory_order_release);
        done_={};readDone_={};states_.clear();
    }
    bool save(juce::File studio,MixerSessionDocument document,
              std::vector<armix::shows::BinaryState> binary,
              Completion completion,juce::File exportFile={}) {
        if(busy_.exchange(true,std::memory_order_acq_rel))return false;
        studio_=std::move(studio);
        exportFile_=std::move(exportFile);
        doc_=std::move(document);
        states_=std::move(binary);
        done_=std::move(completion);
        return start();
    }
    bool load(juce::File file,ReadCompletion completion) {
        if(busy_.exchange(true,std::memory_order_acq_rel))return false;
        exportFile_=std::move(file);
        readDone_=std::move(completion);
        return start();
    }
private:
    bool start() {
        ++serial_;
        if(!startThread()){
            busy_.store(false,std::memory_order_release);
            done_={};readDone_={};states_.clear();
            return false;
        }
        return true;
    }
    void run() override {
        juce::String error;
        try {
            NativeMixerSessionStore store(studio_);
            if(readDone_)error=NativeMixerSessionStore::readFile(exportFile_,doc_,states_);
            else error=exportFile_==juce::File{}?store.commit(doc_,states_):
                NativeMixerSessionStore::writeFile(exportFile_,doc_,states_);
        }catch(...) {error="Mixer session IO worker exception";}
        // Release potentially large VST3 binary buffers before callback.
        if(!readDone_)states_.clear();
        auto alive=alive_;
        const auto serial=serial_;
        juce::MessageManager::callAsync([this,alive,error,serial]() mutable {
            if(!alive->load(std::memory_order_acquire)||serial!=serial_)return;
            busy_.store(false,std::memory_order_release);
            auto callback=std::move(done_);
            done_={};
            auto readCallback=std::move(readDone_);
            readDone_={};
            if(readCallback)readCallback(std::move(error),std::move(doc_),std::move(states_));
            else if(callback)callback(std::move(error));
        });
    }
    juce::File studio_,exportFile_;
    MixerSessionDocument doc_;
    std::vector<armix::shows::BinaryState> states_;
    Completion done_;
    ReadCompletion readDone_;
    unsigned serial_=0;
    std::atomic<bool> busy_{false};
    std::shared_ptr<std::atomic<bool>> alive_=
        std::make_shared<std::atomic<bool>>(true);
};
} // namespace armix::session
