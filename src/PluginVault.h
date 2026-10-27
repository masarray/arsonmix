// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include <filesystem>
#include <system_error>

// Explicitly selected VST3 file or bundle -> private user Vault. This is
// never used in the AudioIODeviceCallback. No blanket disk scans and no
// permissions/vendor whitelist. The only restrictions guard data integrity.
namespace armix::storage
{
struct PluginVault final
{
    struct Result
    {
        juce::File plugin;
        juce::String error;
        bool copied = false;
        bool ok() const noexcept { return error.isEmpty() && plugin != juce::File(); }
    };

    static std::filesystem::path native (const juce::File& file)
    {
       #if JUCE_WINDOWS
        return std::filesystem::path (file.getFullPathName().toWideCharPointer());
       #else
        return std::filesystem::path (file.getFullPathName().toRawUTF8());
       #endif
    }

    static Result importSelected (const juce::File& selected, const juce::File& workspace)
    {
        if (!selected.hasFileExtension ("vst3") ||
            (!selected.isDirectory() && !selected.existsAsFile()))
            return { {}, "Choose a real .vst3 file or folder bundle", false };

        const auto root = PortableWorkspace::vaultRoot (workspace);
        if (selected.isAChildOf (root))
            return { selected, {}, false }; // import already managed; no duplicate

        if (root.createDirectory().failed())
            return { {}, "Cannot create Studio/Plugins/VST3 (check disk permissions)", false };

        // Isolate each module in its own GUID directory to preserve the original
        // VST3 bundle/binary filename, support coexisting variants/versions and
        // never silently overwrite the user's previously imported VST.
        const auto id = juce::Uuid().toString().removeCharacters ("-{}");
        const auto staging = root.getChildFile (".incoming-" + id);
        const auto finalFolder = root.getChildFile (id);
        if (staging.createDirectory().failed())
            return { {}, "Cannot stage plugin import in workspace", false };

        struct Rollback final
        {
            juce::File folder;
            bool committed = false;
            ~Rollback() { if (!committed) folder.deleteRecursively(); }
        } rollback { staging };
        const auto stagedPlugin = staging.getChildFile (selected.getFileName());

        const auto fail = [] (juce::String why) -> Result
        { return { {}, std::move (why), false }; };

        // Block symbolic-link/reparse point traversal; do not dereference
        // arbitrary external files while copying a selected VST3 bundle.
        std::error_code ec;
        const auto from = native (selected);
        auto type = std::filesystem::symlink_status (from, ec);
        if (ec || std::filesystem::is_symlink (type))
            return fail ("Plugin source is inaccessible or a link");
        const auto to = native (stagedPlugin);

        if (selected.existsAsFile())
        {
            if (!std::filesystem::is_regular_file (type))
                return fail ("Unsupported VST3 file type");
            std::filesystem::copy_file (from, to, std::filesystem::copy_options::none, ec);
            if (ec) return fail ("Cannot copy VST3 file: " + juce::String (ec.message()));
        }
        else
        {
            if (!std::filesystem::is_directory (type))
                return fail ("Unsupported VST3 bundle source");
            std::filesystem::create_directory (to, ec);
            if (ec) return fail ("Cannot stage VST3 bundle: " + juce::String (ec.message()));
            std::filesystem::recursive_directory_iterator it (from,
                std::filesystem::directory_options::none, ec), end;
            if (ec) return fail ("Cannot enumerate VST3 bundle: " + juce::String (ec.message()));
            for (; it != end; it.increment (ec))
            {
                if (ec) return fail ("VST3 bundle enumeration failed: " + juce::String (ec.message()));
                const auto status = it->symlink_status (ec);
                if (ec || std::filesystem::is_symlink (status))
                    return fail ("VST3 bundle contains an external symlink or junction");
                const auto relative = it->path().lexically_relative (from);
                if (relative.empty() || relative.is_absolute())
                    return fail ("Invalid relative entry in VST3 bundle");
                const auto destination = to / relative;
                if (std::filesystem::is_directory (status))
                    std::filesystem::create_directory (destination, ec);
                else if (std::filesystem::is_regular_file (status))
                    std::filesystem::copy_file (it->path(), destination,
                        std::filesystem::copy_options::none, ec);
                else
                    return fail ("Unsupported special file inside VST3 bundle");
                if (ec) return fail ("Cannot stage VST3 bundle content: " + juce::String (ec.message()));
            }
            if (ec) return fail ("VST3 bundle enumeration failed: " + juce::String (ec.message()));
        }

        // Atomic directory rename within the same filesystem. Never publish
        // partial copies in the index. Scanner will inspect the FINAL path.
        if (!staging.moveFileTo (finalFolder))
            return fail ("Could not commit VST3 bundle; old library unchanged");
        rollback.committed = true;
        return { finalFolder.getChildFile (selected.getFileName()), {}, true };
    }
};
} // namespace armix::storage
