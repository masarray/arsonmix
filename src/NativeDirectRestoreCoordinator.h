// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/device/DirectRestoreBackoff.h"
#include <juce_core/juce_core.h>

// N-MOD-19: ONE control-thread DIRECT restore coordinator. Does not own a
// device, callback, PCM renderer, driver binding, or recovery timer. The
// existing LiveAudioEngine still exclusively owns JUCE device transactions.
// The caller supplies diagnostics and operator notification sinks.
namespace armix::device {
class NativeDirectRestoreCoordinator final {
public:
    using Gate=DirectRestoreBackoff;
    static void annotate(juce::var& state,const Gate& gate,
                         const juce::String& previousError,
                         bool pending,std::uint64_t nowMs) {
        auto* obj=state.getDynamicObject();
        if(!obj)return;
        const auto snapshot=gate.snapshot();
        const char* stateName=snapshot.state==Gate::State::idle?"IDLE":
            snapshot.state==Gate::State::waiting?"WAITING":"EXHAUSTED";
        obj->setProperty("directRestoreState",stateName);
        obj->setProperty("directRestoreAttempts",
                         static_cast<int>(snapshot.attempts));
        obj->setProperty("directRestoreNextMs",
                         static_cast<double>(gate.remainingMs(nowMs)));
        obj->setProperty("directRestoreGeneration",
                         static_cast<double>(snapshot.generation));
        obj->setProperty("directRestoreError",previousError);
        obj->setProperty("directRestorePending",pending);
    }

    template<class Engine,class Diagnostic,class Notice>
    static void service(Gate& gate,juce::String& previousError,
                        bool& settingsDirty,std::uint64_t nowMs,
                        Engine& engine,Diagnostic&& record,Notice&& notify) {
        // Control thread only. Polling observes independent callback atomic
        // progress but never executes or blocks inside either audio callback.
        engine.pollIndependentClockHealth(nowMs);
        auto snapshot=gate.snapshot();
        if(snapshot.state==Gate::State::idle &&
           (engine.independentClockFaulted() ||
            (!engine.independentClockActive() &&
             engine.hasPendingDirectRestore()))) {
            const auto generation=gate.begin(nowMs);
            record("DIRECT_RESTORE_BEGIN",
                   juce::String("Generation ") +
                   juce::String(static_cast<juce::int64>(generation)));
            snapshot=gate.snapshot();
        }
        if(!gate.due(snapshot.generation,nowMs))return;

        const bool wasIndependent=engine.independentClockActive();
        const auto stateBeforeRestore=engine.status();
        const auto faultStatus=juce::JSON::toString(stateBeforeRestore,false);
        // Keep field diagnostics FIRST: recorder truncation must not erase
        // stream watchdog evidence when the device is disposed.
        const auto watchdogSummary=juce::String("format=")
            + stateBeforeRestore.getProperty("bridgeNegotiationFault","NONE").toString()
            + " watchdog="
            + stateBeforeRestore.getProperty("bridgeWatchdogFault","NONE").toString()
            + " captureSilentMs="
            + stateBeforeRestore.getProperty("bridgeCaptureSilentMs",0).toString()
            + " renderSilentMs="
            + stateBeforeRestore.getProperty("bridgeRenderSilentMs",0).toString()
            + " continuity="
            + stateBeforeRestore.getProperty("bridgeContinuityFault","NONE").toString()
            + " lossBp="
            + stateBeforeRestore.getProperty("bridgeContinuityLossBp",0).toString()
            + " badWindows="
            + stateBeforeRestore.getProperty("bridgeContinuityBadWindows",0).toString();
        // The actual restore call remains on the one existing engine owner.
        const auto error=engine.setIndependentClockEnabled(false);
        if(error.isEmpty()) {
            gate.recovered(snapshot.generation);
            previousError.clear();
            settingsDirty=true;
            record("SMART_CLOCK_RECOVERY",
                   juce::String("DIRECT_RESTORED after ") +
                   juce::String(snapshot.attempts+1) +
                   " attempt(s) | " +watchdogSummary+" | "+faultStatus);
            if(engine.isOutputMuted())
                notify("DIRECT device reopened. Output remains muted for safety; unmute only when ready.",true);
            else
                notify(wasIndependent?
                       "Independent device fault: safe DIRECT audio restored.":
                       "DIRECT audio device restored after a bounded retry.",true);
            return;
        }
        gate.failed(snapshot.generation,nowMs);
        previousError=error;
        const auto next=gate.snapshot();
        record("SMART_CLOCK_RECOVERY",
               juce::String("RESTORE_FAIL ") +
               juce::String(next.attempts) + "/" +
               juce::String(static_cast<int>(Gate::maxAttempts)) +
               ": " +error+" | "+watchdogSummary+" | "+faultStatus);
        if(next.state==Gate::State::exhausted)
            notify("Automatic DIRECT recovery exhausted; audio remains safe/muted. "
                   "Check AUDIO SETUP or retry DIRECT manually. "+error,true);
        else if(next.attempts==1)
            notify("Device interrupted. DIRECT restore is retrying with bounded backoff.",true);
    }
};
} // namespace armix::device
