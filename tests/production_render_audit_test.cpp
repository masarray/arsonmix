// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/core/ProductionRenderAudit.h"
#include <array>
#include <cassert>
#include <type_traits>
#include <utility>
using Audit=armix::rt::ProductionRenderAudit;
static_assert(!std::is_copy_constructible_v<Audit>);
static_assert(noexcept(std::declval<Audit&>().started(Audit::Path::channelId)));
static_assert(noexcept(std::declval<Audit&>().snapshot()));
int main() {
    Audit a;
    auto s=a.snapshot();
    assert(s.nativeStarted==0 && s.m4Started==0 && s.overlapDenied==0);
    // Count once per outer hardware callback, not per DSP chunk or channel.
    a.started(Audit::Path::channelId);a.completed(Audit::Path::channelId);
    a.started(Audit::Path::channelId);a.completed(Audit::Path::channelId);
    a.started(Audit::Path::channelId);a.failed(Audit::Path::channelId);
    a.started(Audit::Path::compatibilityM4);a.completed(Audit::Path::compatibilityM4);
    a.started(Audit::Path::compatibilityM4);a.failed(Audit::Path::compatibilityM4);
    a.deniedOverlap();a.unpatched();a.unpatched();a.muted();
    s=a.snapshot();
    assert(s.nativeStarted==3&&s.nativeCompleted==2&&s.nativeFailed==1);
    assert(s.m4Started==2&&s.m4Completed==1&&s.m4Failed==1);
    assert(s.overlapDenied==1&&s.unpatchedSilent==2&&s.mutedNoRender==1);
    assert(s.nativeStarted==s.nativeCompleted+s.nativeFailed);
    assert(s.m4Started==s.m4Completed+s.m4Failed);

    // M3b source-retirement gate: read two real counter boundaries, not
    // process-lifetime totals. Historical M4 blocks must not taint a new
    // native-only window, but a fresh M4 block must disqualify it.
    using Status=Audit::WindowStatus;
    const auto begin=a.snapshot();
    for(int i=0;i<4;++i) {
        a.started(Audit::Path::channelId);
        a.completed(Audit::Path::channelId);
    }
    const auto end=a.snapshot();
    const auto verified=Audit::assessWindow(begin,end,true,true,true);
    assert(verified.exclusiveNativeObserved());
    assert(verified.delta.nativeCompleted==4);
    assert(verified.delta.m4Started==0);
    assert(verified.delta.nativeFailed==0);

    assert(Audit::assessWindow(begin,end,false,true,true).status==
           Status::invalidContext);
    assert(Audit::assessWindow(begin,end,true,false,true).status==
           Status::invalidContext);
    assert(Audit::assessWindow(begin,end,true,true,false).status==
           Status::invalidContext);

    auto reset=end;
    --reset.m4Completed;
    assert(Audit::assessWindow(begin,reset,true,true,true).status==
           Status::countersRegressed);
    auto mid=end;
    ++mid.nativeStarted; // asynchronous CONTROL read during DSP callback
    assert(Audit::assessWindow(begin,mid,true,true,true).status==
           Status::boundaryInFlight);
    auto beforePartial=begin;
    ++beforePartial.nativeStarted;
    assert(Audit::assessWindow(beforePartial,end,true,true,true).status==
           Status::boundaryInFlight);

    auto withLegacy=end;
    ++withLegacy.m4Started;++withLegacy.m4Completed;
    const auto fallback=Audit::assessWindow(begin,withLegacy,true,true,true);
    assert(fallback.status==Status::legacyRendered);
    assert(fallback.delta.m4Completed==1);

    auto withFailure=end;
    ++withFailure.nativeStarted;++withFailure.nativeFailed;
    assert(Audit::assessWindow(begin,withFailure,true,true,true).status==
           Status::nativeFailed);
    auto withOverlap=end;
    ++withOverlap.overlapDenied;
    assert(Audit::assessWindow(begin,withOverlap,true,true,true).status==
           Status::nativeFailed);

    auto noNewBlock=Audit::assessWindow(end,end,true,true,true);
    assert(noNewBlock.status==Status::noNativeRender);
    auto withMute=end;
    ++withMute.mutedNoRender;
    assert(Audit::assessWindow(begin,withMute,true,true,true).status==
           Status::silentOrMuted);
    auto withUnpatched=end;
    ++withUnpatched.unpatchedSilent;
    assert(Audit::assessWindow(begin,withUnpatched,true,true,true).status==
           Status::silentOrMuted);

    // M3c: a single successful render window cannot qualify sustained
    // native-only ownership. Every boundary needs independent same-process
    // same-physical-epoch admission, and each intervening window must prove
    // completed ChannelId PCM with no M4/silent/failed/overlap activity.
    using Boundary=Audit::Boundary;
    using SeriesStatus=Audit::SeriesStatus;
    Audit continuous;
    continuous.started(Audit::Path::compatibilityM4);
    continuous.completed(Audit::Path::compatibilityM4); // historical only
    std::array<Boundary,4> boundaries{};
    for(std::size_t i=0;i<boundaries.size();++i) {
        boundaries[i]={continuous.snapshot(),73,19,true};
        if(i+1<boundaries.size())
            for(int block=0;block<8;++block) {
                continuous.started(Audit::Path::channelId);
                continuous.completed(Audit::Path::channelId);
            }
    }
    const auto sustained=Audit::assessSeries(boundaries,24);
    assert(sustained.continuousNativeObserved());
    assert(sustained.nativeCompleted==24);
    assert(Audit::assessSeries(boundaries,25).status==
           SeriesStatus::insufficientNativeBlocks);
    assert(Audit::assessSeries(boundaries,0).status==
           SeriesStatus::tooFewWindows);
    assert(Audit::assessSeries(std::span<const Boundary>{
           boundaries.data(),3},1).status==SeriesStatus::tooFewWindows);
    auto stale=boundaries;
    stale[2].deviceEpoch=20;
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::invalidContext);
    stale=boundaries;
    stale[1].processNonce=74;
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::invalidContext);
    stale=boundaries;
    stale[2].verifiedInstalledChannelId=false;
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::invalidContext);
    stale=boundaries;
    stale[0].processNonce=0;
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::invalidContext);
    stale=boundaries;
    ++stale[2].counters.m4Started;
    ++stale[2].counters.m4Completed;
    const auto legacyInWindow=Audit::assessSeries(stale,1);
    assert(legacyInWindow.status==SeriesStatus::badRenderWindow);
    assert(legacyInWindow.firstBadWindow==Status::legacyRendered);
    stale=boundaries;
    ++stale[2].counters.mutedNoRender;
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::badRenderWindow);
    stale=boundaries;
    ++stale[2].counters.nativeStarted; // genuine in-flight snapshot
    assert(Audit::assessSeries(stale,1).status==SeriesStatus::badRenderWindow);
    stale=boundaries;
    stale[2].counters.nativeStarted=0; // reset or swapped process
    const auto resetSeries=Audit::assessSeries(stale,1);
    assert(resetSeries.status==SeriesStatus::badRenderWindow);
    assert(resetSeries.firstBadWindow==Status::countersRegressed);
    // These results never authorize output, retire M4 or replace physical
    // ASIO field evidence. A different epoch requires a NEW series.

    // A clean native window is *render ownership evidence only*. Neither
    // this test nor the classifier is an audible ASIO physical FAT.
}
