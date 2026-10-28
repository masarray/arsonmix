// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <filesystem>
#include <functional>
#include <atomic>
#include <memory>
#include <unordered_set>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

// User-directed, versioned, self-contained backup. Deliberately exports to
// NEW folders rather than replacing the only live studio. Worker exclusively;
// no filesystem/hash/third-party VST operations on audio callback.
namespace armix::storage
{
class StudioBackup final
{
public:
    struct Result
    {
        juce::File folder;
        juce::String error;
        int files = 0;
        juce::int64 bytes = 0;
        int externalPluginReferences = 0;
        bool ok() const noexcept { return error.isEmpty() && folder.isDirectory(); }
    };

    static Result create (const juce::File& executable,
                          const juce::File& studio,
                          const juce::File& destinationRoot)
    {
        if (!executable.existsAsFile() || !studio.isDirectory() ||
            !destinationRoot.isDirectory() || linked (executable) ||
            linked (studio) || linked (destinationRoot))
            return { {}, "App, Studio or backup destination is missing" };
        // Do not recurse into the workspace while enumerating it.
        if (destinationRoot == studio || destinationRoot.isAChildOf (studio))
            return { {}, "Choose a backup folder OUTSIDE the Studio workspace" };
        const auto uuid = juce::Uuid().toString().removeCharacters ("-{}");
        const auto tag = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") +
                          "-" + uuid.substring (0, 8);
        const auto staging = destinationRoot.getChildFile (".arsonmix-backup-incomplete-" + uuid);
        const auto complete = destinationRoot.getChildFile ("ArSonMix-Backup-" + tag);
        if (staging.createDirectory().failed())
            return { {}, "Cannot write to the selected backup destination" };
        struct Cleanup final
        {
            juce::File directory;
            bool committed = false;
            ~Cleanup() { if (!committed) directory.deleteRecursively (false); }
        } cleanup { staging };

        auto files = juce::Array<juce::var>();
        juce::int64 total = 0;
        const auto fail = [] (juce::String error) -> Result
        { return { {}, std::move (error) }; };

        auto copyVerify = [&](const juce::File& from, const juce::File& to,
                              const juce::String& relative) -> juce::String
        {
            if (!from.existsAsFile() || linked (from))
                return "Backup rejected missing/linked file: " + relative;
            if (to.getParentDirectory().createDirectory().failed())
                return "Backup destination cannot create directory for: " + relative;
            const auto before = juce::SHA256 (from).toHexString();
            const auto length = from.getSize();
            if (!from.copyFileTo (to) || to.getSize() != length)
                return "Unable to copy " + relative;
            if (!before.equalsIgnoreCase (juce::SHA256 (to).toHexString()) ||
                !before.equalsIgnoreCase (juce::SHA256 (from).toHexString()))
                return "Backup verification failed for " + relative;
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty ("path", relative);
            item->setProperty ("bytes", static_cast<double> (length));
            item->setProperty ("sha256", before);
            files.add (juce::var (item.release()));
            total += length;
            return {};
        };

        auto error = copyVerify (executable,
            staging.getChildFile (executable.getFileName()), executable.getFileName());
        if (error.isNotEmpty()) return fail (error);
        const auto portableMarker = staging.getChildFile ("portable_mode.txt");
        if (!portableMarker.replaceWithText ("ARSONMIX_PORTABLE_WORKSPACE_V1"))
            return fail ("Could not create portable-mode marker in backup");
        // The marker is part of the signed-by-hash manifest, just like the EXE.
        const auto markerLength = portableMarker.getSize();
        auto markerRecord = std::make_unique<juce::DynamicObject>();
        markerRecord->setProperty ("path", "portable_mode.txt");
        markerRecord->setProperty ("bytes", static_cast<double> (markerLength));
        markerRecord->setProperty ("sha256", juce::SHA256 (portableMarker).toHexString());
        files.add (juce::var (markerRecord.release()));
        total += markerLength;

        // Scan the explicit Studio tree, not random installed VST locations.
        // Browser caches and diagnostics are regenerated per machine; vendor
        // snapshots, native presets, plugin catalog and Vault ARE backed up.
        const auto root = StudioBackup::native (studio);
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it (
            root, std::filesystem::directory_options::none, ec), end;
        if (ec) return fail ("Cannot list Studio for backup: " + juce::String (ec.message()));
        for (; it != end; it.increment (ec))
        {
            if (ec) return fail ("Studio enumeration failed: " + juce::String (ec.message()));
            const auto& item = *it;
            const auto rel = item.path().lexically_relative (root);
            if (rel.empty() || rel.is_absolute())
                return fail ("Unsafe Studio entry");
            const auto utf8 = rel.generic_u8string();
            const auto localRel = juce::String::fromUTF8 (
                reinterpret_cast<const char*> (utf8.data()), static_cast<int> (utf8.size()));
            const auto original = studio.getChildFile (localRel);
            const auto status = item.symlink_status (ec);
            if (ec || std::filesystem::is_symlink (status) || linked (original))
                return fail ("Linked/reparse Studio entries cannot be backed up");
            if (it.depth() == 0 &&
                (item.path().filename() == "Cache" || item.path().filename() == "diagnostics"))
            {
                if (std::filesystem::is_directory (status)) it.disable_recursion_pending();
                continue;
            }
            if (std::filesystem::is_directory (status)) continue;
            if (!std::filesystem::is_regular_file (status))
                return fail ("Unsupported special file inside Studio");
            const auto relative = juce::String ("Studio/") + localRel;
            const auto target = staging.getChildFile (relative);
            error = copyVerify (original, target, relative);
            if (error.isNotEmpty()) return fail (error);
        }
        if (ec) return fail ("Studio enumeration failed: " + juce::String (ec.message()));
        // Warn, never falsely claim a 100% portable bundle for a user's
        // deliberate Load-in-Place catalog paths. There may ALSO be external
        // sample libraries not discoverable from the JUCE catalog.
        int external = 0;
        const auto catalog = juce::JSON::parse (studio.getChildFile ("plugin-catalog.json"));
        const auto catalogItems = catalog.getProperty ("items", {});
        if (const auto* list = catalogItems.getArray())
            for (const auto& item : *list)
                if (!item.getProperty ("path", {}).toString().startsWith (
                    PortableWorkspace::scheme))
                    ++external;

        auto manifest = std::make_unique<juce::DynamicObject>();
        manifest->setProperty ("format", "arsonmix.studio-backup");
        manifest->setProperty ("schemaVersion", 1);
        manifest->setProperty ("createdUTC", juce::Time::getCurrentTime().toISO8601 (true));
        manifest->setProperty ("executable", executable.getFileName());
        manifest->setProperty ("dataRoot", "Studio");
        manifest->setProperty ("files", juce::var (files));
        manifest->setProperty ("status", "NATIVE_FILES_VERIFIED__GUI_BROWSER_SCENES_NOT_PORTABLE_YET");
        manifest->setProperty ("externalCatalogPluginReferences", external);
        manifest->setProperty ("warning", "This verifies native files, not third-party activation, "
            "external sample libraries or browser localStorage scenes.");
        if (!staging.getChildFile ("backup-manifest.json").replaceWithText (
            juce::JSON::toString (juce::var (manifest.release()), true)))
            return fail ("Cannot write backup manifest");
        // Validate the written backup itself before publishing. This catches
        // partial files and a broken manifest; a previous good backup survives.
        const auto validation = verify (staging);
        if (validation.isNotEmpty()) return fail (validation);
        // Last step: same-volume atomic rename. Never overwrite an old backup.
        if (complete.exists() || !staging.moveFileTo (complete))
            return fail ("Could not finalize backup folder; previous backups were not touched");
        cleanup.committed = true;
        return { complete, {}, files.size(), total, external };
    }

