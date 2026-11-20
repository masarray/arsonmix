// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "NativeMixerSessionStore.h"

namespace armix::session {
// Control-thread policy shared by startup and device-apply recovery. The host
// still owns the physical restore, output authorization and notifications.
class NativeMixerSessionRecovery final {
public:
    using BindingMode = MixerDeviceBindingPolicy::Mode;

    enum class Source { retained, head, previous, missing };

    struct ReadResult {
        Source source;
        juce::String error;
    };

    template <class ReadRetained>
    static ReadResult read(const NativeMixerSessionStore& store, bool retained,
                           MixerSessionDocument& doc,
                           std::vector<armix::shows::BinaryState>& states,
                           ReadRetained&& readRetained) {
        if(retained)
            return {Source::retained,readRetained(doc,states)};
        if(!store.exists())
            return {Source::missing,"No saved dynamic mixer session"};

        auto error=store.load(doc,states);
        if(error.isNotEmpty() && store.recoverPrevious(doc,states).isEmpty())
            return {Source::previous,{}};
        return {Source::head,std::move(error)};
    }


    // One CONTROL-thread admission policy for .smix SAVE/OPEN. The host
    // supplies observed native authority and keeps OS dialogs, callbacks,
    // physical graph ownership and UI ACK/readback; no shadow session state.
    enum class SaveAction { reject, unchanged, exportLogical, captureInstalled };
    struct SaveContext {
        bool automatic = false;
        bool workerBusy = false;
        bool showBusy = false;
        bool backupBusy = false;
        bool catalogBusy = false;
        bool graphPending = false;
        bool readOnly = false;
        bool channelIdInstalled = false;
        bool m4Installed = false;
        bool secondaryArmed = false;
        bool retainedNative = false;
        bool recoveryPending = false;
        bool shutdownCheckpoint = false;
        bool physicalOpen = false;
        bool directAuthorized = false;
        bool mainInterrupted = false;
    };
    struct SavePlan {
        SaveAction action;
        const char* error = nullptr;
    };
    static SavePlan planSave(const SaveContext& c) noexcept {
        if(c.workerBusy || c.showBusy || c.backupBusy ||
           c.catalogBusy || c.graphPending)
            return {SaveAction::reject,
                    "Mixer is busy. Try again after the current operation."};
        if(c.readOnly)
            return {SaveAction::reject,
                    "Saved mixer recovery required before writing a new session"};
        if(!c.channelIdInstalled && !c.m4Installed && !c.secondaryArmed) {
            if(c.retainedNative)
                return {SaveAction::reject, "Retained native mixer still owns the session"};
            return {c.automatic ? SaveAction::unchanged : SaveAction::exportLogical, nullptr};
        }
        if(c.automatic && c.recoveryPending)
            return {SaveAction::reject,
                    "The retained mixer checkpoint is awaiting a verified device"};
        if(c.automatic && !c.shutdownCheckpoint &&
           (!c.physicalOpen || !c.directAuthorized || c.mainInterrupted))
            return {SaveAction::reject,
                    "Automatic checkpoint paused until audio device recovery"};
        if(c.secondaryArmed)
            return {SaveAction::reject, "Stop STREAM before saving the mixer"};
        return {SaveAction::captureInstalled, nullptr};
    }

    enum class OpenAction { reject, openLogical, restoreInstalled };
    struct OpenContext {
        bool operationBusy = false;
        bool epochChanged = false;
        bool secondaryArmed = false;
        bool channelIdInstalled = false;
        bool m4Installed = false;
        bool retainedNative = false;
    };
    struct OpenPlan {
        OpenAction action;
        const char* error = nullptr;
    };
    static OpenPlan planOpen(const OpenContext& c) noexcept {
        if(c.operationBusy || c.epochChanged)
            return {OpenAction::reject,
                    "Mixer or audio device changed while opening the file"};
        if(c.secondaryArmed)
            return {OpenAction::reject, "Stop STREAM before opening a mixer"};
        if(!c.channelIdInstalled && !c.m4Installed && c.retainedNative)
            return {OpenAction::reject, "Retained native mixer still owns the session"};
        return {c.channelIdInstalled || c.m4Installed ?
                OpenAction::restoreInstalled : OpenAction::openLogical, nullptr};
    }

    struct Context {
        BindingMode bindingMode;
        Source source;
        bool startupOnSavedDevice;
        bool recoveryPending;
        bool nativeAlreadyActive;
        bool pristineStartup;
        bool firstBinding;
    };

    struct Plan {
        BindingMode bindingMode;
        bool replacePristineStartup;
        bool restoreAllowed;
        bool resumeMain;
    };

    static Plan plan(const MixerSessionDocument& doc, const Context& context) {
        auto mode=context.bindingMode;
        if(mode==BindingMode::exact && context.startupOnSavedDevice &&
           MixerDeviceBindingPolicy::isAsio4All(doc.binding))
            mode=BindingMode::verifiedAsioRestart;

        const bool recoveringAfterDeviceApply=mode!=BindingMode::exact &&
            context.recoveryPending;
        const bool fromDisk=context.source==Source::head ||
            context.source==Source::previous;
        // Only an unmodified starter can be replaced. Previous-gen recovery
        // can restore that graph, but may never inherit MAIN auto-resume.
        const bool replace=context.nativeAlreadyActive && fromDisk &&
            (context.startupOnSavedDevice || recoveringAfterDeviceApply || context.firstBinding) &&
            context.pristineStartup;
        const bool restoreAllowed=!context.nativeAlreadyActive || replace;
        const bool resume=restoreAllowed && !context.firstBinding && context.startupOnSavedDevice &&
            (mode==BindingMode::exact || mode==BindingMode::verifiedAsioRestart) &&
            context.source==Source::head && doc.resumeMain.value_or(true);
        return {mode,replace,restoreAllowed,resume};
    }
};
} // namespace armix::session
