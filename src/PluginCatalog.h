// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "PortableWorkspace.h"
#include "PluginVault.h"
#include "FactoryFxRegistry.h"
#include <algorithm>
#if JUCE_WINDOWS
 #include <objbase.h>
#endif
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
    bool favorite = false; // Portable user preference; never inferred from vendor scanner.
    bool factory = false; // Verified app-managed binary, not user Vault.
    juce::String factoryProductId;

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
        o->setProperty ("favorite", favorite);
        o->setProperty ("source", factory ? "factory" : "user");
        if (factory) o->setProperty ("factoryProductId", factoryProductId);
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
        e.favorite = static_cast<bool> (v.getProperty ("favorite", false));
        e.factory = v.getProperty ("source", {}).toString() == "factory";
        e.factoryProductId = v.getProperty ("factoryProductId", {}).toString();
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
        lifetime_->store (false);
        worker_.signalThreadShouldExit();
        // Scanning cannot be forcibly cancelled inside a third-party scanner.
        // Never detach a worker that still references this object.
        worker_.stopThread (-1);
    }

    const std::vector<CatalogEntry>& entries() const noexcept { return entries_; }
    bool busy() const noexcept { return scanning_.load(); }
    juce::String scanMessage() const { return scanMessage_; }
    // Real scan counters. Enumeration has no known denominator; only report
    // a percentage after candidate discovery has finished.
    juce::var progress() const
    {
        auto result = std::make_unique<juce::DynamicObject>();
        const auto phase = phase_.load (std::memory_order_acquire);
        const auto total = total_.load (std::memory_order_relaxed);
        result->setProperty ("active", busy());
        result->setProperty ("phase", phase == 1 ? "discovering" :
                                        phase == 2 ? "inspecting" : "done");
        result->setProperty ("done", completed_.load (std::memory_order_relaxed));
        result->setProperty ("total", total);
        result->setProperty ("found", found_.load (std::memory_order_relaxed));
        {
            const juce::ScopedLock lock (progressLock_);
            result->setProperty ("current", current_);
        }
        return juce::var (result.release());
    }
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

    void scanFactoryIfPresent()
    {
        // Fast existence check; all manifest hashing and VST discovery run on worker.
        if (! busy() && armix::factory::Registry::manifestAt (
                armix::factory::Registry::executableRoot()).existsAsFile())
            scan ({}, false, true);
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
    void importVst3 (const juce::File& file, bool copyToVault = true)
    {
        if (busy()) return;
        if (! file.exists() || ! file.hasFileExtension (".vst3"))
        {
            scanMessage_ = "Choose a .vst3 file or bundle. Legacy VST2 .dll is not supported.";
            if (onChanged) onChanged();
            return;
        }
        // The selected VST3 is copied on Scanner's non-audio thread.
        // Keep the original filename/bundle/resources and scan ONLY this copy.
        scan (juce::StringArray { file.getFullPathName() }, copyToVault);
    }

    // Preference writes only on the JUCE message thread, never during DSP processing.
    // Stored alongside each catalog variant so the Studio backup carries favorites.
    bool setFavorite (const juce::String& id, bool favorite)
    {
        for (auto& entry : entries_)
            if (entry.key() == id)
            {
                if (entry.favorite == favorite) return true;
                entry.favorite = favorite;
                if (entry.factory)
                {
                    if (favorite) factoryFavorites_.addIfNotAlreadyThere (entry.factoryProductId);
                    else factoryFavorites_.removeString (entry.factoryProductId);
                }
                save();
                if (onChanged) onChanged();
                return true;
            }
        return false;
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
        bool copyToVault_ = false;
        bool factoryOnly_ = false;

        void run() override
        {
           #if JUCE_WINDOWS
            const auto hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
           #endif
            std::vector<CatalogEntry> found;
            juce::VST3PluginFormat format;
            juce::StringArray allCandidates;
            juce::String importErrors;
            const auto factory = factoryOnly_
                ? armix::factory::Registry::discover (armix::factory::Registry::executableRoot())
                : armix::factory::Discovery{};
            if (factoryOnly_ && ! factory.ok()) importErrors = factory.error;
            // 1. Enumerate candidates. Total cannot be estimated here.
            owner_.phase_.store (1, std::memory_order_release);
            if (factoryOnly_ && factory.ok())
                for (const auto& item : factory.candidates)
                    allCandidates.addIfNotAlreadyThere (item.path);
            for (const auto& requested : requested_)
            {
                if (threadShouldExit()) break;
                juce::File file (requested);
                if (copyToVault_)
                {
                    const auto imported = armix::storage::PluginVault::importSelected (
                        file, owner_.dataFolder_);
                    if (!imported.ok())
                    {
                        importErrors << (importErrors.isNotEmpty() ? " | " : "")
                                     << file.getFileName() << ": " << imported.error;
                        continue;
                    }
                    file = imported.plugin;
                }
                juce::StringArray candidates;
                if (file.isDirectory() && ! file.hasFileExtension (".vst3"))
                {
                    juce::FileSearchPath search;
                    search.add (file);
                    candidates = format.searchPathsForPlugins (search, true, false);
                }
                else if (file.hasFileExtension (".vst3"))
                    candidates.add (file.getFullPathName());
                for (const auto& path : candidates)
                    allCandidates.addIfNotAlreadyThere (path);
            }

            // 2. Inspect actual plugins; progress denominator is now real.
            owner_.total_.store (allCandidates.size(), std::memory_order_relaxed);
            owner_.phase_.store (2, std::memory_order_release);
            for (int n = 0; n < allCandidates.size(); ++n)
            {
                if (threadShouldExit()) break;
                const auto path = allCandidates[n];
                {
                    const juce::ScopedLock lock (owner_.progressLock_);
                    owner_.current_ = juce::File (path).getFileName();
                }
                if (format.fileMightContainThisPluginType (path))
                {
                    juce::OwnedArray<juce::PluginDescription> descriptions;
                    format.findAllTypesForFile (descriptions, path);
                    for (int i = 0; i < descriptions.size(); ++i)
                    {
                        const auto* desc = descriptions[i];
                        if (desc == nullptr || desc->isInstrument) continue;
                        CatalogEntry entry { desc->fileOrIdentifier, desc->name,
                                             desc->manufacturerName, desc->category,
                                             desc->uniqueId, i };
                        if (factoryOnly_)
                        {
                            const auto it = std::find_if (factory.candidates.begin(), factory.candidates.end(),
                                [path, desc] (const armix::factory::Candidate& approved)
                                { return approved.path == path && approved.hostUniqueId == desc->uniqueId; });
                            if (it == factory.candidates.end()) continue;
                            entry.factory = true;
                            entry.factoryProductId = it->productId;
                        }
                        found.push_back (std::move (entry));
                    }
                }
                owner_.found_.store (static_cast<int> (found.size()), std::memory_order_relaxed);
                owner_.completed_.store (n + 1, std::memory_order_release);
            }
            {
                const juce::ScopedLock lock (owner_.progressLock_);
                owner_.current_.clear();
            }
           #if JUCE_WINDOWS
            if (SUCCEEDED (hr)) CoUninitialize();
           #endif
            // Completion is delivered by a SafePointer owned by the UI host,
            // so destruction cannot dereference the window after shutdown.
            auto lifetime = owner_.lifetime_;
            const bool wasFactoryScan = factoryOnly_;
            juce::MessageManager::callAsync ([&owner = owner_, lifetime, results = std::move (found),
                                              importErrors, wasFactoryScan]() mutable
            {
                if (! lifetime->load()) return;
                if (wasFactoryScan)
                    owner.entries_.erase (std::remove_if (owner.entries_.begin(), owner.entries_.end(),
                        [] (const CatalogEntry& e) { return e.factory; }), owner.entries_.end());
                for (auto e : results)
                {
                    const auto iter = std::find_if (owner.entries_.begin(), owner.entries_.end(),
                        [&e] (const CatalogEntry& old) { return old.key() == e.key(); });
                    if (e.factory) e.favorite = owner.factoryFavorites_.contains (e.factoryProductId);
                    if (iter == owner.entries_.end())
                        owner.entries_.push_back (e);
                    else
                    {
                        const bool favorite = iter->favorite;
                        *iter = e;
                        iter->favorite = favorite; // Rescan must NEVER erase user favorites.
                    }
                }
                std::sort (owner.entries_.begin(), owner.entries_.end(),
                    [] (const CatalogEntry& a, const CatalogEntry& b)
                    { return a.name.compareIgnoreCase (b.name) < 0; });
                owner.scanMessage_ = importErrors.isNotEmpty()
                    ? (wasFactoryScan ? importErrors : "Import incomplete: " + importErrors)
                    : (results.empty()
                        ? "No compatible VST3 effects found; check plugin architecture and vendor dependencies."
                        : "Ready: " + juce::String (results.size()) + " VST3 variants inspected");
                owner.phase_.store (0, std::memory_order_release);
                owner.scanning_.store (false);
                owner.save();
                if (owner.onChanged) owner.onChanged();
            });
        }
    };

    void scan (const juce::StringArray& paths, bool copyToVault = false, bool factoryOnly = false)
    {
        if (busy()) return;
        completed_.store (0);
        total_.store (0);
        found_.store (0);
        phase_.store (1);
        { const juce::ScopedLock lock (progressLock_); current_.clear(); }
        scanning_.store (true);
        scanMessage_ = factoryOnly ? "Verifying bundled Factory VST3..." :
            (copyToVault ? "Importing selected VST3 into Studio (only chosen module)..."
                         : "Scanning VST3 metadata (background)...");
        if (onChanged) onChanged();
        worker_.requested_ = paths;
        worker_.copyToVault_ = copyToVault;
        worker_.factoryOnly_ = factoryOnly;
        worker_.startThread();
    }

    void load()
    {
        auto json = juce::JSON::parse (dataFolder_.getChildFile ("plugin-catalog.json"));
        const auto storedItems = json.getProperty ("items", {});
        if (auto* items = storedItems.getArray())
            for (const auto& item : *items)
            {
                auto entry = CatalogEntry::fromVar (item);
                if (entry.path.startsWith (armix::storage::PortableWorkspace::scheme))
                {
                    const auto resolved = armix::storage::PortableWorkspace::decode (dataFolder_, entry.path);
                    if (resolved == juce::File()) continue;
                    entry.path = resolved.getFullPathName();
                }
                if (! entry.factory && entry.path.isNotEmpty())
                    entries_.push_back (std::move (entry));
            }
        const auto storedFolders = json.getProperty ("folders", {});
        if (auto* folders = storedFolders.getArray())
            for (const auto& folder : *folders)
                customFolders_.addIfNotAlreadyThere (folder.toString());
        const auto storedFactoryFavorites = json.getProperty ("factoryFavorites", {});
        if (auto* favorites = storedFactoryFavorites.getArray())
            for (const auto& id : *favorites)
                factoryFavorites_.addIfNotAlreadyThere (id.toString());
        scanMessage_ = "Ready - " + juce::String (entries_.size()) + " cached VST3 effects";
    }

    void save()
    {
        auto o = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> list, folders;
        for (const auto& entry : entries_)
        {
            if (entry.factory) continue; // Never persist an unverified factory binary path.
            auto item = entry.toVar();
            item.getDynamicObject()->setProperty ("path",
                armix::storage::PortableWorkspace::encode (dataFolder_, juce::File (entry.path)));
            list.add (std::move (item));
        }
        for (const auto& folder : customFolders_) folders.add (folder);
        o->setProperty ("schemaVersion", 1);
        o->setProperty ("items", juce::var (list));
        o->setProperty ("folders", juce::var (folders));
        juce::Array<juce::var> factoryFav;
        for (const auto& id : factoryFavorites_) factoryFav.add (id);
        o->setProperty ("factoryFavorites", juce::var (factoryFav));
        dataFolder_.getChildFile ("plugin-catalog.json")
            .replaceWithText (juce::JSON::toString (juce::var (o.release()), true));
    }

    juce::File dataFolder_;
    std::vector<CatalogEntry> entries_;
    juce::StringArray customFolders_, factoryFavorites_;
    std::atomic<bool> alive_ { true }, scanning_ { false };
    std::shared_ptr<std::atomic<bool>> lifetime_ = std::make_shared<std::atomic<bool>> (true);
    mutable juce::CriticalSection progressLock_;
    juce::String current_;
    std::atomic<int> phase_ { 0 }, total_ { 0 }, completed_ { 0 }, found_ { 0 };
    juce::String scanMessage_ = "Ready";
    Scanner worker_;
};