    // Readback / restore-preflight verifier. Checks every expected relative
    // path, SHA256, byte length, and refuses extras or linked entries. The
    // manifest is a corruption detector, NOT a cryptographic authenticity
    // signature of third-party plugin binaries.
    static juce::String verify (const juce::File& backupFolder)
    {
        if (!backupFolder.isDirectory() || linked (backupFolder))
            return "Backup folder missing or is a link";
        const auto marker = backupFolder.getChildFile ("portable_mode.txt");
        if (!marker.existsAsFile() ||
            marker.loadFileAsString().trim() != "ARSONMIX_PORTABLE_WORKSPACE_V1")
            return "Portable startup marker missing or invalid";
        const auto manifestFile = backupFolder.getChildFile ("backup-manifest.json");
        if (!manifestFile.existsAsFile() || manifestFile.getSize() > 16 * 1024 * 1024)
            return "Backup manifest missing or exceeds integrity limit";
        const auto manifest = juce::JSON::parse (manifestFile);
        if (!manifest.isObject() ||
            manifest.getProperty ("format", {}).toString() != "arsonmix.studio-backup" ||
            static_cast<int> (manifest.getProperty ("schemaVersion", 0)) != 1)
            return "Backup manifest schema is invalid";
        const auto items = manifest.getProperty ("files", {});
        const auto* records = items.getArray();
        if (!records || records->size() < 2)
            return "Backup manifest contains no application and marker";
        std::unordered_set<std::string> names;
        for (const auto& record : *records)
        {
            const auto relative = record.getProperty ("path", {}).toString();
            if (!PortableWorkspace::safeRelativePath (relative))
                return "Unsafe backup manifest file path";
            if (!names.insert (relative.toStdString()).second)
                return "Duplicate file path in backup manifest";
            const auto target = backupFolder.getChildFile (relative);
            if (!target.isAChildOf (backupFolder) || !target.existsAsFile() ||
                linked (target))
                return "Missing or linked backup file: " + relative;
            auto parent = target.getParentDirectory();
            while (parent != backupFolder)
            {
                if (linked (parent) || !parent.isAChildOf (backupFolder))
                    return "Linked or escaping backup folder: " + relative;
                parent = parent.getParentDirectory();
            }
            const auto expected = record.getProperty ("bytes", {});
            const auto expectedHash = record.getProperty ("sha256", {}).toString();
            if (!(expected.isInt() || expected.isInt64() || expected.isDouble()) ||
                static_cast<double> (expected) != static_cast<double> (target.getSize()) ||
                expectedHash.length() != 64 ||
                !juce::SHA256 (target).toHexString().equalsIgnoreCase (expectedHash))
                return "Backup integrity mismatch: " + relative;
        }
        const auto root = native (backupFolder);
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it (
            root, std::filesystem::directory_options::none, ec), end;
        if (ec) return "Cannot audit backup folder";
        for (; it != end; it.increment (ec))
        {
            if (ec) return "Cannot enumerate backup contents";
            const auto status = it->symlink_status (ec);
            if (ec || std::filesystem::is_symlink (status))
                return "Backup contains linked/reparse path";
            if (std::filesystem::is_directory (status)) continue;
            if (!std::filesystem::is_regular_file (status))
                return "Backup contains unsafe special file";
           #if JUCE_WINDOWS
            const auto fileFlags = GetFileAttributesW (it->path().c_str());
            if (fileFlags != INVALID_FILE_ATTRIBUTES &&
                (fileFlags & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                return "Backup contains reparse path";
           #endif
            const auto rel = it->path().lexically_relative (root).generic_u8string();
            const auto name = std::string (reinterpret_cast<const char*> (rel.data()), rel.size());
            if (name != "backup-manifest.json" && names.find (name) == names.end())
                return "Unmanifested file found in backup";
        }
        if (ec) return "Backup enumeration failed";
        if (names.find ("portable_mode.txt") == names.end() ||
            names.find (manifest.getProperty ("executable", {}).toString().toStdString()) == names.end())
            return "Application or portable marker not listed in manifest";
        return {};
    }

    static bool linked (const juce::File& file)
    {
       #if JUCE_WINDOWS
        const auto flags = GetFileAttributesW (file.getFullPathName().toWideCharPointer());
        return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
       #else
        return file.isSymbolicLink();
       #endif
    }

    static std::filesystem::path native (const juce::File& f)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (f.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::path (f.getFullPathName().toRawUTF8());
       #endif
    }

    // Async orchestration owned by ConsoleWindow; destruction blocks until
    // file copying finishes (same shutdown policy as existing VST scanner).
    class Worker final : private juce::Thread
    {
    public:
        Worker() : juce::Thread ("ArSonMix Studio Backup") {}
        ~Worker() override
        {
            alive_->store (false);
            signalThreadShouldExit();
            stopThread (-1);
        }

        bool launch (juce::File exe, juce::File studio, juce::File destination,
                     std::function<void(Result)> onFinished)
        {
            if (isThreadRunning()) return false;
            executable_ = std::move (exe);
            studio_ = std::move (studio);
            destination_ = std::move (destination);
            finished_ = std::move (onFinished);
            return startThread();
        }

    private:
        void run() override
        {
            auto outcome = StudioBackup::create (executable_, studio_, destination_);
            auto alive = alive_;
            auto callback = finished_;
            juce::MessageManager::callAsync (
                [alive, callback = std::move (callback), result = std::move (outcome)]() mutable
                {
                    if (alive->load() && callback) callback (std::move (result));
                });
        }

        juce::File executable_, studio_, destination_;
        std::function<void(Result)> finished_;
        std::shared_ptr<std::atomic<bool>> alive_ =
            std::make_shared<std::atomic<bool>> (true);
    };
};
} // namespace armix::storage
