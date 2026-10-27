// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include "PortableWorkspace.h"
#include <array>
#include <cmath>
#include <vector>

// P2.4 native VST3-state container. Archive never extracts paths to disk.
// All calls are CONTROL/IO thread only, never made by AudioIODeviceCallback.
namespace armix::storage
{
struct FxState
{
    juce::String name, path, catalogId;
    int uid = 0;
    bool bypass = false;
    juce::MemoryBlock binary;
};

struct Snapshot
{
    juce::String name;
    float channelDb = 0.f, pan = 0.f, masterDb = 0.f;
    bool channelMute = false, masterMute = false;
    std::vector<FxState> effects;
};

class NativePresetArchive final
{
public:
    static constexpr int maxEffects = 8;
    static constexpr size_t maxStateBytes = 8 * 1024 * 1024;
    static constexpr juce::int64 maxArchiveBytes = 72 * 1024 * 1024;

    explicit NativePresetArchive (juce::File directory, juce::File workspaceRoot = {})
        : folder_ (std::move (directory)), workspaceRoot_ (std::move (workspaceRoot))
    {
        folder_.createDirectory();
    }

    static juce::String cleanName (juce::String input)
    {
        return input.trim().substring (0, 56)
                    .retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    }

    juce::File fileFor (const juce::String& name) const
    {
        return folder_.getChildFile (cleanName (name) + ".armix");
    }

    juce::var list() const
    {
        juce::Array<juce::var> values;
        auto files = folder_.findChildFiles (juce::File::findFiles, false, "*.armix");
        for (const auto& file : files)
        {
            if (values.size() >= 120) break;
            values.add (file.getFileNameWithoutExtension());
        }
        return juce::var (values);
    }

    // This ZIP is the ARD-specified portable container, not a list of GUI knob labels.
    juce::String save (const Snapshot& state)
    {
        if (state.effects.size() > maxEffects) return "Too many effects in snapshot";
        const auto safe = cleanName (state.name);
        if (safe.isEmpty()) return "Use a preset name containing letters or numbers";
        if (!folder_.isDirectory() && folder_.createDirectory().failed())
            return "Preset directory cannot be created";

        juce::ZipFile::Builder builder;
        auto manifest = std::make_unique<juce::DynamicObject>();
        manifest->setProperty ("format", "arsonmix.native.fx-snapshot");
        manifest->setProperty ("schemaVersion", 1);
        manifest->setProperty ("name", safe);
        manifest->setProperty ("channelId", "ch-001");
        manifest->setProperty ("channelDb", static_cast<double> (state.channelDb));
        manifest->setProperty ("channelPan", static_cast<double> (state.pan));
        manifest->setProperty ("masterDb", static_cast<double> (state.masterDb));
        manifest->setProperty ("channelMute", state.channelMute);
        manifest->setProperty ("masterMute", state.masterMute);
        // Hardware output endpoints are intentionally NOT saved or switched.
        juce::Array<juce::var> items;
        for (int index = 0; index < static_cast<int> (state.effects.size()); ++index)
        {
            const auto& fx = state.effects[static_cast<size_t> (index)];
            if (! fx.path.endsWithIgnoreCase (".vst3") || fx.path.length() > 1024 ||
                fx.binary.getSize() > maxStateBytes || fx.path.isEmpty())
                return "Invalid plugin path or oversized VST3 state in slot " + juce::String (index + 1);
            const auto path = "state/" + juce::String (index).paddedLeft ('0', 2) + ".bin";
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("name", fx.name);
            const auto storedPath = workspaceRoot_ == juce::File()
                ? fx.path : PortableWorkspace::encode (workspaceRoot_, juce::File (fx.path));
            obj->setProperty ("path", storedPath);
            // The legacy key is path|JUCE_UID|variant. Rebase the path portion
            // together with the module path so the loaded rack finds the
            // correct catalog entry after a USB drive-letter change.
            const auto storedId = fx.catalogId.startsWith (fx.path + "|")
                ? storedPath + fx.catalogId.substring (fx.path.length())
                : fx.catalogId;
            obj->setProperty ("catalogId", storedId);
            obj->setProperty ("uid", fx.uid);
            obj->setProperty ("bypass", fx.bypass);
            obj->setProperty ("statePath", path);
            obj->setProperty ("stateBytes", static_cast<int> (fx.binary.getSize()));
            obj->setProperty ("sha256", juce::SHA256 (fx.binary).toHexString());
            items.add (juce::var (obj.release()));
            builder.addEntry (
                std::make_unique<juce::MemoryInputStream> (fx.binary.getData(), fx.binary.getSize(), true),
                0, path, juce::Time::getCurrentTime());
        }
        manifest->setProperty ("pluginInstances", juce::var (items));
        const auto json = juce::JSON::toString (juce::var (manifest.release()), true);
        if (json.getNumBytesAsUTF8() > 256 * 1024) return "Manifest too large";
        builder.addEntry (
            std::make_unique<juce::MemoryInputStream> (json.toRawUTF8(),
                 static_cast<size_t> (json.getNumBytesAsUTF8()), true),
            6, "manifest.json", juce::Time::getCurrentTime());

        juce::TemporaryFile temp (fileFor (safe));
        auto stream = temp.getFile().createOutputStream();
        if (! stream) return "Unable to open temporary preset file";
        if (! builder.writeToStream (*stream, nullptr))
            return "Preset ZIP write failed; original preset untouched";
        stream->flush();
        if (stream->getStatus().failed())
            return "Failed to flush preset to disk";
        stream.reset();
        if (temp.getFile().getSize() > maxArchiveBytes)
            return "Preset archive exceeds safety limit";
        if (! temp.overwriteTargetFileWithTemporary())
            return "Could not commit preset file atomically";
        return {};
    }

