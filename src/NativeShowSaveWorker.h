// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeShowStore.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// Message-thread controller owns one long-lived IO worker. Read/list/commit
// are mutually exclusive. No callback, WebView render or third-party VST
// processor ever executes any file operation or JSON decode.
namespace armix::shows {
class NativeShowSaveWorker final : private juce::Thread {
public:
    enum class Operation { save, list, read };
    struct Summary {
        juce::String id,name;
        juce::int64 modifiedMs=0;
    };
    struct Result {
        Operation operation=Operation::save;
        juce::String showId, snapshotId, name, error;
        juce::int64 requestEpoch=0,requestRevision=0;
        std::vector<Summary> items;
        SnapshotV2 loaded;
        bool ok() const noexcept {return error.isEmpty();}
    };
    using Completion=std::function<void(Result)>;
    NativeShowSaveWorker() : juce::Thread("ArSonMix Native Show IO") {}
    ~NativeShowSaveWorker() override {
        alive_->store(false,std::memory_order_release);
        signalThreadShouldExit();
        stopThread(-1);
    }
    bool launch(juce::File studio,SnapshotV2 snapshot,Completion completion) {
        return launch(std::move(studio),std::move(snapshot),{},
                      std::move(completion));
    }
    bool launch(juce::File studio,SnapshotV2 snapshot,
                std::vector<BinaryState> binaryStates,Completion completion) {
        if(!reserve(Operation::save,std::move(studio),std::move(completion)) ||
           !detail::uuid(snapshot.id))return false;
        snapshot_=std::move(snapshot);
        blobs_=std::move(binaryStates);
        if(startThread())return true;
        return false;
    }
    bool list(juce::File studio,Completion completion) {
        if(!reserve(Operation::list,std::move(studio),std::move(completion)))
            return false;
        return startThread();
    }
    bool read(juce::File studio,juce::String showId,
              juce::int64 epoch,juce::int64 revision,Completion completion) {
        if(!detail::uuid(showId)||epoch<=0||revision<=0 ||
           !reserve(Operation::read,std::move(studio),std::move(completion)))
            return false;
        requestedId_=std::move(showId);
        requestEpoch_=epoch;requestRevision_=revision;
        return startThread();
    }
private:
    bool reserve(Operation mode,juce::File studio,Completion completion) {
        if(isThreadRunning()||!completion)return false;
        operation_=mode;studio_=std::move(studio);
        finished_=std::move(completion);
        return true;
    }
    void run() override {
        Result result;
        result.operation=operation_;
        result.requestEpoch=requestEpoch_;
        result.requestRevision=requestRevision_;
        try {
            NativeShowStore store(studio_);
            if(operation_==Operation::save)save(store,result);
            else if(operation_==Operation::list)listShows(result);
            else readShow(store,result);
        } catch(const std::exception& e) {
            result.error="Native Show IO exception: "+juce::String(e.what());
        } catch(...) {
            result.error="Unknown native Show IO exception; session unchanged";
        }
        auto alive=alive_;
        auto done=finished_;
        juce::MessageManager::callAsync(
            [alive,done=std::move(done),result=std::move(result)]() mutable {
                if(alive->load(std::memory_order_acquire)&&done)
                    done(std::move(result));
            });
    }
    void save(NativeShowStore& store,Result& result) {
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
        result.error=store.commit(show,{snapshot_},blobs_);
        if(result.error.isNotEmpty())return;
        ShowV2 verified;
        std::vector<SnapshotV2> snapshots;
        result.error=store.load(show.id,verified,snapshots);
        if(result.error.isEmpty() &&
           (verified.currentSnapshotId!=snapshot_.id ||
            snapshots.size()!=1 ||
            Codec::encode(snapshots[0])!=Codec::encode(snapshot_)))
            result.error="Saved native Show readback mismatch";
    }
    // Lightweight bounded metadata listing. Full data+SHA verification is
    // performed again by NativeShowStore::load before any recall can apply.
    void listShows(Result& result) {
        const auto root=studio_.getChildFile("Shows");
        if(!root.isDirectory())return;
        juce::DirectoryIterator iter(root,false,"*",juce::File::findDirectories);
        int inspected=0;
        while(iter.next()) {
            if(++inspected>256) {
                result.error="Too many Shows to browse safely (256 limit)";
                result.items.clear();return;
            }
            const auto folder=iter.getFile();
            const auto id=folder.getFileName();
            if(!detail::uuid(id))continue;
            const auto head=folder.getChildFile("HEAD");
            if(!head.existsAsFile()||head.getSize()>128)continue;
            const auto version=head.loadFileAsString().trim();
            if(!detail::uuid(version))continue;
            const auto meta=folder.getChildFile("versions")
                .getChildFile(version).getChildFile("show.json");
            if(!meta.existsAsFile()||meta.getSize()>maxJsonBytes)continue;
            ShowV2 show;
            if(!Codec::decode(meta.loadFileAsString(),show) ||
               !show.id.equalsIgnoreCase(id)||show.snapshotOrder.empty())continue;
            result.items.push_back({show.id,show.name,show.modifiedMs});
        }
        std::sort(result.items.begin(),result.items.end(),
            [](const Summary& a,const Summary& b) {
                if(a.modifiedMs!=b.modifiedMs)return a.modifiedMs>b.modifiedMs;
                return a.id<b.id;
            });
        if(result.items.size()>64)result.items.resize(64);
    }
    void readShow(NativeShowStore& store,Result& result) {
        result.showId=requestedId_;
        ShowV2 show;
        std::vector<SnapshotV2> saved;
        result.error=store.load(requestedId_,show,saved);
        if(result.error.isNotEmpty())return;
        if(show.currentSnapshotId.isEmpty()) {
            result.error="Saved Show has no current snapshot";return;
        }
        const auto at=std::find_if(saved.begin(),saved.end(),[&](const SnapshotV2& s) {
            return s.id.equalsIgnoreCase(show.currentSnapshotId);
        });
        if(at==saved.end()) {
            result.error="Saved Show current snapshot missing";return;
        }
        result.loaded=*at;
        result.snapshotId=at->id;
        result.name=show.name;
    }
    Operation operation_=Operation::save;
    juce::File studio_;
    SnapshotV2 snapshot_;
    std::vector<BinaryState> blobs_; // captured on CONTROL, hashed/written on IO worker
    juce::String requestedId_;
    juce::int64 requestEpoch_=0,requestRevision_=0;
    Completion finished_;
    std::shared_ptr<std::atomic<bool>> alive_=
        std::make_shared<std::atomic<bool>>(true);
};
} // namespace armix::shows
