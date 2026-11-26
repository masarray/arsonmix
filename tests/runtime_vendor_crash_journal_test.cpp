// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/RuntimeVendorCrashJournal.h"
#include <cassert>
#include <cstdlib>
#include <iostream>

using Journal=armix::startup::RuntimeVendorCrashJournal;

int main(int argc,char** argv) {
    // Real process termination: no destructor, no cleanup, no simulated catch.
    if(argc==3 && juce::String::fromUTF8(argv[1])=="--interrupt") {
        Journal journal(juce::File(juce::String::fromUTF8(argv[2])));
        Journal::Operation op(journal,"Fixture.vst3","CREATE");
        if(!op)return 2;
        std::_Exit(77);
    }
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixVendorJournal","",false);
    assert(root.createDirectory().wasOk());
    const auto session=root.getChildFile("last.smix");
    assert(session.replaceWithText("PRESERVE"));
    const auto exe=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::ChildProcess child;
    assert(child.start(juce::StringArray{exe.getFullPathName(),"--interrupt",root.getFullPathName()},0));
    assert(child.waitForProcessToFinish(10000));
    assert(child.getExitCode()==77);
    {
        Journal next(root);
        assert(next.recoveryRequired());
        assert(next.previousOperation().contains("CREATE"));
        assert(!next.finishCleanExit()); // safe-mode launch alone cannot clear
        assert(session.loadFileAsString()=="PRESERVE");
        {
            Journal::Operation retry(next,"Fixture.vst3","EXPLICIT_RETRY");
            assert(retry);
            Journal::Operation nested(next,"Other.vst3","NESTED");
            assert(!nested);
            assert(!next.finishCleanExit());
        }
        assert(next.recoveryRequired()); // controlled retry is not approval
        assert(!next.finishCleanExit());
    }
    {
        Journal explicitOpen(root);
        assert(explicitOpen.recoveryRequired());
        explicitOpen.acknowledgeExplicitRecovery();
        assert(!explicitOpen.recoveryRequired());
        assert(explicitOpen.finishCleanExit());
    }
    {
        Journal normal(root);
        assert(!normal.recoveryRequired());
        {
            Journal::Operation operation(normal,"Fixture.vst3","PREPARE");
            assert(operation);
        }
        // Even after the constructor succeeds, later DSP/editor/teardown
        // crashes leave the lifetime marker. Only full clean exit clears it.
        assert(Journal(root).recoveryRequired());
        assert(normal.finishCleanExit());
        assert(!Journal(root).recoveryRequired());
    }
    // Marker path conflict is fail-closed, preserving all session bytes.
    assert(root.getChildFile("vendor-control.pending").createDirectory().wasOk());
    Journal blocked(root);
    Journal::Operation rejected(blocked,"Fixture.vst3","CREATE");
    assert(!rejected);
    assert(!blocked.finishCleanExit());
    assert(session.loadFileAsString()=="PRESERVE");
    assert(root.deleteRecursively());
    std::cout<<"ZBSH09_RUNTIME_CRASH_JOURNAL_PASS\n";
}
