// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

// All callbacks run on JUCE's message thread. The scanner performs filesystem
// enumeration and plugin metadata discovery on a dedicated non-audio thread.
struct CatalogEntry
{
    juce::String path, name, vendor, category;
    int uid = 0;
    int variant = 0;

    juce::String key() const { return path + "|" + juce::String (uid) + "|" + juce::String (variant); }

    juce::var toVar() const
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", key());
        o->setProperty ("path", path);
        o->setProperty ("name", name);
        o->setProperty ("vendor", vendor);
        o->setProperty ("category", category);
        o->setProperty ("uid", uid);
        o->setProperty ("variant", variant);
        return juce::var (o.release());
    }

    static CatalogEntry fromVar (const juce::var& v)
    {
        CatalogEntry e;
        e.path = v.getProperty ("path", {}).toString();
        e.name = v.getProperty ("name", {}).toString();
        e.vendor = v.getProperty ("vendor", {}).toString();
        e.category = v.getProperty ("category", {}).toString();
        e.uid = static_cast<int> (v.getProperty ("uid", 0));
        e.variant = static_cast<int> (v.getProperty ("variant", 0));
        return e;
    }
};

class PluginCatalog final
{
public:
    using Callback = std::function<void()>;
    explicit PluginCatalog (juce::File dataFolder)
        : dataFolder_ (std::move (dataFolder)), worker_ (*this)
    {
        dataFolder_.createDirectory();
        load();
    }

    ~PluginCatalog()
    {
        alive_.store (false);
        worker_.signalThreadShouldExit();
        // Scanning cannot be forcibly cancelled inside a third-party scanner.
        // Never detach a worker that still references this object.
        worker_.stopThread (-1);
    }

    const std::vector<CatalogEntry>& entries() const noexcept { return entries_; }
    bool busy() const noexcept { return scanning_.load(); }
    juce::String scanMessage() const { return scanMessage_; }
    Callback onChanged;

    void addDirectory (const juce::File& folder)
    {
        if (! folder.isDirectory() || busy())
            return;
        const auto p = folder.getFullPathName();
        if (! customFolders_.contains (p))
        {
            customFolders_.add (p);
            save();
        }
        scan ({ p });
    }

    void scanDefaults()
    {
        juce::VST3PluginFormat format;
        juce::StringArray paths;
        const auto defaults = format.getDefaultLocationsToSearch();
        for (int i = 0; i < defaults.getNumPaths(); ++i)
            paths.addIfNotAlreadyThere (defaults[i].getFullPathName());
        for (const auto& p : customFolders_)
            paths.addIfNotAlreadyThere (p);
        scan (paths);
    }

    // Direct VST3 file/bundle import (no forced recursive system scan).
    // Native .dll is legacy VST2 and is deliberately not mislabeled as VST3.
    void importVst3 (const juce::File& file)
    {
        if (busy()) return;
        if (! file.exists() || ! file.hasFileExtension (".vst3"))
        {
            scanMessage_ = "Choose a .vst3 file or bundle. Legacy VST2 .dll is not supported.";
            if (onChanged) onChanged();
            return;
        }
        scan (juce::StringArray { file.getFullPathName() });
    }

    const CatalogEntry* find (const juce::String& id) const
    {
        for (const auto& entry : entries_)
            if (entry.key() == id)
                return &entry;
        return nullptr;
    }

    juce::var snapshot() const
    {
        auto o = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> list;
        for (const auto& entry : entries_)
            list.add (entry.toVar());
        o->setProperty ("items", juce::var (list));
        o->setProperty ("scanning", busy());
        o->setProperty ("message", scanMessage_);
        o->setProperty ("count", static_cast<int> (entries_.size()));
        return juce::var (o.release());
    }

private:
    struct Scanner final : juce::Thread
    {
        explicit Scanner (PluginCatalog& owner) : juce::Thread ("AR MIX VST3 Scanner"), owner_ (owner) {}
        PluginCatalog& owner_;
        juce::StringArray requested_;

