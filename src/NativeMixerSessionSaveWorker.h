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
    void finishForShutdown(bool applySaveCompletion=false) {
        waitForThreadToExit(-1);
        ++serial_; // invalidate the queued message-thread completion
        busy_.store(false,std::memory_order_release);
        auto completion=std::move(done_);
        done_={};readDone_={};states_.clear();
        // Archive lifecycle commits may have published HEAD just before
        // shutdown. Never discard their authoritative workspace adoption.
        if((applySaveCompletion||liveControlsOnly_||archiveMode_!=NativeMixerSessionStore::ArchiveMode::none)&&completion)
            completion(saveResult_);
        archiveMode_=NativeMixerSessionStore::ArchiveMode::none;
        liveControlsOnly_=false;
    }
    bool save(juce::File studio,MixerSessionDocument document,
              std::vector<armix::shows::BinaryState> binary,
              Completion completion,juce::File exportFile={},NativeMixerSessionStore::ArchiveMode archiveMode=NativeMixerSessionStore::ArchiveMode::none,
              bool verifyExportArchive=false,bool liveControlsOnly=false) {
        if(busy_.exchange(true,std::memory_order_acq_rel))return false;
        studio_=std::move(studio);
        exportFile_=std::move(exportFile);
        archiveMode_=archiveMode;
        verifyExportArchive_=verifyExportArchive;
        liveControlsOnly_=liveControlsOnly;
        doc_=std::move(document);
        states_=std::move(binary);
        done_=std::move(completion);
        return start();
    }
    bool load(juce::File file,ReadCompletion completion) {
        if(busy_.exchange(true,std::memory_order_acq_rel))return false;
        archiveMode_=NativeMixerSessionStore::ArchiveMode::none;
        verifyExportArchive_=false;
        liveControlsOnly_=false;
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
            else if(liveControlsOnly_) {
                if(exportFile_!=juce::File{}||archiveMode_!=NativeMixerSessionStore::ArchiveMode::none||!states_.empty())
                    error="Invalid live checkpoint worker request";
                else error=store.commitLiveControls(std::move(doc_));
            }
            else if(verifyExportArchive_) {
                if(exportFile_==juce::File{} || exportFile_.exists() ||
                   exportFile_.getParentDirectory().createDirectory().failed())
                    error="Cannot create a unique session archive";
                else {
                    error=NativeMixerSessionStore::writeFile(exportFile_,doc_,states_);
                    if(error.isEmpty()) {
                        MixerSessionDocument verified;
                        std::vector<armix::shows::BinaryState> verifiedStates;
                        error=NativeMixerSessionStore::readFile(exportFile_,verified,verifiedStates);
                        doc_.resumeMain.reset();
                        if(error.isEmpty() && Codec::encode(doc_)!=Codec::encode(verified))
                            error="Session archive readback mismatch";
                    }
                }
            }
            else error=archiveMode_!=NativeMixerSessionStore::ArchiveMode::none?
                store.commitNewSession(doc_,states_,archiveMode_):
                exportFile_==juce::File{}?store.commit(doc_,states_):
                NativeMixerSessionStore::writeFile(exportFile_,doc_,states_);
        }catch(...) {error="Mixer session IO worker exception";}
        // Release potentially large VST3 binary buffers before callback.
        if(!readDone_) {
            saveResult_=error;
            states_.clear();
        }
        auto alive=alive_;
        const auto serial=serial_;
        juce::MessageManager::callAsync([this,alive,error,serial] {
            if(alive->load(std::memory_order_acquire))completeOnControl(error,serial);
        });
    }

    void completeOnControl(juce::String error,unsigned serial) {
        if(serial!=serial_)return;
        // Open chains a file read into a durable HEAD commit on this worker.
        // The message can arrive just before JUCE retires the thread handle.
        if(isThreadRunning()) {
            juce::Timer::callAfterDelay(1,[this,alive=alive_,error,serial] {
                if(alive->load(std::memory_order_acquire))completeOnControl(error,serial);
            });
            return;
        }
        busy_.store(false,std::memory_order_release);
        auto callback=std::move(done_);
        done_={};
        auto readCallback=std::move(readDone_);
        readDone_={};
        archiveMode_=NativeMixerSessionStore::ArchiveMode::none;
        liveControlsOnly_=false;
        if(readCallback)readCallback(std::move(error),std::move(doc_),std::move(states_));
        else if(callback)callback(std::move(error));
    }
    juce::File studio_,exportFile_;
    MixerSessionDocument doc_;
    std::vector<armix::shows::BinaryState> states_;
    juce::String saveResult_;
    Completion done_;
    ReadCompletion readDone_;
    NativeMixerSessionStore::ArchiveMode archiveMode_=NativeMixerSessionStore::ArchiveMode::none;
    bool verifyExportArchive_=false,liveControlsOnly_=false;
    unsigned serial_=0;
    std::atomic<bool> busy_{false};
    std::shared_ptr<std::atomic<bool>> alive_=
        std::make_shared<std::atomic<bool>>(true);
};
} // namespace armix::session
