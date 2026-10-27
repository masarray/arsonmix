// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/PortableWorkspace.h"
#include "../src/NativePresetArchive.h"

namespace storage = armix::storage;
int main()
{
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("ArSonMixPortableWorkspaceP1", "", false);
    assert (temp.createDirectory().wasOk());
    const auto appA = temp.getChildFile ("USB-A");
    const auto appB = temp.getChildFile ("USB-B");
    assert (appA.createDirectory().wasOk());
    assert (!storage::PortableWorkspace::isPortableAt (appA));
    assert (storage::PortableWorkspace::dataRootFor (appA) ==
        storage::PortableWorkspace::installedDataRoot());
    assert (storage::PortableWorkspace::machineDataRoot() ==
        storage::PortableWorkspace::installedDataRoot());
    assert (appA.getChildFile ("portable_mode.txt").replaceWithText ("ARSONMIX_PORTABLE_WORKSPACE_V1"));
    assert (storage::PortableWorkspace::isPortableAt (appA));
    const auto rootA = storage::PortableWorkspace::dataRootFor (appA);
    assert (rootA.createDirectory().wasOk());
    assert (rootA == appA.getChildFile ("Studio"));
    assert (storage::PortableWorkspace::browserDataRootFor (appA) ==
        rootA.getChildFile ("Cache/WebView2"));

    const auto plugin = rootA.getChildFile ("Plugins/VST3/111/MyReverb.vst3");
    assert (plugin.getParentDirectory().createDirectory().wasOk());
    assert (plugin.replaceWithText ("pretend VST3 fixture -- not executable"));
    const auto encoded = storage::PortableWorkspace::encode (rootA, plugin);
    assert (encoded == "workspace://Plugins/VST3/111/MyReverb.vst3");
    assert (storage::PortableWorkspace::decode (rootA, encoded) == plugin);
    const auto outside = temp.getChildFile ("external.vst3");
    assert (storage::PortableWorkspace::encode (rootA, outside) == outside.getFullPathName());

    armix::storage::Snapshot snap;
    snap.name = "Live Portable";
    snap.channelDb = -6.5f;
    snap.masterDb = -2.0f;
    armix::storage::FxState fx;
    fx.name = "MyReverb";
    fx.path = plugin.getFullPathName();
    fx.uid = 7719;
    const char bytes[] = { 1, 0, 7, 2 };
    fx.binary.append (bytes, sizeof bytes);
    snap.effects.push_back (std::move (fx));
    armix::storage::NativePresetArchive archiveA (rootA.getChildFile ("presets"), rootA);
    assert (archiveA.save (snap).isEmpty());

    // Confirm persisted ZIP uses a relative *reference*, not absolute drive C/D/E.
    juce::ZipFile zip (archiveA.fileFor (snap.name));
    const int idx = zip.getIndexOfFileName ("manifest.json");
    assert (idx >= 0);
    const auto manifestStream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (idx));
    assert (manifestStream);
    const auto manifest = manifestStream->readEntireStreamAsString();
    assert (manifest.contains ("workspace://Plugins/VST3/111/MyReverb.vst3"));
    assert (!manifest.contains (appA.getFullPathName()));

    // Simulate backing up entire app and recovering on a new USB drive letter.
    assert (appB.createDirectory().wasOk());
    assert (appB.getChildFile ("portable_mode.txt").replaceWithText ("ARSONMIX_PORTABLE_WORKSPACE_V1"));
    assert (rootA.copyDirectoryTo (appB.getChildFile ("Studio")));
    const auto rootB = storage::PortableWorkspace::dataRootFor (appB);
    armix::storage::NativePresetArchive archiveB (rootB.getChildFile ("presets"), rootB);
    armix::storage::Snapshot loaded;
    assert (archiveB.load (snap.name, loaded).isEmpty());
    const auto relocated = rootB.getChildFile ("Plugins/VST3/111/MyReverb.vst3");
    assert (loaded.effects.size() == 1);
    assert (relocated.existsAsFile() && loaded.effects[0].path == relocated.getFullPathName());
    assert (loaded.effects[0].binary == snap.effects[0].binary);

    // Reject hostile workspace relative paths WITHOUT mutating old snapshot.
    for (const auto& invalid : {
        "workspace://../outside.vst3", "workspace:///absolute.vst3",
        "workspace://Plugins//evil.vst3", "workspace://C:/escape.vst3",
        "workspace://Plugins\\escape.vst3", "workspace://Plugins/./bad.vst3" })
        assert (storage::PortableWorkspace::decode (rootB, invalid) == juce::File());

    // User intentionally 'Load in Place' remains backward compatible.
    assert (storage::PortableWorkspace::decode (rootB, outside.getFullPathName()) == outside);

    assert (temp.deleteRecursively());
    std::cout << "PASS: same executable, portable marker, vendor state, USB relocation, invalid paths and v1 fallback\n";
    return 0;
}
