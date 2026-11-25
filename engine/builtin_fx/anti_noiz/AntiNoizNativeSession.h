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
    bool prepare(double rate,int channels,Core::SourceRole role=Core::SourceRole::Unknown) {
        if(sealed_||pending_)return false;
        ready_=false;
        auto next=std::make_unique<Control>();
        if(!next->prepare(rate,channels,role)||!next->stageUnpublished(accepted_))return false;
        observed_=next->snapshot();fx_=std::move(next);
        rate_=rate;channels_=channels;ready_=true;return true;
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
        accepted_=s;if(ready_)observed_=fx_->snapshot();return true;
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
    bool pollTelemetry(Control::Snapshot& s) noexcept {
        if(!fx_||!fx_->pollTelemetry(s))return false;
        observed_=s;return true;
    }
    // CONTROL-only last observation; ACK is the durable parameter authority.
    Control::Snapshot observed() const noexcept {return observed_;}
    void onBypass(bool requested,bool fully) noexcept {
        if(!ready_)return;
        fx_->setHostBypass(requested);preConsumed_=fully;
    }
    void consume() noexcept {if(ready_)fx_->consume();}
    void process(float* l,float* r,int n) noexcept {
        if(!ready_)return;
        const bool consumed=preConsumed_;preConsumed_=false;
        if(!consumed)fx_->consume();
        fx_->processAudio(l,r,n);
    }
private:
    std::unique_ptr<Control> fx_;
    Core::Settings accepted_{};
    Control::Snapshot observed_{};
    std::uint64_t pending_=0;
    double rate_=0.;
    int channels_=0;
    bool ready_=false,sealed_=false,preConsumed_=false; // lifecycle quiescent; flag AUDIO only after publication
};
}
