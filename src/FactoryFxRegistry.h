// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <set>
#include <vector>

// Control/background thread ONLY. Factory modules are app-managed, never user Vault.
// This parser intentionally rejects an incomplete or modified manifest as a WHOLE:
// a partial load must not make a damaged six-FX package appear healthy.
namespace armix::factory
{
struct Candidate
{
    juce::String productId, path;
    int hostUniqueId = 0;
};

struct Discovery
{
    std::vector<Candidate> candidates;
    juce::String error;
    bool ok() const noexcept { return error.isEmpty(); }
};

struct Registry final
{
    static juce::File executableRoot()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getParentDirectory();
    }

    static juce::File manifestAt (const juce::File& root)
    {
        return root.getChildFile ("FactoryFX/factoryfx-manifest.json");
    }

    static bool hex (const juce::String& s, int size)
    {
        if (s.length() != size) return false;
        for (auto c : s)
            if (juce::CharacterFunctions::getHexDigitValue (c) < 0) return false;
        return true;
    }

    // Strict ASCII relative paths. No absolute paths, backslashes, drive prefixes,
    // empty components or dot segments. The package generator owns filenames.
    static bool safePath (const juce::String& s)
    {
        if (s.isEmpty() || s.startsWithChar ('/') || s.contains ("..") ||
            s.contains ("//") || s.containsChar ('\\'))
            return false;
        for (auto c : s)
            if (! ((juce::CharacterFunctions::isAscii (c) && juce::CharacterFunctions::isLetterOrDigit (c)) ||
                   c == '/' || c == '_' || c == '-' || c == '.'))
                return false;
        return true;
    }

    // Checks the resolved filesystem path as well as lexical safety: symlinks and
    // Windows junctions escaping the app folder are never accepted.
    static bool contained (const juce::File& root, const juce::File& file)
    {
        std::error_code ec;
       #if JUCE_WINDOWS
        const std::filesystem::path base (root.getFullPathName().toWideCharPointer());
        const std::filesystem::path target (file.getFullPathName().toWideCharPointer());
       #else
        const std::filesystem::path base (root.getFullPathName().toStdString());
        const std::filesystem::path target (file.getFullPathName().toStdString());
       #endif
        const auto relative = std::filesystem::relative (target, base, ec);
        if (ec || relative.empty() || relative.is_absolute()) return false;
        for (const auto& part : relative)
            if (part == "..") return false;
        return true;
    }

    static Discovery discover (const juce::File& appRoot)
    {
        Discovery result;
        auto fail = [&result] (const juce::String& reason) -> Discovery
        {
            result.candidates.clear();
            result.error = "Factory FX unavailable: " + reason;
            return result;
        };

        const auto manifest = manifestAt (appRoot);
        if (! manifest.existsAsFile())
            return fail ("no approved factory manifest in this package");
        if (! contained (appRoot, manifest) || manifest.getSize() > 1024 * 1024)
            return fail ("invalid manifest path or size");

        const auto data = juce::JSON::parse (manifest);
        if (! data.isObject() ||
            static_cast<int> (data.getProperty ("schemaVersion", 0)) != 1 ||
            data.getProperty ("platform", {}).toString() != "windows-x64" ||
            data.getProperty ("hostFormat", {}).toString() != "VST3")
            return fail ("incompatible manifest version/platform");
        const auto distribution = data.getProperty ("distribution", {}).toString();
        if (distribution != "engineering-partial" && distribution != "production-six")
            return fail ("unknown distribution type");

        const auto plugins = data.getProperty ("plugins", {});
        auto* items = plugins.getArray();
        if (items == nullptr || items->isEmpty() || items->size() > 6)
            return fail ("factory entries missing or out of bounds");

        std::set<std::string> seen;
        for (const auto& item : *items)
        {
            if (! item.isObject()) return fail ("invalid plugin entry");
            const auto id = item.getProperty ("productId", {}).toString();
            if (id != "arson-eq" && id != "arson-comp" && id != "arson-verb" &&
                id != "arson-harmony" && id != "arson-color" &&
                id != "arsonkupik-enhancer")
                return fail ("unapproved product ID");
            if (! seen.insert (id.toStdString()).second)
                return fail ("duplicate product ID");
            if (item.getProperty ("licenseReview", {}).toString() != "approved")
                return fail (id + ": distribution licence not approved");
            const auto policy = item.getProperty ("entitlementPolicy", {}).toString();
            if (policy != "unrestricted" && policy != "separate-license" &&
                policy != "factory-entitlement")
                return fail (id + ": entitlement status missing");
            if (! hex (item.getProperty ("vst3ClassId", {}).toString(), 32) ||
                ! hex (item.getProperty ("sourceCommit", {}).toString(), 40) ||
                ! hex (item.getProperty ("artifactSha256", {}).toString(), 64))
                return fail (id + ": unverified release identity");
            if (! item.hasProperty ("hostUniqueId") ||
                ! (item.getProperty ("hostUniqueId", {}).isInt() ||
                   item.getProperty ("hostUniqueId", {}).isInt64()))
                return fail (id + ": JUCE host UID missing");

            const auto relative = item.getProperty ("bundleRelativePath", {}).toString();
            if (! safePath (relative) || ! relative.startsWith ("FactoryFX/") ||
                ! relative.endsWithIgnoreCase (".vst3"))
                return fail (id + ": invalid bundle path");
            const auto bundle = appRoot.getChildFile (relative);
            if ((! bundle.isDirectory() && ! bundle.existsAsFile()) ||
                ! contained (appRoot, bundle))
                return fail (id + ": VST3 bundle missing/outside installation");

            const auto files = item.getProperty ("files", {});
            const auto* fileRows = files.getArray();
            if (fileRows == nullptr || fileRows->isEmpty() || fileRows->size() > 256)
                return fail (id + ": no verified VST3 files");
            for (const auto& f : *fileRows)
            {
                const auto name = f.getProperty ("path", {}).toString();
                const auto hash = f.getProperty ("sha256", {}).toString();
                if (! safePath (name) || ! hex (hash, 64))
                    return fail (id + ": invalid file hash entry");
                const auto binary = name == "." ? bundle : bundle.getChildFile (name);
                if (! binary.existsAsFile() || ! contained (bundle.isDirectory() ? bundle : appRoot, binary))
                    return fail (id + ": bundle resource missing");
                // Disk IO and SHA256 hashing are performed only on scanner worker.
                if (! juce::SHA256 (binary).toHexString().equalsIgnoreCase (hash))
                    return fail (id + ": modified VST3 resource");
            }
            result.candidates.push_back ({ id, bundle.getFullPathName(),
                static_cast<int> (item.getProperty ("hostUniqueId", 0)) });
        }
        if (distribution == "production-six" && result.candidates.size() != 6)
            return fail ("production manifest must contain all six effects");
        return result;
    }
};
} // namespace armix::factory
