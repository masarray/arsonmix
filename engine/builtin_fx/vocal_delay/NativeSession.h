// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ContinuousDelayCore.h"
#include "StateCodec.h"
#include "../FxSpsc.h"
#include <cstdint>
#include <type_traits>
namespace armix::builtin_fx::vocal_delay {
// Host supplies immutable identity from the accepted graph. Never use UI
// slot numbers as an identity. All lifecycle operations are stopped-only.
struct Lease final {
    std::uint64_t channel=0,instance=0,epoch=0,graph=0;
    bool valid() const noexcept{return channel&&instance&&epoch&&graph;}
    bool operator==(const Lease& b) const noexcept{
        return channel==b.channel&&instance==b.instance&&epoch==b.epoch&&graph==b.graph;
    }
};
struct Edit final {Lease lease{};std::uint32_t revision=0;Parameters settings{};};
struct Receipt final {Lease lease{};std::uint32_t revision=0;Parameters accepted{};bool applied=false;};
struct Snapshot final {Lease lease{};std::uint32_t revision=0,lastSubmitted=0;Parameters accepted{};bool quiescent=false;};
enum class Submit { queued, stale, invalid, busy, notReady };
class NativeSession final {
public:
    // CONTROL/device while unpublished or fully stopped; never callback.
    bool prepareStopped(double rate,Lease lease,const State& state){
        if(!lease.valid()||!validRate(rate)||state.kind!=State::Kind::continuous||
           !valid(state.continuous))return false;
        // Same lease must never reset revision or replay a stale packet.
        if(ready_&&!newerLease(lease))return false;
        ready_=false;clearStopped();
        if(!core_.prepare(rate,state.continuous))return false;
        lease_=lease;rate_=rate;accepted_=audioAccepted_=state.continuous;
        lastSubmitted_=acceptedRevision_=audioRevision_=0;pending_=false;
        bypassed_=fullyBypassed_=false;ready_=true;return true;
    }
    // Preserve applied-but-not-yet-polled settings on a stopped device restart.
    // Old unconsumed edits are discarded, never replayed on the next epoch.
    bool restartStopped(double rate,Lease lease){
        if(!ready_||!newerLease(lease))return false;
        State s{};s.continuous=audioAccepted_;
        return prepareStopped(rate,lease,s);
    }
    // CONTROL only. One in-flight edit; UI/controller coalesces latest desired.
    Submit submit(const Edit& edit) noexcept{
        if(!ready_)return Submit::notReady;
        if(!(edit.lease==lease_)||!edit.revision||edit.revision<=lastSubmitted_)return Submit::stale;
        if(!valid(edit.settings))return Submit::invalid;
        if(pending_)return Submit::busy;
        Command c{};c.edit=edit;
        if(!core_.prepareEdit(edit.settings,rate_,c.prepared))return Submit::invalid;
        if(!commands_.push(c))return Submit::busy;
        pending_=true;lastSubmitted_=edit.revision;return Submit::queued;
    }
    // AUDIO only, also called when host skips the PCM for a bypassed insert.
    void consumeRealtimeEdits() noexcept{
        Command c{};
        if(!ready_||!receipts_.canPush()||!commands_.pop(c))return;
        const bool ok=c.edit.lease==lease_&&c.edit.revision>audioRevision_&&core_.applyPrepared(c.prepared);
        if(ok){audioAccepted_=c.edit.settings;audioRevision_=c.edit.revision;}
        (void)receipts_.push({lease_,c.edit.revision,audioAccepted_,ok});
    }
    void onRealtimeBypass(bool requested,bool fully) noexcept{
        if((requested&&fully&&!fullyBypassed_)||(!requested&&bypassed_))core_.invalidate();
        bypassed_=requested;fullyBypassed_=requested&&fully;
    }
    void processStereo(float* l,float* r,std::size_t n) noexcept{
        consumeRealtimeEdits();
        if(ready_&&!fullyBypassed_)core_.processStereo(l,r,n);
    }
    // CONTROL only. Accepted settings do not advance until AUDIO applied.
    bool poll(Receipt& receipt) noexcept{
        if(!receipts_.pop(receipt))return false;
        pending_=false;
        if(receipt.applied&&receipt.lease==lease_&&receipt.revision>acceptedRevision_){
            accepted_=receipt.accepted;acceptedRevision_=receipt.revision;
        }
        return true;
    }
    ContinuousDelayCore::Meter meter() const noexcept{return core_.meter();}
    bool quiescent() const noexcept{return ready_&&!pending_;}
    Snapshot acceptedSnapshot() const noexcept{return {lease_,acceptedRevision_,lastSubmitted_,accepted_,quiescent()};}
    bool save(StateCodec::Bytes& out) const noexcept{
        if(!quiescent())return false;
        State s{};s.continuous=accepted_;return StateCodec::encode(s,out);
    }
private:
    bool newerLease(const Lease& l) const noexcept{
        return l.channel==lease_.channel&&l.instance==lease_.instance&&
            (l.epoch>lease_.epoch||(l.epoch==lease_.epoch&&l.graph>lease_.graph));
    }
    struct Command {Edit edit{};ContinuousDelayCore::Prepared prepared{};};
    static_assert(std::is_trivially_copyable<Command>::value);
    static_assert(std::is_trivially_copyable<Receipt>::value);
    void clearStopped() noexcept{
        Command c{};while(commands_.pop(c)){}
        Receipt r{};while(receipts_.pop(r)){}
    }
    ContinuousDelayCore core_{};
    FxSpsc<Command,4> commands_{};
    FxSpsc<Receipt,4> receipts_{};
    Lease lease_{};double rate_=0.;
    Parameters accepted_{},audioAccepted_{};
    std::uint32_t lastSubmitted_=0,acceptedRevision_=0,audioRevision_=0;
    bool ready_=false,pending_=false,bypassed_=false,fullyBypassed_=false;
};
} // namespace armix::builtin_fx::vocal_delay
