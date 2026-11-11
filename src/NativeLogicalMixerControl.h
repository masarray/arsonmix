// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "NativeLogicalMixerWorkspace.h"
#include "NativeMixerSessionSaveWorker.h"

namespace armix::session {
// JUCE message thread. The accepted workspace is replaced only after the
// shared session worker has committed the complete document and FX state.
class NativeLogicalMixerControl final {
public:
    using Workspace=NativeLogicalMixerWorkspace;
    using Completion=NativeMixerSessionSaveWorker::Completion;
    using PrepareFx=std::function<juce::String(const Workspace&,mixer::ChannelId,
        std::size_t,StoredInsert&,std::unique_ptr<juce::AudioProcessor>&)>;
    using EmitAck=std::function<void(juce::var)>;
    struct Availability {
        bool hostBusy=false;
        bool logicalAuthority=false;
        bool vendorEditorOpen=false;
    };

    NativeLogicalMixerControl(juce::File studio,NativeMixerSessionSaveWorker& worker,
                              std::function<void(const juce::String&)> checkpointSaved)
        :studio_(std::move(studio)),worker_(worker),
         checkpointSaved_(std::move(checkpointSaved)) {}

    NativeLogicalMixerControl(const NativeLogicalMixerControl&)=delete;
    NativeLogicalMixerControl& operator=(const NativeLogicalMixerControl&)=delete;

    struct LoadResult {
        bool found=false;
        juce::String error;
    };

    LoadResult ensureLoaded() {
        if(loaded_)return {};
        loaded_=true;
        NativeMixerSessionStore store(studio_);
        if(!store.exists())return {};
        MixerSessionDocument document;
        std::vector<shows::BinaryState> states;
        auto error=store.load(document,states);
        if(error.isEmpty() && !workspace_.load(std::move(document),std::move(states)))
            error="Saved logical mixer routing is invalid or cyclic";
        readOnly_=error.isNotEmpty();
        return {true,std::move(error)};
    }

    bool loaded() const noexcept {return loaded_;}
    bool readOnly() const noexcept {return readOnly_;}
    const Workspace& workspace() const noexcept {return workspace_;}

    bool restoreCheckpoint(MixerSessionDocument doc,std::vector<shows::BinaryState> states) {
        if(!workspace_.load(std::move(doc),std::move(states)))return false;
        loaded_=true;
        readOnly_=false;
        return true;
    }

    bool open(MixerSessionDocument doc,std::vector<shows::BinaryState> states,
              Completion completion) {
        if(worker_.busy())return false;
        auto candidate=std::make_shared<Workspace>();
        if(!candidate->load(std::move(doc),std::move(states))) {
            completion("Saved logical routing is invalid or cyclic");
            return true;
        }
        return commit(std::move(candidate),std::move(completion));
    }

    bool exportTo(const juce::File& file,Completion completion) {
        return !readOnly_ && worker_.save(studio_,workspace_.document(),
            workspace_.states(),std::move(completion),file);
    }

    bool saveFx(StoredInsert insert,juce::MemoryBlock bytes,Completion completion) {
        if(readOnly_ || worker_.busy())return false;
        auto candidate=std::make_shared<Workspace>(workspace_);
        return candidate->saveFx(std::move(insert),std::move(bytes)) &&
            commit(std::move(candidate),std::move(completion));
    }

