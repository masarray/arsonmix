// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>

namespace armix::startup {
// A process crash inside a third-party VST3 DLL cannot be caught by JUCE or
// C++ exception handlers. Mark automatic FX restore BEFORE loading vendor code.
// An interrupted prior startup must NOT replay that same potentially crashing
// session forever. Keep .smix and plugin state untouched; manual Open remains
// available after diagnosing the vendor. Only CONTROL thread touches this file.
class Vst3RestoreCrashGuard final {
public:
    explicit Vst3RestoreCrashGuard(const juce::File& settingsRoot)
        : marker_(settingsRoot.getChildFile("startup-vst3-restore.pending")) {}

    [[nodiscard]] bool interrupted() const { return marker_.existsAsFile(); }

    [[nodiscard]] bool arm() {
        if (interrupted()) return false;  // never overwrite crash evidence
        return marker_.replaceWithText("RESTORE_IN_PROGRESS\n");
    }

    // Manual Open is an explicit user's retry after safe recovery. Preserve
    // crash evidence if the process dies again; clear only after the newly
    // installed FX graph is stable. A writable marker is mandatory.
    [[nodiscard]] bool armExplicitRetry() {
        return marker_.replaceWithText("EXPLICIT_FX_RESTORE_IN_PROGRESS\n");
    }

    [[nodiscard]] bool complete() {
        // Called after stable startup or graceful shutdown, never while
        // processor construction is still underway.
        return !marker_.existsAsFile() || marker_.deleteFile();
    }
private:
    juce::File marker_;
};
} // namespace armix::startup
