// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AntiNoizCore.h"
#include "../FxSpsc.h"
#include <cstdint>
namespace armix::builtin_fx::anti_noiz {
// One CONTROL producer, one AUDIO consumer. Lifecycle only while quiescent.
// Host adapter must fence instance identity and graph generation externally.
class Control final {
public:
    enum class Kind { settings, learn };
    struct Command { std::uint64_t id=0; Kind kind=Kind::settings; Core::Settings settings{}; };
    struct Snapshot {
        Core::Settings settings{};
        bool profile=false, learning=false;
        float spectralGain=1, residualGain=1;
        int latency=0;
        Core::SourceRole sourceRole=Core::SourceRole::Unknown;
        Core::Mode effectiveMode=Core::Mode::AutoProtect;
    };
    struct Completion { std::uint64_t id=0; bool applied=false; Snapshot state{}; };
    bool prepare(double rate,int channels,Core::SourceRole role=Core::SourceRole::Unknown) noexcept {
        // A new graph epoch requires a fresh Control instance or drained queues.
        if(!commands_.empty()||!done_.empty()||!telemetry_.empty())return false;
        if(!core_.prepare(rate,channels,role))return false;
        telemetryInterval_=static_cast<int>(rate/20.);remaining_=1;return true;
    }
    bool submit(Command c) noexcept {
        if(c.id==0||!commands_.canPush())return false;
        Packet packet{c.id,c.kind,{}};
        if(c.kind==Kind::settings)packet.prepared=Core::prepareSettings(c.settings);
        // Invalid settings still get an AUDIO-owned negative ACK, as before.
        return commands_.push(packet);
    }
    bool poll(Completion& c) noexcept {return done_.pop(c);}
    bool pollTelemetry(Snapshot& s) noexcept {return telemetry_.pop(s);}
    void consume() noexcept {
        Packet c;
        if(!core_.ready()||!done_.canPush()||!commands_.pop(c))return;
        bool ok=false;
        if(c.kind==Kind::settings)ok=core_.applySettings(c.prepared);
        if(c.kind==Kind::learn&&core_.settings().enabled&&core_.effectiveMode()!=Core::Mode::Music)
            ok=core_.beginLearn();
        (void)done_.push({c.id,ok,snapshot()});
    }
    void process(float* l,float* r,int n) noexcept {
        consume();core_.process(l,r,n);
        if(n<=0||!core_.ready())return;
        if(n>=remaining_){
            // Carry overshoot; do not accumulate block-size-dependent drift.
            // At most one latest snapshot per callback, even for huge blocks.
            remaining_=telemetryInterval_-(n-remaining_)%telemetryInterval_;
            if(telemetry_.canPush())(void)telemetry_.push(snapshot());
        }else remaining_-=n;
    }
    // AUDIO owner or unpublished/quiescent only. No UI thread access.
    Snapshot snapshot() const noexcept {
        return {core_.settings(),core_.hasProfile(),core_.learning(),core_.spectralGain(),core_.residualGain(),core_.latencySamples(),core_.sourceRole(),core_.effectiveMode()};
    }
    // Native-only seams. Do not change the qualified standalone process,
    // command packet, Snapshot layout or steady-state DSP.
    bool stageUnpublished(Core::Settings s) noexcept {
        return core_.ready()&&commands_.empty()&&done_.empty()&&telemetry_.empty()&&core_.setSettings(s);
    }
    bool learning() const noexcept {return core_.learning();}
    bool lastNativeLearnQualified() const noexcept {return core_.lastNativeLearnQualified();}
    void cancelNativeLearn() noexcept {core_.cancelNativeLearn();}
    void consumeWhileRackBypassed() noexcept {
        Packet c;
        if(!core_.ready()||!done_.canPush()||!commands_.pop(c))return;
        // Same ordered ACK and prepared settings as consume(); only Learn
        // authority differs. No mutation of the user's durable Power.
        const bool ok=c.kind==Kind::settings&&core_.applySettings(c.prepared);
        (void)done_.push({c.id,ok,snapshot()});
    }
    void processNativeAudio(float* l,float* r,int n,bool dry) noexcept {
        if(dry)core_.processNativeDry(l,r,n);else core_.process(l,r,n);
    }
private:
    struct Packet {
        std::uint64_t id=0;Kind kind=Kind::settings;
        Core::PreparedSettings prepared{};
    };
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "Anti Noiz audio queues require lock-free indices");
    int telemetryInterval_=2400,remaining_=1;
    Core core_{};
    FxSpsc<Packet,16> commands_{};
    FxSpsc<Completion,16> done_{};
    FxSpsc<Snapshot,2> telemetry_{};
};
} // namespace armix::builtin_fx::anti_noiz
