// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include "BoundedDataInput.h"

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

    // Managed portable layout is opt-in at the stable root. Legacy loose
    // packages and FIELD builds still use their existing adjacent Studio.
    static bool hasManagedPayloadShape (const juce::File& folder)
    {
        const auto key=folder.getFileName();
        return folder.getParentDirectory().getFileName()=="payloads" &&
            folder.getParentDirectory().getParentDirectory().getFileName()==".ArSonMixUpdates" &&
            key.length()==105 && key[40]=='-' &&
            key.substring(0,40).containsOnly("0123456789abcdef") &&
            key.substring(41).containsOnly("0123456789abcdef");
    }

    static juce::File portableRootFor (const juce::File& folder)
    {
        if(!hasManagedPayloadShape(folder))return folder;
        const auto stable=folder.getParentDirectory().getParentDirectory().getParentDirectory();
        const auto capability=stable.getChildFile("portable_launcher_v2.txt");
        const auto rootMarker=stable.getChildFile(marker);
        const auto payloadMarker=folder.getChildFile(marker);
        juce::String content,rootMode,payloadMode;
        if(!BoundedDataInput::managedPath(stable,folder) ||
           !BoundedDataInput::managedPath(stable,capability) ||
           !BoundedDataInput::managedPath(stable,rootMarker) ||
           !BoundedDataInput::managedPath(folder,payloadMarker) ||
           !BoundedDataInput::text(capability,128,content) ||
           content.trim()!="ARSONMIX_PORTABLE_LAUNCHER_V2" ||
           !BoundedDataInput::text(rootMarker,128,rootMode) ||
           rootMode.trim()!="ARSONMIX_PORTABLE_WORKSPACE_V1" ||
           !BoundedDataInput::text(payloadMarker,128,payloadMode) ||
           payloadMode.trim()!="ARSONMIX_PORTABLE_WORKSPACE_V1")return {};
        return stable;
    }

    static juce::File updateRootFor (const juce::File& folder)
    {
        const auto stable=portableRootFor(folder);
        return stable==juce::File()?juce::File():stable.getChildFile(".ArSonMixUpdates");
    }

    static juce::File dataRootFor (const juce::File& executableFolder)
    {
        if(hasManagedPayloadShape(executableFolder)){
            const auto stable=portableRootFor(executableFolder);
            return stable==juce::File()?juce::File():stable.getChildFile ("Studio");
        }
        return isPortableAt (executableFolder)
            ? executableFolder.getChildFile ("Studio")
            : installedDataRoot();
    }

    static bool launcherEnabledAt(const juce::File& folder)
    {
        if(folder==juce::File())return false;
        const auto cap=folder.getChildFile("portable_launcher_v2.txt");
        juce::String value,mode;
        return BoundedDataInput::managedPath(folder,cap) &&
            BoundedDataInput::text(cap,128,value) && value.trim()=="ARSONMIX_PORTABLE_LAUNCHER_V2" &&
            BoundedDataInput::text(folder.getChildFile(marker),128,mode) && mode.trim()=="ARSONMIX_PORTABLE_WORKSPACE_V1";
    }

    // Every GUI-only probe uses a fresh machine-local scratch root, including
    // installed probes. No production Studio, device XML or WebView profile.
    static juce::File probeDataRoot()
    {
#if defined(_WIN32)
        const auto token=juce::SystemStats::getEnvironmentVariable("ARSONMIX_UPDATE_PROBE",{});
        const auto local=juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{});
        if(token.length()==36 && juce::Uuid(token).toDashedString()==token && local.isNotEmpty())
            return juce::File(local).getChildFile("ArSonMix/Updates/probes").getChildFile(token);
#endif
        return {};
    }

    static juce::File dataRoot()
    {
        const auto probe=probeDataRoot();
        return probe==juce::File()?dataRootFor (executableDirectory()):probe;
    }

    // Device selections remain per MACHINE so copying USB never un-mutes an
    // unintended endpoint, and legacy installed audio-devices.xml is retained.
    static juce::File machineDataRoot() { const auto probe=probeDataRoot(); return probe==juce::File()?installedDataRoot():probe; }

    static juce::File browserDataRootFor (const juce::File& executableFolder)
    {
        if (isPortableAt (executableFolder) || hasManagedPayloadShape(executableFolder)){
            const auto data=dataRootFor(executableFolder);
            return data==juce::File()?juce::File():data.getChildFile ("Cache/WebView2");
        }
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("AR-MIX-Studio-WebView2");
    }

    static juce::File browserDataRoot() { const auto probe=probeDataRoot(); return probe==juce::File()?browserDataRootFor (executableDirectory()):probe.getChildFile("Cache/WebView2"); }

    static juce::File vaultRoot (const juce::File& workspace)
    {
        return workspace.getChildFile ("Plugins/VST3");
    }

    static bool safeRelativePath (const juce::String& relative)
    {
        if (relative.isEmpty() || relative.length() > 2048 ||
            relative.startsWithChar ('/') || relative.endsWithChar ('/') || relative.containsChar ('\\') ||
            relative.containsChar (':') || relative.containsChar ('\0'))
            return false;
        juce::StringArray components;
        components.addTokens (relative, "/", "");
        if (components.isEmpty()) return false;
        for (const auto& part : components) {
            if (part.isEmpty() || part == "." || part == ".." ||
                part.endsWithChar (' ') || part.endsWithChar ('.') ||
                part.containsAnyOf("<>:\"|?*"))return false;
            for(auto p=part.getCharPointer();!p.isEmpty();++p)
                if(*p<32)return false;
            const auto stem=part.upToFirstOccurrenceOf(".",false,false).trimEnd().toUpperCase();
            if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
               (stem.length()==4&&(stem.startsWith("COM")||stem.startsWith("LPT"))&&
                ((stem[3]>='1'&&stem[3]<='9')||stem[3]==0xb9||stem[3]==0xb2||stem[3]==0xb3)))return false;
        }
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
            return juce::File::isAbsolutePath(stored)?juce::File(stored):juce::File(); // v1 absolute-path compatibility
        const auto relative = stored.substring (juce::String (scheme).length());
        if (workspace == juce::File() || !safeRelativePath (relative))
            return {}; // invalid workspace reference, not silently treated as CWD
        const auto resolved = workspace.getChildFile (relative);
        return resolved.isAChildOf (workspace) && BoundedDataInput::managedPath(workspace,resolved)
            ? resolved : juce::File();
    }
};
} // namespace armix::storage