    // Authority is determined from the host's admitted/retained readback.
    // A logical document can never itself grant physical output admission.
    void handleIntent(const juce::var& request,Availability availability,
            const std::function<bool(const juce::String&)>& canInsertFx,
            const PrepareFx& prepareFx,const EmitAck& emit) {
        const auto requestId=request.getProperty("requestId",{}).toString();
        auto reply=[this,requestId,emit](const juce::String& error,
                                        const juce::String& createdId={}) {
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("status",error.isEmpty()?"ACCEPTED":"REJECTED");
            ack->setProperty("errorCode",error.isEmpty()?"NONE":error);
            ack->setProperty("revision",juce::String(std::to_string(workspace_.revision())));
            if(error.isEmpty())ack->setProperty("createdId",createdId);
            ack->setProperty("physicalOutputAdmitted",false);
            emit(juce::var(ack.release()));
        };
        if(!requestId.startsWith("logical-") || requestId.length()>80)
            return reply("INVALID_LOGICAL_REQUEST");
        if(requests_.contains(requestId))
            return reply("DUPLICATE_LOGICAL_REQUEST");
        if(readOnly_)return reply("SAVED_MIXER_RECOVERY_REQUIRED");
        if(availability.hostBusy || worker_.busy())return reply("LOGICAL_EDITOR_BUSY");
        if(!availability.logicalAuthority)return reply("PHYSICAL_PCM_AUTHORITY_ACTIVE");

        const auto operation=request.getProperty("operation","").toString();
        if(availability.vendorEditorOpen &&
           (operation=="REMOVE_FX_REFERENCE" || operation=="DELETE_CHANNEL" || operation=="REMOVE_BUS"))
            return reply("Close the vendor FX editor before removing channels or inserts");
        const bool addFx=operation=="ADD_FX_REFERENCE";
        if(addFx && !canInsertFx(request.getProperty("payload",{})
                .getProperty("catalogId","").toString()))
            return reply("UNKNOWN_OR_SCANNING_FX_CATALOG");

        auto candidate=std::make_shared<Workspace>(workspace_);
        const auto result=candidate->apply(request);
        if(!result.accepted)return reply(result.error);
        if(addFx) {
            std::uint64_t channel=0;
            if(!parseDecimal(request.getProperty("channelId",{}),channel))
                return reply("INVALID_LOGICAL_CHANNEL");
            const auto* row=workspace_.document().roster.find({channel});
            if(!row)return reply("INVALID_LOGICAL_CHANNEL");
            StoredInsert insert;
            std::unique_ptr<juce::AudioProcessor> processor;
            try {
                const auto error=prepareFx(*candidate,{channel},row->insertCatalogIds.size(),
                                          insert,processor);
                if(error.isNotEmpty())return reply(error);
                juce::MemoryBlock bytes;
                processor->getStateInformation(bytes);
                if(!candidate->saveFx(std::move(insert),std::move(bytes)))
                    return reply("INVALID_FX_STATE");
            } catch(...) {
                return reply("INVALID_FX_STATE");
            }
        }
        if(!commit(std::move(candidate),[reply,createdId=result.createdId](juce::String error){
            reply(error,createdId);
        }))return reply("LOGICAL_EDITOR_BUSY");
        requests_.add(requestId);
        if(requests_.size()>512)requests_.remove(0);
    }

    juce::var readback() const {
        auto logical=workspace_.snapshot();
        logical.getDynamicObject()->setProperty("readOnly",readOnly_);
        if(readOnly_) {
            logical.getProperty("graph",{}).getProperty("capabilities",{})
                .getDynamicObject()->setProperty("supportedOps",juce::var(juce::Array<juce::var>{}));
        }
        return logical;
    }

    bool matchesFxTarget(const juce::var& request,std::uint64_t& channel,std::size_t& slot) const {
        const auto rawSlot=request.getProperty("slot",{});
        const auto index=static_cast<int>(rawSlot);
        std::uint64_t revision=0;
        if(!rawSlot.isInt() || index<0 ||
           request.getProperty("deviceEpoch",{}).toString()!="0" ||
           request.getProperty("workspaceToken","").toString()!=workspace_.workspaceToken() ||
           !parseDecimal(request.getProperty("graphRevision",{}),revision) ||
           revision!=workspace_.revision() ||
           !parseDecimal(request.getProperty("channelId",{}),channel))
            return false;
        slot=static_cast<std::size_t>(index);
        const auto* row=workspace_.document().roster.find({channel});
        return row && slot<row->insertCatalogIds.size() &&
            request.getProperty("instanceId","").toString()==workspace_.fxToken({channel},slot);
    }

private:
    bool commit(std::shared_ptr<Workspace> candidate,Completion completion) {
        return worker_.save(studio_,candidate->document(),candidate->states(),
            [this,lifetime=std::weak_ptr<int>(lifetime_),candidate,
             completion=std::move(completion)](juce::String error) {
                if(lifetime.expired())return;
                if(error.isEmpty()) {
                    workspace_=std::move(*candidate);
                    loaded_=true;
                    readOnly_=false;
                    checkpointSaved_(workspace_.document().id);
                }
                completion(std::move(error));
            });
    }

    const juce::File studio_;
    NativeMixerSessionSaveWorker& worker_;
    std::function<void(const juce::String&)> checkpointSaved_;
    Workspace workspace_;
    juce::StringArray requests_;
    bool loaded_=false;
    bool readOnly_=false;
    std::shared_ptr<int> lifetime_=std::make_shared<int>(0);
};
} // namespace armix::session
