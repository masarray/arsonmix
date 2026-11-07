// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <set>
#include <vector>

// Control/background thread ONLY. Factory modules are app-managed, never user Vault.
// Strict mode validates the entire package for release/testing. Runtime mode
// isolates faulty *optional* effects without silencing valid plugins or mixer DSP.
namespace armix::factory
{
struct Candidate
{
    juce::String productId, path;
    int hostUniqueId = 0;
    juce::String artifactSha256; // Fixed, approved ZIP identity for cold-scan metadata cache.
};

struct Discovery
{
    std::vector<Candidate> candidates;
    juce::String error;
    juce::StringArray warnings; // Runtime only: rejected optional modules, never loadable.
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

    static Discovery discover (const juce::File& appRoot, bool tolerateUnavailable = false)
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
        if (distribution != "engineering-partial" && distribution != "production-four" &&
            distribution != "production-six")
            return fail ("unknown distribution type");

        const auto plugins = data.getProperty ("plugins", {});
        auto* items = plugins.getArray();
        if (items == nullptr || items->isEmpty() || items->size() > 6)
            return fail ("factory entries missing or out of bounds");

        std::set<std::string> seen;
        for (const auto& item : *items)
        {
            Candidate candidate;
            const auto validate = [&]() -> juce::String
            {
                if (! item.isObject()) return juce::String ("invalid plugin entry");
                const auto id = item.getProperty ("productId", {}).toString();
                if (id != "arson-eq" && id != "arson-comp" && id != "arson-verb" &&
                    id != "arson-harmony" && id != "arson-color" &&
                    id != "arsonkupik-enhancer")
                    return juce::String ("unapproved product ID");
                if (! seen.insert (id.toStdString()).second)
                    return juce::String ("duplicate product ID");
                if (item.getProperty ("licenseReview", {}).toString() != "approved")
                    return juce::String (id + ": distribution licence not approved");
                const auto policy = item.getProperty ("entitlementPolicy", {}).toString();
                if (policy != "unrestricted" && policy != "separate-license" &&
                    policy != "factory-entitlement")
                    return juce::String (id + ": entitlement status missing");
                if (! hex (item.getProperty ("vst3ClassId", {}).toString(), 32) ||
                    ! hex (item.getProperty ("sourceCommit", {}).toString(), 40) ||
                    ! hex (item.getProperty ("artifactSha256", {}).toString(), 64))
                    return juce::String (id + ": unverified release identity");
                if (! item.hasProperty ("hostUniqueId") ||
                    ! (item.getProperty ("hostUniqueId", {}).isInt() ||
                       item.getProperty ("hostUniqueId", {}).isInt64()))
                    return juce::String (id + ": JUCE host UID missing");
    
                const auto relative = item.getProperty ("bundleRelativePath", {}).toString();
                if (! safePath (relative) || ! relative.startsWith ("FactoryFX/") ||
                    ! relative.endsWithIgnoreCase (".vst3"))
                    return juce::String (id + ": invalid bundle path");
                const auto bundle = appRoot.getChildFile (relative);
                if ((! bundle.isDirectory() && ! bundle.existsAsFile()) ||
                    ! contained (appRoot, bundle))
                    return juce::String (id + ": VST3 bundle missing/outside installation");
    
                const auto files = item.getProperty ("files", {});
                const auto* fileRows = files.getArray();
                if (fileRows == nullptr || fileRows->isEmpty() || fileRows->size() > 256)
                    return juce::String (id + ": no verified VST3 files");
                for (const auto& f : *fileRows)
                {
                    const auto name = f.getProperty ("path", {}).toString();
                    const auto hash = f.getProperty ("sha256", {}).toString();
                    if (! safePath (name) || ! hex (hash, 64))
                        return juce::String (id + ": invalid file hash entry");
                    const auto binary = name == "." ? bundle : bundle.getChildFile (name);
                    if (! binary.existsAsFile() || ! contained (bundle.isDirectory() ? bundle : appRoot, binary))
                        return juce::String (id + ": bundle resource missing");
                    // Disk IO and SHA256 hashing are performed only on scanner worker.
                    if (! juce::SHA256 (binary).toHexString().equalsIgnoreCase (hash))
                        return juce::String (id + ": modified VST3 resource");
                }
                candidate = { id, bundle.getFullPathName(),
                    static_cast<int> (item.getProperty ("hostUniqueId", 0)),
                    item.getProperty ("artifactSha256", {}).toString() };
                return {};
            };
            const auto rejected = validate();
            if (rejected.isNotEmpty())
            {
                if (! tolerateUnavailable) return fail (rejected);
                result.warnings.add (rejected);
                continue;
            }
            result.candidates.push_back (std::move (candidate));
        }
        // Official four-FX packs require the exact reviewed original suppliers.
        if (distribution == "production-four")
        {
            static const std::set<std::string> required {
                "arson-eq", "arson-comp", "arson-verb", "arsonkupik-enhancer" };
            if (! tolerateUnavailable && seen != required)
                return fail ("production-four manifest requires EQ, Comp, Verb, KuPik");
        }
        if (! tolerateUnavailable && distribution == "production-six" && result.candidates.size() != 6)
            return fail ("production manifest must contain all six effects");
        return result;
    }
};
} // namespace armix::factory
