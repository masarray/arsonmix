// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include "LiveChannelFxGraph.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// M5 universal ChannelId/VST3 ownership bridge.
//
// Unlike the fixed ten-strip index, this immutable CONTROL-PREPARED table
// follows stable ChannelId across +CHANNEL, +GROUP, FX Return and reorder.
// It holds the exact M4 vendor FxInstance owner leases (binary state,
// parameters, bypass envelope, reverb tail). The audio callback traverses
// RAW pointers only: no shared_ptr refcount, heap, lock, VST instantiation,
// string compare or plugin lookup by catalog name.
//
// The bank must be owned by the SAME published/retired native graph snapshot
// as the PCM schedule. Do not discard it while a callback can render from it.
// Never render a former legacy graph concurrently with its migrated instances.
namespace armix::mixer {
class DynamicChannelFxLeaseBank final {
public:
    using FxGraph=LiveChannelFxGraph;
    using Lease=FxGraph::InstanceLease;
    static constexpr int maxInserts=FxGraph::maxInsertsPerStrip;

    struct Assignment final {
        ChannelId id{};
        ProcessingFormat format=ProcessingFormat::stereo;
        std::vector<Lease> inserts; // CONTROL only; no creation on callback
        // Reused over revision changes. This flag overrides the effective
        // output without changing the individual FxInstance bypass flags.
        std::shared_ptr<std::atomic<bool>> rackBypassed;
    };

    // Occupied CH1 vendor/Orbit owner remains alive with accepted PCM.
    // A callback reads only a raw, prevalidated pointer; no refcount/heap.
    struct ExternalLease final {
        ChannelId id{};
        std::shared_ptr<void> owner;
        void* context=nullptr;
        void (*render)(void*,float*,float*,int) noexcept=nullptr;
        bool populated() const noexcept {return id.value||owner||context||render;}
        bool valid() const noexcept {
            return id.value && owner && context==owner.get() && render;
        }
    };
    enum class Fault : std::uint8_t {
        none,invalidEpoch,emptyId,duplicateChannel,tooManyInserts,
        emptyLease,duplicateInstance,invalidBusLayout,uncompensatedLatency,
        missingLegacyIdentity,invalidExternalLease,invalidInsertPosition,staleInstance
    };
    DynamicChannelFxLeaseBank()=default;
    DynamicChannelFxLeaseBank(const DynamicChannelFxLeaseBank&)=delete;
    DynamicChannelFxLeaseBank& operator=(const DynamicChannelFxLeaseBank&)=delete;
    DynamicChannelFxLeaseBank(DynamicChannelFxLeaseBank&&)=default;
    DynamicChannelFxLeaseBank& operator=(DynamicChannelFxLeaseBank&&)=default;

    // Used by message-thread M4->M5 migration. There is no fallback by
    // slot position or catalog ID: an actual opaque M4 instance token is
    // required, so a Show/reorder cannot accidentally acquire another plugin.
    static bool captureLegacy(int lane,ChannelId nativeId,
                              ProcessingFormat format,
                              const FxGraph& prior,
                              Assignment& destination) {
        if(lane<0||lane>=FxGraph::fxSlots||!nativeId.value)return false;
        Assignment staged;
        staged.id=nativeId;staged.format=format;
        for(int slot=0;slot<prior.insertsOn(lane);++slot){
            const auto& token=prior.insertInstanceId(lane,slot);
            if(token.isEmpty())return false;
            auto owner=prior.leaseInsert(lane,slot,token);
            if(!owner||owner->instanceId!=token)return false;
            staged.inserts.push_back(std::move(owner));
        }
        destination=std::move(staged);
        return true;
    }

