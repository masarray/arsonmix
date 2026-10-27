// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>
#include <filesystem>
#include <functional>
#include <atomic>
#include <memory>

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
        bool ok() const noexcept { return error.isEmpty() && folder.isDirectory(); }
    };

    static Result create (const juce::File& executable,
                          const juce::File& studio,
                          const juce::File& destinationRoot)
    {
        if (!executable.existsAsFile() || !studio.isDirectory() ||
            !destinationRoot.isDirectory())
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
            if (!from.existsAsFile() || from.isSymbolicLink())
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
        if (!staging.getChildFile ("portable_mode.txt").replaceWithText ("ARSONMIX_PORTABLE_WORKSPACE_V1"))
            return fail ("Could not create portable-mode marker in backup");

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
            const auto original = studio.getChildFile (juce::String (rel.generic_u8string().c_str()));
            const auto status = item.symlink_status (ec);
            if (ec || std::filesystem::is_symlink (status))
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
            const auto relative = juce::String ("Studio/") +
                juce::String (rel.generic_u8string().c_str());
            const auto target = staging.getChildFile (relative);
            error = copyVerify (original, target, relative);
            if (error.isNotEmpty()) return fail (error);
        }
        if (ec) return fail ("Studio enumeration failed: " + juce::String (ec.message()));
        auto manifest = std::make_unique<juce::DynamicObject>();
        manifest->setProperty ("format", "arsonmix.studio-backup");
        manifest->setProperty ("schemaVersion", 1);
        manifest->setProperty ("createdUTC", juce::Time::getCurrentTime().toISO8601 (true));
        manifest->setProperty ("executable", executable.getFileName());
        manifest->setProperty ("dataRoot", "Studio");
        manifest->setProperty ("files", juce::var (files));
        manifest->setProperty ("status", "VERIFIED_NATIVE_USER_FILES__WEBVIEW_CACHE_EXCLUDED");
        if (!staging.getChildFile ("backup-manifest.json").replaceWithText (
            juce::JSON::toString (juce::var (manifest.release()), true)))
            return fail ("Cannot write backup manifest");
        // Last step: same-volume atomic rename. Never overwrite an old backup.
        if (complete.exists() || !staging.moveFileTo (complete))
            return fail ("Could not finalize backup folder; previous backups were not touched");
        cleanup.committed = true;
        return { complete, {}, files.size(), total };
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
