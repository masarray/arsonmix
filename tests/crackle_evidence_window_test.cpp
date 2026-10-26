// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/diagnostics/CrackleEvidenceWindow.h"
#include <cstdlib>
#include <string>
using E=armix::diagnostics::CrackleEvidenceWindow;
static void check(bool ok) { if(!ok)std::abort(); }
static E::Sample sample(std::uint64_t ms, std::uint64_t generation=42,
                        std::string scope="ASIO|mic|speaker|NORMAL")
{
    E::Sample s;
    s.atMs=ms;
    s.deviceGeneration=generation;
    s.routeScope=std::move(scope);
    s.connected=true;
    using C=E::Counter;
    s.counters[static_cast<std::size_t>(C::renderedCallbacks)]={ms/10,true};
    s.counters[static_cast<std::size_t>(C::overBudgetCallbacks)]={5,true};
    s.counters[static_cast<std::size_t>(C::lateCallbackIntervals)]={ms/1000,true};
    s.counters[static_cast<std::size_t>(C::callbackGapIntervals)]={0,true};
    // Unavailable is materially different from a real native zero.
    s.counters[static_cast<std::size_t>(C::wasapiCaptureDiscontinuities)]={0,false};
    s.counters[static_cast<std::size_t>(C::otherDriverXruns)]={0,false};
    s.counters[static_cast<std::size_t>(C::bridgeUnderflowFrames)]={0,false};
    return s;
}
static E::Delta delta(const E::Window& window,E::Counter id)
{
    return window.changes[static_cast<std::size_t>(id)];
}
int main()
{
    E tracker;
    check(tracker.mark(sample(1000)).reason==E::Reason::insufficientHistory);
    for(std::uint64_t ms=1000;ms<=6000;ms+=1000)
        tracker.push(sample(ms));
    auto mark=sample(6500);
    auto window=tracker.mark(mark);
    check(window.ready() && window.elapsedMs==4500);
    check(delta(window,E::Counter::renderedCallbacks).comparable);
    check(delta(window,E::Counter::renderedCallbacks).value==450);
    check(delta(window,E::Counter::overBudgetCallbacks).comparable);
    check(delta(window,E::Counter::overBudgetCallbacks).value==0);
    check(!delta(window,E::Counter::wasapiCaptureDiscontinuities).comparable);
    check(!delta(window,E::Counter::bridgeUnderflowFrames).comparable);

    // Real 0 for a source-specific WASAPI capture discontinuity is valid.
    tracker.reset();
    auto zero=sample(1000);
    zero.counters[static_cast<std::size_t>(E::Counter::wasapiCaptureDiscontinuities)]={0,true};
    tracker.push(zero);
    for(std::uint64_t ms=2000;ms<=4000;ms+=1000) tracker.push(sample(ms));
    auto after=sample(5000);
    after.counters[static_cast<std::size_t>(E::Counter::wasapiCaptureDiscontinuities)]={3,true};
    auto res=tracker.mark(after);
    check(res.ready());
    check(delta(res,E::Counter::wasapiCaptureDiscontinuities).value==3);
    check(delta(res,E::Counter::wasapiCaptureDiscontinuities).comparable);

    // A device-restart or backend/route/mute change invalidates ANY delta.
    check(tracker.mark(sample(2000,43)).reason==E::Reason::changedRouteOrGeneration);
    check(tracker.mark(sample(2000,42,"WASAPI|mic|speaker|NORMAL")).reason==
          E::Reason::changedRouteOrGeneration);
    tracker.push(sample(3000,43));
    check(tracker.mark(sample(3300,42)).reason==E::Reason::changedRouteOrGeneration);
    check(tracker.mark(sample(3300,43)).reason==E::Reason::noSampleInsideWindow);

    // Reset counter never wraps to a huge plausible audio loss.
    tracker.reset();
    auto old=sample(1000);
    old.counters[static_cast<std::size_t>(E::Counter::bridgeUnderflowFrames)]={1000,true};
    tracker.push(old);
    for(std::uint64_t ms=2000;ms<=4000;ms+=1000) tracker.push(sample(ms));
    auto fresh=sample(5000);
    fresh.counters[static_cast<std::size_t>(E::Counter::bridgeUnderflowFrames)]={3,true};
    res=tracker.mark(fresh);
    check(res.ready());
    check(!delta(res,E::Counter::bridgeUnderflowFrames).comparable);
    check(delta(res,E::Counter::renderedCallbacks).comparable);

    // Missing sample at report time remains N/A, never a made-up zero.
    fresh=sample(5000);
    fresh.counters[static_cast<std::size_t>(E::Counter::renderedCallbacks)].available=false;
    res=tracker.mark(fresh);
    check(res.ready() && !delta(res,E::Counter::renderedCallbacks).comparable);

    // A stale control polling loop cannot claim a 5s pre-marker window.
    check(tracker.mark(sample(6201)).reason==E::Reason::stalePoll);
    check(tracker.mark(sample(900)).reason==E::Reason::clockRegression);
    check(tracker.mark(sample(5000,42,"changed-route")).reason==
          E::Reason::changedRouteOrGeneration);

    // Never bridge a >2.1s sampling gap; old history is obsolete.
    tracker.reset();
    tracker.push(sample(1000));
    tracker.push(sample(3500));
    check(tracker.mark(sample(4000)).reason==E::Reason::noSampleInsideWindow);
    tracker.push(sample(4500));
    check(tracker.mark(sample(5000)).ready());

    // Exact five-second bound (exclude 5.001s old samples) and ring wrap.
    tracker.reset();
    for(std::uint64_t ms=1000;ms<=18000;ms+=1000) tracker.push(sample(ms));
    res=tracker.mark(sample(18500));
    check(res.ready() && res.elapsedMs==4500);
    // The history includes 14000..18000. The old 13000 is inside the ring
    // but older than 5000ms and MUST be excluded.
    check(delta(res,E::Counter::renderedCallbacks).value==450);
    tracker.push(sample(19000));
    res=tracker.mark(sample(19500));
    check(res.ready() && res.elapsedMs==4500);

    // Route device disconnected: fail explicit, and drop existing history.
    auto offline=sample(20000);
    offline.connected=false;
    check(tracker.mark(offline).reason==E::Reason::noDevice);
    tracker.push(offline);
    check(tracker.mark(sample(21000)).reason==E::Reason::insufficientHistory);
    check(std::string(E::name(E::Counter::wasapiCaptureDiscontinuities))
        =="WASAPI_CAPTURE_DISCONTINUITY_FLAGS");
    check(std::string(E::name(E::Counter::bridgeUnderflowFrames))
        =="ASRC_INPUT_UNDERFLOW_FRAMES");
    check(std::string(E::reasonName(E::Reason::ready))=="BOUNDED_PRE_MARK_WINDOW");
    return 0;
}
