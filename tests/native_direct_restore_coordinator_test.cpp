// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include <utility>
#include "../src/NativeDirectRestoreCoordinator.h"

using Coordinator=armix::device::NativeDirectRestoreCoordinator;
using Gate=armix::device::DirectRestoreBackoff;
struct Engine {
    bool active=false,fault=false,pending=false,muted=false;
    juce::String result;
    int polls=0,attempts=0;
    void pollIndependentClockHealth(std::uint64_t) {++polls;}
    bool independentClockFaulted() const {return fault;}
    bool independentClockActive() const {return active;}
    bool hasPendingDirectRestore() const {return pending;}
    bool isOutputMuted() const {return muted;}
    juce::var status() const {
        auto state=juce::var(new juce::DynamicObject());
        auto* obj=state.getDynamicObject();
        obj->setProperty("bridgeNegotiationFault","ASIO_CLOCK");
        obj->setProperty("bridgeWatchdogFault","CAPTURE_STALLED");
        obj->setProperty("bridgeCaptureSilentMs",234);
        obj->setProperty("bridgeRenderSilentMs",12);
        obj->setProperty("bridgeContinuityFault","NONE");
        obj->setProperty("bridgeContinuityLossBp",2);
        obj->setProperty("bridgeContinuityBadWindows",1);
        return state;
    }
    juce::String setIndependentClockEnabled(bool enable) {
        assert(!enable);
        ++attempts;
        if(result.isEmpty()) {
            fault=false;pending=false;active=false;
        }
        return result;
    }
};
int main() {
    Gate gate;
    Engine engine;
    juce::String lastError;
    bool dirty=false;
    std::vector<std::pair<juce::String,juce::String>> events;
    std::vector<juce::String> notices;
    auto record=[&](const juce::String& key,const juce::String& value) {
        events.emplace_back(key,value);
    };
    auto notify=[&](const juce::String& message,bool isError) {
        assert(isError);notices.push_back(message);
    };
    auto tick=[&](std::uint64_t now) {
        Coordinator::service(gate,lastError,dirty,now,engine,record,notify);
    };
    tick(100);
    assert(engine.polls==1 && engine.attempts==0);
    engine.active=true;engine.fault=true;engine.result="driver cannot reopen";
    tick(200); // immediate first attempt, never in callback
    assert(engine.attempts==1 && !dirty);
    assert(lastError=="driver cannot reopen");
    assert(gate.snapshot().state==Gate::State::waiting);
    assert(events.size()==2 && notices.size()==1);
    assert(events[1].second.startsWith("RESTORE_FAIL 1/5"));
    assert(events[1].second.contains("captureSilentMs=234"));
    juce::var state(new juce::DynamicObject());
    Coordinator::annotate(state,gate,lastError,true,300);
    assert(state.getProperty("directRestoreState","")=="WAITING");
    assert(static_cast<int>(state.getProperty("directRestoreNextMs",0))==150);
    const auto pending=state.getProperty("directRestorePending",false);
    assert(pending.isBool() && static_cast<bool>(pending));
    tick(449);
    assert(engine.attempts==1);
    engine.result.clear();
    engine.muted=true;
    tick(450);
    assert(engine.attempts==2 && dirty);
    assert(lastError.isEmpty() && gate.snapshot().state==Gate::State::idle);
    assert(notices.back().contains("remains muted"));
    assert(events.back().second.contains("DIRECT_RESTORED after 2 attempt"));
    assert(engine.active==false);
    tick(451);
    assert(engine.attempts==2); // recovery cannot flap/auto-arm

    // Lost DIRECT while no independent endpoint is active uses the same
    // bounded policy; exhausted retries never reopen a faulting bridge.
    engine.pending=true;
    engine.result="physical output missing";
    gate.cancel();
    dirty=false;notices.clear();
    tick(1000);
    assert(engine.attempts==3 && notices.size()==1);
    tick(1249);assert(engine.attempts==3);
    tick(1250);assert(engine.attempts==4);
    tick(2250);assert(engine.attempts==5);
    tick(5250);assert(engine.attempts==6);
    tick(13250);assert(engine.attempts==7);
    assert(gate.snapshot().state==Gate::State::exhausted);
    assert(notices.back().contains("exhausted"));
    tick(30000);assert(engine.attempts==7 && !dirty);
    juce::var exhausted(new juce::DynamicObject());
    Coordinator::annotate(exhausted,gate,lastError,true,30000);
    assert(exhausted.getProperty("directRestoreState","")=="EXHAUSTED");
    assert(static_cast<int>(exhausted.getProperty("directRestoreAttempts",0))==5);
    gate.cancel();engine.pending=false;
    tick(31000);assert(engine.attempts==7);
    juce::var invalid;Coordinator::annotate(invalid,gate,lastError,false,0);
    assert(!invalid.isObject());
}
