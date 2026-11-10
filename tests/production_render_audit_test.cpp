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
}
