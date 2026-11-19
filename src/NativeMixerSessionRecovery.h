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
