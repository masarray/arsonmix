// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/PluginCatalog.h"

// Native catalog on a real filesystem and JUCE message thread. No third-party
// plugin binaries are required: scan an empty custom folder and preserve an
// existing cached descriptor. The fixture never claims actual vendor DSP.
static bool customPath(const PluginCatalog& catalog,const juce::String& path,bool available=true) {
    const auto snapshot=catalog.snapshot();
    const auto rows=snapshot.getProperty("scanPaths",{});
    const auto* paths=rows.getArray();
    assert(paths);
    for(const auto& row:*paths)
        if(row.getProperty("path",{}).toString()==path)
            return bool(row.getProperty("custom",false)) &&
                   bool(row.getProperty("available",false))==available;
    return false;
}
static void waitForScan(PluginCatalog& catalog) {
    const auto until=juce::Time::getMillisecondCounter()+10000u;
    while(catalog.busy()&&juce::Time::getMillisecondCounter()<until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    assert(!catalog.busy());
}
int main(){
    juce::ScopedJuceInitialiser_GUI juce;
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("armix-catalog-","",false);
    const auto studio=root.getChildFile("Studio");
    const auto folder=root.getChildFile("User VST");
    const auto second=root.getChildFile("Other VST");
    assert(studio.createDirectory().wasOk());
    assert(folder.createDirectory().wasOk());
    assert(second.createDirectory().wasOk());
    const auto file=studio.getChildFile("plugin-catalog.json");
    CatalogEntry cached;
    cached.path=root.getChildFile("Existing.vst3").getFullPathName();
    cached.name="Existing";cached.vendor="Fixture";cached.uid=42;
    auto seed=std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> items;items.add(cached.toVar());
    seed->setProperty("items",juce::var(items));
    assert(file.replaceWithText(juce::JSON::toString(juce::var(seed.release()))));
    {
        PluginCatalog catalog(studio);
        const auto original=catalog.entries().front().key();
        assert(!customPath(catalog,folder.getFullPathName()));
        assert(catalog.addDirectory(folder).isEmpty());
        assert(customPath(catalog,folder.getFullPathName()));
        waitForScan(catalog);
        assert(catalog.entries().size()==1&&catalog.entries().front().key()==original);
        assert(catalog.addDirectory(folder).isEmpty()); // idempotent source registration
        waitForScan(catalog);
        const auto snapshot=catalog.snapshot();
        const auto rows=snapshot.getProperty("scanPaths",{});
        int count=0;
        for(const auto& row:*rows.getArray())
            if(row.getProperty("path",{}).toString()==folder.getFullPathName())++count;
        assert(count==1);
        // Start the next real scan directly from the previous completion.
        // A premature idle receipt can lose a scan before JUCE retires its thread.
        int completions=0;
        constexpr int cycles=32;
        catalog.onChanged=[&] {
            if(!catalog.busy() && ++completions<cycles)
                assert(catalog.addDirectory(folder).isEmpty());
        };
        assert(catalog.addDirectory(folder).isEmpty());
        waitForScan(catalog);
        assert(completions==cycles);
        catalog.onChanged=nullptr;
        assert(catalog.setFavorite(original,true));
        assert(catalog.entries().front().favorite);
    }
    {
        PluginCatalog catalog(studio); // restart: same native persisted folder
        assert(customPath(catalog,folder.getFullPathName()));
        assert(catalog.entries().size()==1&&catalog.entries().front().key()==cached.key());
        assert(catalog.removeDirectory(second.getFullPathName()).isNotEmpty());
        const auto moved=root.getChildFile("Temporarily moved VST");
        assert(folder.moveFileTo(moved));
        assert(customPath(catalog,folder.getFullPathName(),false));
        assert(moved.moveFileTo(folder));
        // A directory in place of the JSON file is a deterministic write
        // failure without administrator permissions or OS ACL assumptions.
        const auto backup=studio.getChildFile("catalog-backup.json");
        assert(file.moveFileTo(backup));
        assert(file.createDirectory().wasOk());
        // Favorite mutation must not ACK success or alter readback if disk fails.
        assert(catalog.entries().front().favorite);
        assert(!catalog.setFavorite(cached.key(),false));
        assert(catalog.entries().front().favorite);
        assert(catalog.removeDirectory(folder.getFullPathName()).isNotEmpty());
        assert(customPath(catalog,folder.getFullPathName()));
        assert(catalog.addDirectory(second).isNotEmpty());
        assert(!customPath(catalog,second.getFullPathName())&&!catalog.busy());
        assert(file.deleteRecursively());
        assert(backup.moveFileTo(file));
        assert(catalog.setFavorite(cached.key(),false));
        assert(!catalog.entries().front().favorite);
        assert(catalog.removeDirectory(folder.getFullPathName()).isEmpty());
        assert(!customPath(catalog,folder.getFullPathName()));
        assert(catalog.entries().size()==1&&folder.isDirectory());
    }
    {
        PluginCatalog catalog(studio);
        assert(!customPath(catalog,folder.getFullPathName()));
        assert(catalog.entries().size()==1&&catalog.entries().front().key()==cached.key());
        assert(!catalog.entries().front().favorite);
    }
    assert(root.deleteRecursively());
    std::cout<<"PASS: real native chained scan retirement, scan-folder persistence, favorite rollback and restart\n";
}