        void run() override
        {
           #if JUCE_WINDOWS
            const auto hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
           #endif
            std::vector<CatalogEntry> found;
            juce::VST3PluginFormat format;
            for (const auto& requested : requested_)
            {
                if (threadShouldExit()) break;
                const juce::File file (requested);
                juce::StringArray candidates;
                if (file.isDirectory() && ! file.hasFileExtension (".vst3"))
                {
                    juce::FileSearchPath search;
                    search.add (file);
                    candidates = format.searchPathsForPlugins (search, true, false);
                }
                else if (file.hasFileExtension (".vst3"))
                    candidates.add (requested);

                for (const auto& path : candidates)
                {
                    if (threadShouldExit()) break;
                    if (! format.fileMightContainThisPluginType (path)) continue;
                    juce::OwnedArray<juce::PluginDescription> descriptions;
                    format.findAllTypesForFile (descriptions, path);
                    for (int i = 0; i < descriptions.size(); ++i)
                    {
                        const auto* desc = descriptions[i];
                        if (desc == nullptr || desc->isInstrument) continue;
                        found.push_back ({ desc->fileOrIdentifier, desc->name,
                                           desc->manufacturerName, desc->category,
                                           desc->uniqueId, i });
                    }
                }
            }
           #if JUCE_WINDOWS
            if (SUCCEEDED (hr)) CoUninitialize();
           #endif
            // Completion is delivered by a SafePointer owned by the UI host,
            // so destruction cannot dereference the window after shutdown.
            juce::MessageManager::callAsync ([&owner = owner_, results = std::move (found)]() mutable
            {
                if (! owner.alive_.load()) return;
                for (const auto& e : results)
                {
                    const auto iter = std::find_if (owner.entries_.begin(), owner.entries_.end(),
                        [&e] (const CatalogEntry& old) { return old.key() == e.key(); });
                    if (iter == owner.entries_.end())
                        owner.entries_.push_back (e);
                    else
                        *iter = e;
                }
                std::sort (owner.entries_.begin(), owner.entries_.end(),
                    [] (const CatalogEntry& a, const CatalogEntry& b)
                    { return a.name.compareIgnoreCase (b.name) < 0; });
                owner.scanMessage_ = "Scan complete: " + juce::String (results.size()) + " plugin variants inspected";
                owner.scanning_.store (false);
                owner.save();
                if (owner.onChanged) owner.onChanged();
            });
        }
    };

    void scan (const juce::StringArray& paths)
    {
        if (busy()) return;
        scanning_.store (true);
        scanMessage_ = "Scanning VST3 metadata (background)...";
        if (onChanged) onChanged();
        worker_.requested_ = paths;
        worker_.startThread();
    }

    void load()
    {
        auto json = juce::JSON::parse (dataFolder_.getChildFile ("plugin-catalog.json"));
        if (auto* items = json.getProperty ("items", {}).getArray())
            for (const auto& item : *items)
            {
                auto entry = CatalogEntry::fromVar (item);
                if (entry.path.isNotEmpty())
                    entries_.push_back (std::move (entry));
            }
        if (auto* folders = json.getProperty ("folders", {}).getArray())
            for (const auto& folder : *folders)
                customFolders_.addIfNotAlreadyThere (folder.toString());
        scanMessage_ = "Ready - " + juce::String (entries_.size()) + " cached VST3 effects";
    }

    void save()
    {
        auto o = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> list, folders;
        for (const auto& entry : entries_) list.add (entry.toVar());
        for (const auto& folder : customFolders_) folders.add (folder);
        o->setProperty ("schemaVersion", 1);
        o->setProperty ("items", juce::var (list));
        o->setProperty ("folders", juce::var (folders));
        auto temporary = dataFolder_.getChildFile ("plugin-catalog.json.tmp");
        if (temporary.replaceWithText (juce::JSON::toString (juce::var (o.release()), true)))
            temporary.moveFileTo (dataFolder_.getChildFile ("plugin-catalog.json"));
    }

    juce::File dataFolder_;
    std::vector<CatalogEntry> entries_;
    juce::StringArray customFolders_;
    std::atomic<bool> alive_ { true }, scanning_ { false };
    juce::String scanMessage_ = "Ready";
    Scanner worker_;
};
