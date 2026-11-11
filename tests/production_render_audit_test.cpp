// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/core/ProductionRenderAudit.h"
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

    // A clean native window is *render ownership evidence only*. Neither
    // this test nor the classifier is an audible ASIO physical FAT.
}