    static Fault prepare(std::uint64_t epoch,std::uint64_t revision,
                         std::span<const Assignment> bindings,
                         DynamicChannelFxLeaseBank& destination) {
        return prepare(epoch,revision,bindings,destination,ExternalLease{});
    }
    static Fault prepare(std::uint64_t epoch,std::uint64_t revision,
                         std::span<const Assignment> bindings,
                         DynamicChannelFxLeaseBank& destination,
                         ExternalLease external) {
        if(external.populated() && !external.valid())
            return Fault::invalidExternalLease;
        if(!epoch||!revision)return Fault::invalidEpoch;
        DynamicChannelFxLeaseBank candidate;
        candidate.epoch_=epoch;candidate.revision_=revision;
        candidate.rows_.reserve(bindings.size());
        for(const auto& b:bindings) {
            if(!b.id.value)return Fault::emptyId;
            if(b.inserts.size()>static_cast<std::size_t>(maxInserts))
                return Fault::tooManyInserts;
            if(b.format!=ProcessingFormat::mono &&
               b.format!=ProcessingFormat::stereo)return Fault::invalidBusLayout;
            Row row;row.id=b.id;row.format=b.format;
            row.rackBypassed=b.rackBypassed?b.rackBypassed:
                std::make_shared<std::atomic<bool>>(false); // CONTROL allocation
            row.count=static_cast<std::uint8_t>(b.inserts.size());
            for(std::size_t i=0;i<b.inserts.size();++i) {
                const auto& owner=b.inserts[i];
                if(!owner||!owner->processor)return Fault::emptyLease;
                // M4's preparation verifies a stereo processor layout for
                // both mono-as-dual and stereo strips. Other plugin layouts
                // must be negotiated explicitly before a future PCM cutover.
                if(owner->processor->getTotalNumInputChannels()!=2 ||
                   owner->processor->getTotalNumOutputChannels()!=2)
                    return Fault::invalidBusLayout;
                // Latency is captured once at prepared instance creation.
                // The publisher builds graph-aware fan-in delay from it.
                if(owner->latencySamples<0 ||
                   owner->latencySamples>armix::dsp::StereoLatencyDelay::maximumSamples ||
                   owner->latencyChanged.load(std::memory_order_acquire))
                    return Fault::uncompensatedLatency;
                for(const auto& existing:candidate.rows_)
                    for(std::size_t k=0;k<existing.count;++k)
                        if(existing.owners[k].get()==owner.get())
                            return Fault::duplicateInstance;
                for(std::size_t k=0;k<i;++k)
                    if(row.owners[k].get()==owner.get())
                        return Fault::duplicateInstance;
                row.owners[i]=owner; // owner reference acquired OFF realtime
                row.raw[i]=owner.get();
            }
            candidate.rows_.push_back(std::move(row));
        }
        std::sort(candidate.rows_.begin(),candidate.rows_.end(),
                  [](const Row& a,const Row& b){return a.id.value<b.id.value;});
        for(std::size_t i=1;i<candidate.rows_.size();++i)
            if(candidate.rows_[i-1].id==candidate.rows_[i].id)
                return Fault::duplicateChannel;
        if(external.populated()) {
            const auto* row=candidate.find(external.id);
            // Identical to M4: no native rack may double-render the CH1 FX.
            if(!row || row->count!=0)return Fault::invalidExternalLease;
            candidate.external_=std::move(external);
        }
        destination=std::move(candidate); // atomic control-thread acceptance
        return Fault::none;
    }

    int latencySamples(ChannelId id) const noexcept {
        const auto* row=find(id);int total=0;
        if(row)for(std::size_t i=0;i<row->count;++i) {
            const int n=row->raw[i]->latencySamples;
            if(n>armix::dsp::StereoLatencyDelay::maximumSamples-total)return -1;
            total+=n;
        }
        return total;
    }
    std::uint64_t epoch() const noexcept{return epoch_;}
    std::uint64_t revision() const noexcept{return revision_;}
    std::size_t lanes() const noexcept{return rows_.size();}
    ChannelId externalChannel() const noexcept{return external_.id;}
    std::size_t insertCount(ChannelId id) const noexcept {
        const auto* p=find(id);return p?p->count:0;
    }
    const Lease* ownerForControl(ChannelId id,std::size_t slot) const noexcept {
        const auto* p=find(id);return p&&slot<p->count?&p->owners[slot]:nullptr;
    }

    // E2: stable ChannelId VST3 control plane. Requests are serialized by
    // the native CONTROL writer, never the audio callback. A stale epoch,
    // graph or opaque instance token cannot bypass somebody else's plugin.
    // FxInstance owns the existing 8-ms realtime wet/dry envelope and vendor
    // processBlock. No graph swap, device restart, VST creation or RT heap.
    bool setInsertBypass(std::uint64_t expectedEpoch,
                         std::uint64_t expectedRevision,ChannelId id,
                         std::size_t slot,const juce::String& instanceToken,
                         bool bypass) const noexcept {
        if(expectedEpoch!=epoch_ || expectedRevision!=revision_ ||
           !id.value || instanceToken.isEmpty())return false;
        const auto* owner=ownerForControl(id,slot);
        if(!owner || !*owner || !(*owner)->processor ||
           (*owner)->instanceId!=instanceToken)return false;
        (*owner)->bypassRequested.store(bypass,std::memory_order_release);
        return true;
    }
    bool setRackBypass(std::uint64_t expectedEpoch,
                       std::uint64_t expectedRevision,ChannelId id,
                       bool bypass) const noexcept {
        if(expectedEpoch!=epoch_||expectedRevision!=revision_||!id.value)
            return false;
        const auto* row=find(id);
        if(!row||!row->rackBypassed)return false;
        row->rackBypassed->store(bypass,std::memory_order_release);
        return true;
    }
    bool rackBypassed(ChannelId id) const noexcept {
        const auto* row=find(id);
        return row&&row->rackBypassed &&
            row->rackBypassed->load(std::memory_order_acquire);
    }
    // The actual prepared vendor instance is also the native editor owner;
    // never synthesize a WebView editor or use a catalog name as identity.
    Lease leaseEditor(std::uint64_t expectedEpoch,
                      std::uint64_t expectedRevision,ChannelId id,
                      std::size_t slot,
                      const juce::String& instanceToken) const noexcept {
        if(expectedEpoch!=epoch_ || expectedRevision!=revision_ ||
           !id.value || instanceToken.isEmpty())return {};
        const auto* owner=ownerForControl(id,slot);
        return owner && *owner && (*owner)->processor &&
               (*owner)->instanceId==instanceToken ? *owner : Lease{};
    }

