// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AntiNoizNativeState.h"
#include <memory>
namespace armix::builtin_fx::anti_noiz {
// Native lifetime seam. CONTROL owns construction, restore, submit/poll and
// accepted settings. AUDIO alone owns histories and bypass. Host lease fences
// ChannelId/instance/epoch; a published session is never re-prepared in place.
class NativeSession final {
public:
    enum class LearnStatus { Idle, Learning, Accepted, Rejected, Cancelled };
    struct Snapshot : Control::Snapshot {
        LearnStatus learnStatus=LearnStatus::Idle;
        bool rackBypassed=false;
    };
    bool prepare(double rate,int channels,Core::SourceRole role=Core::SourceRole::Unknown) {
        if(sealed_||pending_)return false;
        ready_=false;
        auto next=std::make_unique<Control>();
        if(!next->prepare(rate,channels,role)||!next->stageUnpublished(accepted_))return false;
        Snapshot stale;while(telemetry_.pop(stale)){}
        fx_=std::move(next);rate_=rate;channels_=channels;
        status_=LearnStatus::Idle;rackBypassed_=fully_=preConsumed_=false;
        interval_=static_cast<int>(rate/20.);remaining_=1;
        observed_=nativeSnapshot();ready_=true;return true;
    }
    void release() noexcept {ready_=false;}
    bool ready() const noexcept {return ready_;}
    int latency() const noexcept {return ready_?(rate_>48000.?512:256):0;}
    double rate() const noexcept {return rate_;}
    int channels() const noexcept {return channels_;}
    void seal() noexcept {sealed_=true;}
    bool quiescent() const noexcept {return pending_==0;}
    Core::Settings acceptedSettings() const noexcept {return accepted_;}
    bool restore(std::span<const std::uint8_t> bytes) noexcept {
        if(sealed_||pending_)return false;
        Core::Settings s;
        if(!NativeState::decode(bytes,s))return false;
        if(ready_&&!fx_->stageUnpublished(s))return false;
        accepted_=s;if(ready_)observed_=nativeSnapshot();return true;
    }
    bool submit(Control::Command c) noexcept {
        if(!ready_||pending_||!c.id||!fx_->submit(c))return false;
        pending_=c.id;return true;
    }
    bool poll(Control::Completion& ack) noexcept {
        if(!fx_||!fx_->poll(ack))return false;
        if(ack.id==pending_){if(ack.applied)accepted_=ack.state.settings;pending_=0;}
        return true;
    }
    bool pollTelemetry(Snapshot& s) noexcept {
        if(!telemetry_.pop(s))return false;
        observed_=s;return true;
    }
    // CONTROL-only last observation; ACK is the durable parameter authority.
    Snapshot observed() const noexcept {return observed_;}
    void onBypass(bool requested,bool fully) noexcept {
        if(!ready_)return;
        if(requested&&!rackBypassed_&&fx_->learning()){
            fx_->cancelNativeLearn();status_=LearnStatus::Cancelled;
        }
        rackBypassed_=requested;fully_=fully;preConsumed_=fully;
    }
    void consume() noexcept {
        if(!ready_)return;
        const bool before=fx_->learning();
        if(rackBypassed_)fx_->consumeWhileRackBypassed();else fx_->consume();
        const bool after=fx_->learning();
        if(!before&&after)status_=LearnStatus::Learning;
        if(before&&!after)status_=LearnStatus::Cancelled;
    }
    void process(float* l,float* r,int n) noexcept {
        if(!ready_||n<=0)return;
        const bool consumed=preConsumed_;preConsumed_=false;
        if(!consumed)consume();
        const bool before=fx_->learning();
        // Fully bypassed: reuse the existing delayed-unity WOLA fast path.
        // During the host fade, keep normal wet DSP; the host owns crossfade.
        fx_->processNativeAudio(l,r,n,fully_);
        if(before&&!fx_->learning())
            status_=fx_->lastNativeLearnQualified()?LearnStatus::Accepted:LearnStatus::Rejected;
        if(n>=remaining_){
            remaining_=interval_-(n-remaining_)%interval_;
            if(telemetry_.canPush())(void)telemetry_.push(nativeSnapshot());
        }else remaining_-=n;
    }
private:
    Snapshot nativeSnapshot() const noexcept {
        return {fx_->snapshot(),status_,rackBypassed_};
    }
    std::unique_ptr<Control> fx_;
    Core::Settings accepted_{};
    Snapshot observed_{};
    // Native path uses this extended telemetry queue instead of Control's
    // standalone queue; no duplicate publication or readback authority.
    FxSpsc<Snapshot,2> telemetry_{};
    std::uint64_t pending_=0;
    double rate_=0.;
    int channels_=0,interval_=2400,remaining_=1;
    LearnStatus status_=LearnStatus::Idle;
    bool ready_=false,sealed_=false,preConsumed_=false,rackBypassed_=false,fully_=false;
};
}
