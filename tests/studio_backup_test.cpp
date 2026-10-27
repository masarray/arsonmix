// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/StudioBackup.h"

int main()
{
    namespace st = armix::storage;
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("ArSonMixVerifiedBackupUnit", "", false);
    assert (temp.createDirectory().wasOk());
    const auto app = temp.getChildFile ("App");
    const auto destination = temp.getChildFile ("USB Backups");
    assert (app.createDirectory().wasOk());
    assert (destination.createDirectory().wasOk());
    const auto exe = app.getChildFile ("ArSonMix.exe");
    assert (exe.replaceWithText ("fake testing executable -- not a real PE"));
    const auto studio = app.getChildFile ("Studio");
    const auto plugin = studio.getChildFile ("Plugins/VST3/unique/My Verb.vst3");
    const auto sound = studio.getChildFile ("presets/Vocal Live.armix");
    const auto cache = studio.getChildFile ("Cache/WebView2/deletable-cache");
    assert (plugin.getParentDirectory().createDirectory().wasOk());
    assert (sound.getParentDirectory().createDirectory().wasOk());
    assert (cache.getParentDirectory().createDirectory().wasOk());
    assert (plugin.replaceWithText ("Fake bundled FX bytes"));
    assert (sound.replaceWithText ("vendor state stub"));
    assert (cache.replaceWithText ("DO NOT BACKUP CACHE"));
    assert (studio.getChildFile ("plugin-catalog.json").replaceWithText ("{\"items\":[]}"));

    assert (!st::StudioBackup::create (exe, studio, studio).ok()); // no recursion into source
    const auto copy = st::StudioBackup::create (exe, studio, destination);
    if (!copy.ok()) std::cerr << copy.error << "\n";
    assert (copy.ok() && copy.files >= 4);
    assert (copy.folder.isAChildOf (destination));
    assert (copy.folder.getChildFile ("ArSonMix.exe").existsAsFile());
    assert (copy.folder.getChildFile ("portable_mode.txt").existsAsFile());
    assert (copy.folder.getChildFile ("Studio/Plugins/VST3/unique/My Verb.vst3")
                .loadFileAsString() == plugin.loadFileAsString());
    assert (copy.folder.getChildFile ("Studio/presets/Vocal Live.armix")
                .loadFileAsString() == sound.loadFileAsString());
    assert (!copy.folder.getChildFile ("Studio/Cache/WebView2/deletable-cache").exists());
    const auto manifest = juce::JSON::parse (
        copy.folder.getChildFile ("backup-manifest.json").loadFileAsString());
    assert (manifest.isObject());
    assert (manifest.getProperty ("format", {}).toString() == "arsonmix.studio-backup");
    const auto records = manifest.getProperty ("files", {});
    assert (records.isArray() && records.getArray()->size() >= 4);
    for (const auto& entry : *records.getArray())
    {
        const auto file = copy.folder.getChildFile (entry.getProperty ("path", {}).toString());
        assert (file.existsAsFile());
        assert (juce::SHA256 (file).toHexString().equalsIgnoreCase (
            entry.getProperty ("sha256", {}).toString()));
    }
    // This is immediately runnable as a portable workspace on a new PC.
    assert (st::PortableWorkspace::dataRootFor (copy.folder) ==
        copy.folder.getChildFile ("Studio"));
    assert (plugin.existsAsFile() && sound.existsAsFile()); // never delete source
    const auto again = st::StudioBackup::create (exe, studio, destination);
    assert (again.ok() && again.folder != copy.folder); // no overwriting old backups
    assert (temp.deleteRecursively());
    std::cout << "PASS: one-folder USB backup, native presets and plugins, verified SHA, no overwrite, runtime cache excluded\n";
}