    // CONTROL only: rebind all current vendor leases to a new graph revision.
    bool retainsEditor(const Lease& owner) const noexcept {
        if(!owner)return false;
        for(const auto& row:rows_)
            for(std::size_t i=0;i<row.count;++i)
                if(row.raw[i]==owner.get())return true;
        return false;
    }
    // CONTROL only. A zero destination deletes; otherwise MOVE transfers the
    // exact processor/state/bypass/editor lease, never clones or re-instantiates.
    static Fault editOwned(std::uint64_t epoch,std::uint64_t revision,
            const DynamicChannelFxLeaseBank& prior,ChannelId source,std::size_t slot,
            const juce::String& token,ChannelId target,std::size_t position,
            ProcessingFormat targetFormat,DynamicChannelFxLeaseBank& destination) {
        if(!epoch||epoch!=prior.epoch_||revision<=prior.revision_)return Fault::invalidEpoch;
        if(prior.external_.id==source || (target.value&&prior.external_.id==target))
            return Fault::invalidExternalLease;
        const auto* lease=prior.ownerForControl(source,slot);
        if(!lease||!*lease||(*lease)->instanceId!=token)return Fault::staleInstance;
        std::vector<Assignment> list;
        for(const auto& row:prior.rows_) {
            Assignment a;a.id=row.id;a.format=row.format;a.rackBypassed=row.rackBypassed;
            for(std::size_t i=0;i<row.count;++i)
                if(row.id!=source||i!=slot)a.inserts.push_back(row.owners[i]);
            list.push_back(std::move(a));
        }
        if(target.value) {
            auto found=std::find_if(list.begin(),list.end(),[&](const auto& a){return a.id==target;});
            if(found==list.end()) {
                Assignment a;a.id=target;a.format=targetFormat;
                list.push_back(std::move(a));found=list.end()-1;
            }
            if(position>found->inserts.size()||found->inserts.size()>=maxInserts)
                return Fault::invalidInsertPosition;
            found->inserts.insert(found->inserts.begin()+position,*lease);
        }
        return prepare(epoch,revision,list,destination,prior.external_);
    }

    // The same FxInstance keeps editor/state/tail and the audio callback only
    // sees raw prepared pointers. No new plugin or audio pipeline is created.
    static Fault reuseRevision(std::uint64_t epoch,std::uint64_t revision,
                               const DynamicChannelFxLeaseBank& prior,
                               DynamicChannelFxLeaseBank& destination) {
        if(!epoch || epoch!=prior.epoch_ ||
           revision<=prior.revision_)return Fault::invalidEpoch;
        std::vector<Assignment> retained;
        retained.reserve(prior.rows_.size());
        for(const auto& row:prior.rows_) {
            Assignment item;
            item.id=row.id;item.format=row.format;item.rackBypassed=row.rackBypassed;
            item.inserts.reserve(row.count);
            for(std::size_t slot=0;slot<row.count;++slot)
                item.inserts.push_back(row.owners[slot]);
            retained.push_back(std::move(item));
        }
        return prepare(epoch,revision,retained,destination,prior.external_);
    }

