// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include <juce_core/juce_core.h>

namespace armix::startup {
// CONTROL / message thread only. Never call this from a PCM callback.
// Evidence of an unclean lifetime, not proof that a plugin caused a crash.
// Keep the journal armed until all vendor objects and devices are destroyed.
class RuntimeVendorCrashJournal final {
public:
    explicit RuntimeVendorCrashJournal(const juce::File& root)
        : root_(root), lifetime_(root.getChildFile("vendor-runtime.pending")),
          operation_(root.getChildFile("vendor-control.pending")),
          recovery_(lifetime_.exists() || operation_.exists() ||
                    lifetime_.isSymbolicLink() || operation_.isSymbolicLink()) {
        if(recovery_ && operation_.existsAsFile() && !operation_.isSymbolicLink() &&
           operation_.getSize()>0 && operation_.getSize()<=512)
            previous_=operation_.loadFileAsString().substring(0,256)
                .removeCharacters("\r\n\t");
    }

    static RuntimeVendorCrashJournal& process() {
        static RuntimeVendorCrashJournal journal(storage::PortableWorkspace::dataRoot());
        return journal;
    }
    bool recoveryRequired() const noexcept { return recovery_; }
    const juce::String& previousOperation() const noexcept { return previous_; }

    // Called only by a successful deliberate physical .smix Open. Merely
    // opening the application, trying a vendor, or waiting is not consent.
    void acknowledgeExplicitRecovery() noexcept { recovery_=false; }

    class Operation final {
    public:
        Operation(RuntimeVendorCrashJournal& journal,const juce::String& module,
                  const char* phase)
            : owner_(journal), entered_(journal.enter(module,phase)) {}
        ~Operation() { if(entered_)owner_.leave(); }
        Operation(const Operation&)=delete;
        Operation& operator=(const Operation&)=delete;
        explicit operator bool() const noexcept { return entered_; }
    private:
        RuntimeVendorCrashJournal& owner_;
        bool entered_=false;
    };

    // Called after window.reset(), not after a timer or device.stop() alone.
    // On a crash, process termination bypasses this method. An unresolved
    // prior fault survives an otherwise clean safe-mode visit.
    bool finishCleanExit() {
        if(active_||recovery_||ioFailure_)return false;
        if(!armed_ && !lifetime_.exists() && !operation_.exists())return true;
        if(!safePaths())return false;
        if(operation_.exists()&&!operation_.deleteFile())return false;
        return !lifetime_.exists()||lifetime_.deleteFile();
    }

private:
    juce::File root_, lifetime_, operation_;
    bool recovery_=false, armed_=false, active_=false, ioFailure_=false;
    juce::String previous_;

    bool safePaths() const {
        return !root_.isSymbolicLink()&&!lifetime_.isSymbolicLink()&&
            !operation_.isSymbolicLink()&&!lifetime_.isDirectory()&&!operation_.isDirectory();
    }
    bool enter(const juce::String& module,const char* phase) {
        // Modal vendor construction may pump the message loop. Refuse
        // nested construction, not a blocking mutex or recursive overwrite.
        if(active_||ioFailure_)return false;
        if(!safePaths()||root_.createDirectory().failed())return false;
        if(!armed_) {
            if(!lifetime_.replaceWithText("VENDOR_LIFETIME_IN_PROGRESS\n"))return false;
            armed_=true;
        }
        // Basename only, no personal workspace path or vendor state.
        auto label=juce::File::isAbsolutePath(module)
            ?juce::File(module).getFileName():juce::String("VST3");
        label=label.substring(0,160).removeCharacters("\r\n\t");
        if(!operation_.replaceWithText(juce::String(phase)+": "+label+"\n")) {
            ioFailure_=true;
            return false;
        }
        active_=true;
        return true;
    }
    void leave() {
        // Controlled returns (including C++ exception unwinding) are not
        // native process crashes. The lifetime marker remains until exit.
        if(!operation_.deleteFile())ioFailure_=true;
        active_=false;
    }
};
} // namespace armix::startup
