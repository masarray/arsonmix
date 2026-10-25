// SPDX-License-Identifier: AGPL-3.0-only
// Runtime test for the actual .armix ZIP implementation, not static text.
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include "../src/NativePresetArchive.h"

int main()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("ArSonMixPresetUnit", "", false);
    assert (folder.createDirectory().wasOk());
    armix::storage::NativePresetArchive archive (folder);

    armix::storage::Snapshot original;
    original.name = "Vocal Session 01";
    original.channelDb = -7.25f;
    original.masterDb = -3.5f;
    original.pan = .42f;
    original.channelMute = true;
    original.masterMute = false;
    armix::storage::FxState plugin;
    plugin.name = "Test FX";
    plugin.path = "C:/Nonexistent/Test FX.vst3";
    plugin.uid = 982314;
    plugin.catalogId = "test-fx-982314";
    plugin.bypass = true;
    const char bytes[] = { '\x01', '\x00', '\x02', '\x7f', '\x01', '\x12' };
    plugin.binary.append (bytes, sizeof (bytes));
    original.effects.push_back (std::move (plugin));
    const auto error = archive.save (original);
    if (error.isNotEmpty()) std::cerr << "SAVE: " << error << "\n";
    assert (error.isEmpty());

    armix::storage::Snapshot loaded;
    const auto loadError = archive.load (original.name, loaded);
    if (loadError.isNotEmpty()) std::cerr << "LOAD: " << loadError << "\n";
    assert (loadError.isEmpty());
    assert (loaded.name == original.name);
    assert (std::abs (loaded.channelDb - original.channelDb) < 1e-4f);
    assert (std::abs (loaded.masterDb - original.masterDb) < 1e-4f);
    assert (std::abs (loaded.pan - original.pan) < 1e-4f);
    assert (loaded.channelMute && !loaded.masterMute);
    assert (loaded.effects.size() == 1);
    assert (loaded.effects[0].path == original.effects[0].path);
    assert (loaded.effects[0].uid == original.effects[0].uid);
    assert (loaded.effects[0].catalogId == original.effects[0].catalogId);
    assert (loaded.effects[0].bypass);
    assert (loaded.effects[0].binary == original.effects[0].binary);

    // Malformed ZIP must not replace previously validated snapshot state.
    const auto existingName = loaded.name;
    {
        juce::FileOutputStream out (archive.fileFor (original.name));
        assert (out.openedOk());
        assert (out.setPosition (0));
        assert (out.writeByte ('?'));
        out.flush();
    }
    auto failure = archive.load (original.name, loaded);
    assert (failure.isNotEmpty());
    assert (loaded.name == existingName && loaded.effects.size() == 1);

    folder.deleteRecursively();
    std::cout << "PASS: native ZIP round-trip, binary VST state, mixer values, corruption refusal\n";
    return 0;
}