    // Processing MONO/STEREO is a DSP lane property, not a device pin change.
    // Rebind the vendor lease's matching format atomically with the PCM
    // topology; otherwise a later insert would see stale format metadata.
    // Never duplicate, reset or instantiate a live processor on this path.
    static Fault reuseWithProcessingFormat(
            std::uint64_t epoch,std::uint64_t revision,
            const DynamicChannelFxLeaseBank& prior,ChannelId target,
            ProcessingFormat format,DynamicChannelFxLeaseBank& destination) {
        if(!epoch || epoch!=prior.epoch_ || revision<=prior.revision_)
            return Fault::invalidEpoch;
        if(!target.value || prior.external_.id==target)
            return Fault::invalidExternalLease;
        if(format!=ProcessingFormat::mono && format!=ProcessingFormat::stereo)
            return Fault::invalidBusLayout;
        std::vector<Assignment> retained;
        retained.reserve(prior.rows_.size());
        bool found=false;
        for(const auto& row:prior.rows_) {
            Assignment item;
            item.id=row.id;
            item.format=row.id==target?format:row.format;
            item.rackBypassed=row.rackBypassed;
            item.inserts.reserve(row.count);
            for(std::size_t slot=0;slot<row.count;++slot)
                item.inserts.push_back(row.owners[slot]);
            found|=row.id==target;
            retained.push_back(std::move(item));
        }
        // Newly created ChannelIds can be insert-free and absent from the
        // original fixed M4 vendor lease inventory. Such a channel is still
        // a valid PCM lane; create an EMPTY lease row for its new width.
        if(!found) {
            Assignment empty;
            empty.id=target;empty.format=format;
            retained.push_back(std::move(empty));
        }
        return prepare(epoch,revision,retained,destination,prior.external_);
    }

    // CONTROL thread: publish a newly instantiated vendor insert on an
    // independently owned ChannelId, retaining old leases/state/tails.
    // This does NOT call a vendor or allocate from the audio callback.
    static Fault appendOwned(std::uint64_t epoch,std::uint64_t revision,
            const DynamicChannelFxLeaseBank& prior,ChannelId target,
            ProcessingFormat format,Lease plugin,
            DynamicChannelFxLeaseBank& destination) {
        if(!epoch||epoch!=prior.epoch_||revision<=prior.revision_)
            return Fault::invalidEpoch;
        if(!target.value)return Fault::emptyId;
        if(!plugin||!plugin->processor)return Fault::emptyLease;
        if(prior.external_.id==target)return Fault::invalidExternalLease;
        std::vector<Assignment> list;
        list.reserve(prior.rows_.size()+1);
        bool found=false;
        for(const auto& row:prior.rows_) {
            Assignment a;a.id=row.id;a.format=row.format;
            a.rackBypassed=row.rackBypassed;
            a.inserts.reserve(row.count+(row.id==target?1:0));
            for(std::size_t i=0;i<row.count;++i)a.inserts.push_back(row.owners[i]);
            if(row.id==target) {
                if(row.count>=maxInserts)return Fault::tooManyInserts;
                if(row.format!=format)return Fault::invalidBusLayout;
                a.inserts.push_back(plugin);found=true;
            }
            list.push_back(std::move(a));
        }
        if(!found) {
            Assignment a;a.id=target;a.format=format;
            a.inserts.push_back(std::move(plugin));
            list.push_back(std::move(a));
        }
        return prepare(epoch,revision,list,destination,prior.external_);
    }

    // Process each M4 VST exactly once at its target ChannelId. CPU work is
    // proportional to that node's FX rack, not every Channel in the session.
    // Call only on the native audio thread after epoch/revision graph admission.
    void operator()(ChannelId id,float* left,float* right,int frames) const noexcept {
        const Row* row=find(id);
        if(!row)return; // a genuinely dry ChannelId or MASTER
        if(!left||!right||left==right||frames<=0)return;
        if(external_.id==id) {
            external_.render(external_.context,left,right,frames);
            return; // occupied CH1 is processed exactly once
        }
        const bool rackOff=row->rackBypassed &&
            row->rackBypassed->load(std::memory_order_acquire);
        for(std::size_t i=0;i<row->count;++i)
            row->raw[i]->render(left,right,frames,rackOff);
    }

private:
    struct Row final {
        ChannelId id{};
        ProcessingFormat format=ProcessingFormat::stereo;
        std::uint8_t count=0;
        std::shared_ptr<std::atomic<bool>> rackBypassed; // persists across revisions
        std::array<Lease,maxInserts> owners{};
        std::array<FxGraph::FxInstance*,maxInserts> raw{};
    };
    const Row* find(ChannelId id) const noexcept {
        const auto it=std::lower_bound(rows_.begin(),rows_.end(),id.value,
              [](const Row& row,std::uint64_t value){return row.id.value<value;});
        return it!=rows_.end()&&it->id==id?&*it:nullptr;
    }
    std::vector<Row> rows_{};
    ExternalLease external_{}; // PCM snapshot retirement occurs off RT
    std::uint64_t epoch_=0,revision_=0;
};
} // namespace armix::mixer
