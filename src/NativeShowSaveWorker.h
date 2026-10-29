// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeShowStore.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

// One opt-in DRY Show per commit, bounded by the existing transactional store.
// All disk I/O and SHA verification run outside the JUCE message/audio threads.
// The worker's native copy cannot observe subsequent live routing mutations.
namespace armix::shows {
class NativeShowSaveWorker final : private juce::Thread {
public:
    struct Result {
        juce::String showId, snapshotId, name, error;
        bool ok() const noexcept {return error.isEmpty();}
    };
    using Completion=std::function<void(Result)>;
    NativeShowSaveWorker() : juce::Thread("ArSonMix Native Show Save") {}
    ~NativeShowSaveWorker() override {
        alive_->store(false,std::memory_order_release);
        signalThreadShouldExit();
        stopThread(-1);
    }
    bool launch(juce::File studio,SnapshotV2 snapshot,Completion completion) {
        if(isThreadRunning() || !completion || !detail::uuid(snapshot.id))
            return false;
        studio_=std::move(studio);
        snapshot_=std::move(snapshot);
        finished_=std::move(completion);
        return startThread();
    }
private:
    void run() override {
        Result result;
        result.snapshotId=snapshot_.id;
        result.name=snapshot_.name;
        ShowV2 show;
        show.id=juce::Uuid().toDashedString().toLowerCase();
        show.name=snapshot_.name;
        show.createdMs=snapshot_.createdMs;
        show.modifiedMs=snapshot_.modifiedMs;
        show.currentSnapshotId=snapshot_.id;
        show.snapshotOrder.push_back(snapshot_.id);
        result.showId=show.id;
        try {
            NativeShowStore store(studio_);
            result.error=store.commit(show,{snapshot_},{});
            if(result.error.isEmpty()) {
                ShowV2 reread;
                std::vector<SnapshotV2> snapshots;
                result.error=store.load(show.id,reread,snapshots);
                if(result.error.isEmpty() &&
                   (reread.currentSnapshotId!=snapshot_.id ||
                    snapshots.size()!=1 ||
                    Codec::encode(snapshots[0])!=Codec::encode(snapshot_)))
                    result.error="Saved DRY Show verification mismatch";
            }
        } catch(const std::exception& e) {
            result.error="Native Show IO exception: "+juce::String(e.what());
        } catch(...) {
            result.error="Unknown native Show IO exception; existing Shows untouched";
        }
        auto alive=alive_;
        auto done=finished_;
        juce::MessageManager::callAsync(
            [alive,done=std::move(done),result=std::move(result)]() mutable {
                if(alive->load(std::memory_order_acquire)&&done)
                    done(std::move(result));
            });
    }
    juce::File studio_;
    SnapshotV2 snapshot_;
    Completion finished_;
    std::shared_ptr<std::atomic<bool>> alive_=
        std::make_shared<std::atomic<bool>>(true);
};
} // namespace armix::shows
