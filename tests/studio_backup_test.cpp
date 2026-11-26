// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/StudioBackup.h"
#if !JUCE_WINDOWS
 #include <sys/stat.h>
#endif

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
    // Native P4 Shows stay inside Studio and must be in verified USB backup.
    const auto showPointer = studio.getChildFile (
        "Shows/11111111-2222-4333-8444-555555555555/HEAD");
    assert (showPointer.getParentDirectory().createDirectory().wasOk());
    assert (showPointer.replaceWithText ("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"));

    assert (studio.getChildFile ("plugin-catalog.json").replaceWithText ("{\"items\":[]}"));

    assert (!st::StudioBackup::create (exe, studio, studio).ok()); // no recursion into source
    const auto copy = st::StudioBackup::create (exe, studio, destination);
    if (!copy.ok()) std::cerr << copy.error << "\n";
    assert (copy.ok() && copy.files >= 5);
    assert (st::StudioBackup::verify (copy.folder).isEmpty());
    assert (copy.folder.isAChildOf (destination));
    assert (copy.folder.getChildFile ("ArSonMix.exe").existsAsFile());
    assert (copy.folder.getChildFile ("portable_mode.txt").existsAsFile());
    assert (copy.folder.getChildFile ("Studio/Plugins/VST3/unique/My Verb.vst3")
                .loadFileAsString() == plugin.loadFileAsString());
    assert (copy.folder.getChildFile ("Studio/presets/Vocal Live.armix")
                .loadFileAsString() == sound.loadFileAsString());
    assert (copy.folder.getChildFile (
        "Studio/Shows/11111111-2222-4333-8444-555555555555/HEAD")
        .loadFileAsString() == showPointer.loadFileAsString());
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
    // Detect post-backup corruption and undeclared content without deleting
    // the last good backup. Then construct another valid backup independently.
    assert (copy.folder.getChildFile ("Studio/presets/Vocal Live.armix").appendText ("corrupt"));
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (copy.folder.getChildFile ("Studio/presets/Vocal Live.armix").replaceWithText (
        sound.loadFileAsString()));
    assert (st::StudioBackup::verify (copy.folder).isEmpty());
    assert (copy.folder.getChildFile ("INJECTED.vst3").replaceWithText ("extra unmanaged payload"));
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (copy.folder.getChildFile ("INJECTED.vst3").deleteFile());
    assert (st::StudioBackup::verify (copy.folder).isEmpty());

    // Manifest and marker are untrusted input, including linked paths and malformed bytes.
    const auto manifestFile = copy.folder.getChildFile ("backup-manifest.json");
    const auto manifestText = manifestFile.loadFileAsString();
    assert (manifestFile.replaceWithText (juce::String::repeatedString ("[", 65) +
        juce::String::repeatedString ("]", 65)));
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (manifestFile.replaceWithText (manifestText));
    auto bad = juce::JSON::parse (manifestText);
    bad.getDynamicObject()->setProperty ("executable", "portable_mode.txt");
    assert (manifestFile.replaceWithText (juce::JSON::toString (bad)));
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (manifestFile.replaceWithText (manifestText));
    const auto extraDir = copy.folder.getChildFile ("extra-empty");
    assert (extraDir.createDirectory().wasOk());
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (extraDir.deleteFile());
    assert (st::StudioBackup::verify (copy.folder).isEmpty());
   #if !JUCE_WINDOWS
    // A FIFO must be rejected BEFORE any stream read (which could block forever).
    assert (manifestFile.deleteFile());
    assert (::mkfifo (manifestFile.getFullPathName().toRawUTF8(), 0600) == 0);
    assert (st::StudioBackup::verify (copy.folder).isNotEmpty());
    assert (manifestFile.deleteFile());
    assert (manifestFile.replaceWithText (manifestText));
   #endif
    // Cold portable recovery drill: verify original, copy into a NEW location,
    // verify the copy before use; no live workspace replacement or execution.
    const auto recovery = temp.getChildFile ("Recovered Studio");
    assert (copy.folder.copyDirectoryTo (recovery));
    assert (st::StudioBackup::verify (recovery).isEmpty());
    assert (recovery.getChildFile ("Studio/presets/Vocal Live.armix").loadFileAsString() == sound.loadFileAsString());
    assert (st::StudioBackup::verify (copy.folder).isEmpty());

    // This is immediately runnable as a portable workspace on a new PC.
    assert (st::PortableWorkspace::dataRootFor (copy.folder) ==
        copy.folder.getChildFile ("Studio"));
    assert (plugin.existsAsFile() && sound.existsAsFile()); // never delete source
    const auto again = st::StudioBackup::create (exe, studio, destination);
    assert (again.ok() && again.folder != copy.folder);
    assert (st::StudioBackup::verify (again.folder).isEmpty()); // no overwriting old backups
    assert (temp.deleteRecursively());
    std::cout << "PASS: one-folder USB backup, native presets and plugins, verified SHA, no overwrite, runtime cache excluded\n";
}