    juce::String load (const juce::String& requestedName, Snapshot& output) const
    {
        const auto safe = cleanName (requestedName);
        if (safe.isEmpty() || safe != requestedName) return "Invalid preset name";
        const auto file = fileFor (safe);
        if (! file.existsAsFile() || file.getSize() > maxArchiveBytes)
            return "Preset file missing or above safety limit";
        juce::ZipFile zip (file);
        if (zip.getNumEntries() < 1 || zip.getNumEntries() > maxEffects + 1)
            return "Invalid preset archive entry count";
        const int manifestIndex = zip.getIndexOfFileName ("manifest.json");
        if (manifestIndex < 0 || !zip.getEntry (manifestIndex) ||
            zip.getEntry (manifestIndex)->uncompressedSize > 256 * 1024)
            return "Preset manifest missing or exceeds 256 KB";
        const auto manifestStream = std::unique_ptr<juce::InputStream> (
            zip.createStreamForEntry (manifestIndex));
        if (!manifestStream) return "Cannot open preset manifest";
        const auto data = manifestStream->readEntireStreamAsString();
        if (data.getNumBytesAsUTF8() > 256 * 1024) return "Manifest exceeds limit";
        const auto manifest = juce::JSON::parse (data);
        if (!manifest.isObject() ||
            manifest.getProperty ("format", {}).toString() != "arsonmix.native.fx-snapshot" ||
            static_cast<int> (manifest.getProperty ("schemaVersion", 0)) != 1 ||
            !manifest.getProperty ("pluginInstances", {}).isArray())
            return "Unsupported native preset format";
        auto temp = Snapshot {};
        temp.name = safe;
        auto readDb = [&manifest] (const char* key, double minimum, double maximum, float& result)
        {
            const auto v = manifest.getProperty (key, {});
            if (!v.isDouble() && !v.isInt()) return false;
            const auto number = static_cast<double> (v);
            if (!std::isfinite (number) || number < minimum || number > maximum) return false;
            result = static_cast<float> (number);
            return true;
        };
        if (!readDb ("channelDb", -60, 10, temp.channelDb) ||
            !readDb ("masterDb", -60, 10, temp.masterDb) ||
            !readDb ("channelPan", -1, 1, temp.pan))
            return "Unsafe mixer values in preset";
        const auto m1 = manifest.getProperty ("channelMute", {});
        const auto m2 = manifest.getProperty ("masterMute", {});
        if (!m1.isBool() || !m2.isBool()) return "Invalid mute flags";
        temp.channelMute = static_cast<bool> (m1);
        temp.masterMute = static_cast<bool> (m2);

        const auto entries = manifest.getProperty ("pluginInstances", {});
        const auto* list = entries.getArray();
        if (!list || list->size() > maxEffects || zip.getNumEntries() != list->size() + 1)
            return "Preset plugin count mismatch";

        for (int i = 0; i < list->size(); ++i)
        {
            const auto& entry = list->getReference (i);
            if (!entry.isObject()) return "Invalid preset plugin record";
            auto fx = FxState {};
            fx.name = entry.getProperty ("name", {}).toString();
            const auto storedPath = entry.getProperty ("path", {}).toString();
            fx.path = storedPath; // v1 absolute string is preserved byte-for-byte
            if (storedPath.startsWith (PortableWorkspace::scheme))
            {
                const auto resolved = PortableWorkspace::decode (workspaceRoot_, storedPath);
                if (resolved == juce::File()) return "Unsafe workspace plugin reference";
                fx.path = resolved.getFullPathName();
            }
            fx.catalogId = entry.getProperty ("catalogId", {}).toString();
            if (fx.catalogId.startsWith (storedPath + "|"))
                fx.catalogId = fx.path + fx.catalogId.substring (storedPath.length());
            const auto vUid = entry.getProperty ("uid", {});
            const auto vBypass = entry.getProperty ("bypass", {});
            if (!vUid.isInt() || !vBypass.isBool() ||
                !fx.path.endsWithIgnoreCase (".vst3") || fx.path.length() > 1024)
                return "Invalid VST3 identity";
            fx.uid = static_cast<int> (vUid);
            fx.bypass = static_cast<bool> (vBypass);
            const auto statePath = "state/" + juce::String (i).paddedLeft ('0', 2) + ".bin";
            if (entry.getProperty ("statePath", {}).toString() != statePath)
                return "Unexpected archive state path";
            const int zipIndex = zip.getIndexOfFileName (statePath);
            const auto* zipEntry = zipIndex >= 0 ? zip.getEntry (zipIndex) : nullptr;
            if (!zipEntry || zipEntry->uncompressedSize < 0 ||
                zipEntry->uncompressedSize > static_cast<juce::int64> (maxStateBytes) ||
                static_cast<int> (entry.getProperty ("stateBytes", -1)) != zipEntry->uncompressedSize)
                return "VST3 state is missing or exceeds safety limit";
            auto input = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (zipIndex));
            if (!input) return "Unable to read VST3 state";
            input->readIntoMemoryBlock (fx.binary, static_cast<int> (maxStateBytes + 1));
            if (fx.binary.getSize() != static_cast<size_t> (zipEntry->uncompressedSize) ||
                !juce::SHA256 (fx.binary).toHexString().equalsIgnoreCase (
                    entry.getProperty ("sha256", {}).toString()))
                return "VST3 state integrity check failed";
            temp.effects.push_back (std::move (fx));
        }
        output = std::move (temp); // transactional: caller sees only validated state
        return {};
    }

private:
    juce::File folder_, workspaceRoot_;
};
} // namespace armix::storage
