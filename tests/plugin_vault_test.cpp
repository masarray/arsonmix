// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/PluginVault.h"

int main()
{
    namespace st = armix::storage;
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("ArSonMixVstVaultUnit", "", false);
    assert (temp.createDirectory().wasOk());
    const auto workspace = temp.getChildFile ("Studio");
    assert (workspace.createDirectory().wasOk());
    const auto sourceDir = temp.getChildFile ("External Plugins");
    assert (sourceDir.createDirectory().wasOk());
    auto a = sourceDir.getChildFile ("My EQ.vst3");
    assert (a.replaceWithText ("fake simple VST3 test fixture"));
    auto importedA = st::PluginVault::importSelected (a, workspace);
    assert (importedA.ok() && importedA.copied);
    assert (importedA.plugin.isAChildOf (st::PortableWorkspace::vaultRoot (workspace)));
    assert (importedA.plugin.getFileName() == "My EQ.vst3");
    assert (importedA.plugin.loadFileAsString() == a.loadFileAsString());
    assert (a.existsAsFile()); // NEVER delete user source VST
    const auto once = st::PluginVault::importSelected (importedA.plugin, workspace);
    assert (once.ok() && !once.copied && once.plugin == importedA.plugin);
    const auto duplicate = st::PluginVault::importSelected (a, workspace);
    assert (duplicate.ok() && duplicate.copied && duplicate.plugin != importedA.plugin);
    assert (importedA.plugin.existsAsFile()); // new import does not overwrite old

    const auto bundle = sourceDir.getChildFile ("MyVerb.vst3");
    const auto bin = bundle.getChildFile ("Contents/x86_64-win/MyVerb.vst3");
    const auto data = bundle.getChildFile ("Contents/Resources/moduleinfo.json");
    assert (bin.getParentDirectory().createDirectory().wasOk());
    assert (data.getParentDirectory().createDirectory().wasOk());
    assert (bin.replaceWithText ("fake Windows DLL"));
    assert (data.replaceWithText ("{\"name\":\"MyVerb\"}"));
    const auto importedBundle = st::PluginVault::importSelected (bundle, workspace);
    assert (importedBundle.ok() && importedBundle.copied);
    assert (importedBundle.plugin.isDirectory());
    assert (importedBundle.plugin.getFileName() == "MyVerb.vst3");
    assert (importedBundle.plugin.getChildFile ("Contents/x86_64-win/MyVerb.vst3")
        .loadFileAsString() == bin.loadFileAsString());
    assert (importedBundle.plugin.getChildFile ("Contents/Resources/moduleinfo.json")
        .loadFileAsString() == data.loadFileAsString());
    const auto portableRef = st::PortableWorkspace::encode (workspace, importedBundle.plugin);
    assert (portableRef.startsWith ("workspace://Plugins/VST3/"));
    assert (st::PortableWorkspace::decode (workspace, portableRef) == importedBundle.plugin);
    assert (!st::PluginVault::importSelected (sourceDir.getChildFile ("bad.dll"), workspace).ok());

    // Simulate a removable drive moved onto an unrelated machine.
    const auto otherRoot = temp.getChildFile ("Restored USB/Studio");
    assert (workspace.copyDirectoryTo (otherRoot));
    const auto resolved = st::PortableWorkspace::decode (otherRoot, portableRef);
    assert (resolved.isDirectory());
    assert (resolved.getChildFile ("Contents/Resources/moduleinfo.json").existsAsFile());
    assert (temp.deleteRecursively());
    std::cout << "PASS: selected VST3 single-file and full bundle copied, version coexistence, USB portability\n";
}
