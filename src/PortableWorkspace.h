// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>

// Portability is a DATA LOCATION, not an edition/entitlement. Native host, VST
// hosting and UI are identical with or without portable_mode.txt. All methods
// run on app control/IO code, never the realtime audio callback.
namespace armix::storage
{
struct PortableWorkspace final
{
    static constexpr const char* marker = "portable_mode.txt";
    static constexpr const char* scheme = "workspace://";

    static juce::File installedDataRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("AR-MIX-Studio");
    }

    static juce::File executableDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getParentDirectory();
    }

    static bool isPortableAt (const juce::File& executableFolder)
    {
        return executableFolder.getChildFile (marker).existsAsFile();
    }

    static juce::File dataRootFor (const juce::File& executableFolder)
    {
        return isPortableAt (executableFolder)
            ? executableFolder.getChildFile ("Studio")
            : installedDataRoot();
    }

    static juce::File dataRoot()
    {
        return dataRootFor (executableDirectory());
    }

    // Device selections remain per MACHINE so copying USB never un-mutes an
    // unintended endpoint, and legacy installed audio-devices.xml is retained.
    static juce::File machineDataRoot() { return installedDataRoot(); }

    static juce::File browserDataRootFor (const juce::File& executableFolder)
    {
        if (isPortableAt (executableFolder))
            return dataRootFor (executableFolder).getChildFile ("Cache/WebView2");
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("AR-MIX-Studio-WebView2");
    }

    static juce::File browserDataRoot() { return browserDataRootFor (executableDirectory()); }

    static juce::File vaultRoot (const juce::File& workspace)
    {
        return workspace.getChildFile ("Plugins/VST3");
    }

    static bool safeRelativePath (const juce::String& relative)
    {
        if (relative.isEmpty() || relative.length() > 2048 ||
            relative.startsWithChar ('/') || relative.containsChar ('\\') ||
            relative.containsChar (':') || relative.containsChar ('\0'))
            return false;
        juce::StringArray components;
        components.addTokens (relative, "/", "");
        if (components.isEmpty()) return false;
        for (const auto& part : components)
            if (part.isEmpty() || part == "." || part == ".." ||
                part.endsWithChar (' ') || part.endsWithChar ('.'))
                return false;
        // Empty tokens are also invalid (e.g. Plugins//Bad.vst3).
        return !relative.contains ("//");
    }

    static juce::String encode (const juce::File& workspace, const juce::File& source)
    {
        if (workspace == juce::File() || source == juce::File() ||
            !source.isAChildOf (workspace))
            return source.getFullPathName(); // explicit Load in Place remains possible
        const auto relative = source.getRelativePathFrom (workspace).replaceCharacter ('\\', '/');
        if (!safeRelativePath (relative))
            return {}; // never persist an invalid workspace ref
        return juce::String (scheme) + relative;
    }

    static juce::File decode (const juce::File& workspace, const juce::String& stored)
    {
        if (!stored.startsWith (scheme))
            return juce::File (stored); // v1 absolute-path compatibility
        const auto relative = stored.substring (juce::String (scheme).length());
        if (workspace == juce::File() || !safeRelativePath (relative))
            return {}; // invalid workspace reference, not silently treated as CWD
        const auto resolved = workspace.getChildFile (relative);
        return resolved.isAChildOf (workspace) ? resolved : juce::File();
    }
};
} // namespace armix::storage
