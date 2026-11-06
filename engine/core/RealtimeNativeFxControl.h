// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Optional native FX control mailbox hook. The existing FxInstance invokes
// this on the SAME audio callback before its bypass fast path; the callback
// may drain one bounded edit even when an insert is fully bypassed.
// No allocation, lock, dynamic cast, UI or second renderer in this interface.
namespace armix::mixer {
struct RealtimeNativeFxControl {
    virtual ~RealtimeNativeFxControl() = default;
    virtual void consumeRealtimeEdits() noexcept = 0;
};
} // namespace armix::mixer
