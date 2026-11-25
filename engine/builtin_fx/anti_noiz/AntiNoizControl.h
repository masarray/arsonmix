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
    };
    struct Completion { std::uint64_t id=0; bool applied=false; Snapshot state{}; };
    bool prepare(double rate,int channels) noexcept {
        // A new graph epoch requires a fresh Control instance or drained queues.
        if(!commands_.empty()||!done_.empty()||!telemetry_.empty())return false;
        return core_.prepare(rate,channels);
    }
    bool submit(Command c) noexcept {return c.id!=0&&commands_.push(c);}
    bool poll(Completion& c) noexcept {return done_.pop(c);}
    bool pollTelemetry(Snapshot& s) noexcept {return telemetry_.pop(s);}
    void consume() noexcept {
        Command c;
        if(!core_.ready()||!done_.canPush()||!commands_.pop(c))return;
        bool ok=false;
        if(c.kind==Kind::settings)ok=core_.setSettings(c.settings);
        if(c.kind==Kind::learn&&core_.settings().enabled&&core_.settings().mode!=Core::Mode::Music)
            ok=core_.beginLearn();
        (void)done_.push({c.id,ok,snapshot()});
    }
    void process(float* l,float* r,int n) noexcept {
        consume();core_.process(l,r,n);
        if(n>0&&core_.ready()&&telemetry_.canPush())(void)telemetry_.push(snapshot());
    }
    // AUDIO owner or unpublished/quiescent only. No UI thread access.
    Snapshot snapshot() const noexcept {
        return {core_.settings(),core_.hasProfile(),core_.learning(),core_.spectralGain(),core_.residualGain(),core_.latencySamples()};
    }
private:
    Core core_{};
    FxSpsc<Command,16> commands_{};
    FxSpsc<Completion,16> done_{};
    FxSpsc<Snapshot,2> telemetry_{};
};
} // namespace armix::builtin_fx::anti_noiz
