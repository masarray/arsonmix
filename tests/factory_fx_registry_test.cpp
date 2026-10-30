// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/FactoryFxRegistry.h"
#include <iostream>

namespace {
bool check (bool ok, const char* message)
{
    if (! ok) std::cerr << "FAIL FactoryFX registry: " << message << "\n";
    return ok;
}
}

int main()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("armix-factory-test-" + juce::Uuid().toString());
    struct Cleanup final
    {
        juce::File folder;
        ~Cleanup() { folder.deleteRecursively(); }
    } cleanup { root };
    const auto bundle = root.getChildFile ("FactoryFX/ArSonEQ/ArSonEQ.vst3");
    if (! bundle.getParentDirectory().createDirectory().wasOk() ||
        ! bundle.replaceWithText ("original EQ VST3 fixture bytes"))
        return 20;

    const auto sha = juce::SHA256 (bundle).toHexString();
    const auto original = juce::String (R"JSON({
      "schemaVersion":1,"platform":"windows-x64","hostFormat":"VST3",
      "distribution":"engineering-partial",
      "plugins":[{
        "productId":"arson-eq","displayName":"ArSonEQ","category":"EQ",
        "pluginVersion":"0.1.0","sourceRepo":"masarray/arson-eq",
        "sourceCommit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "artifactSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bundleRelativePath":"FactoryFX/ArSonEQ/ArSonEQ.vst3",
        "vst3ClassId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "hostUniqueId":1234,"licenseReview":"approved",
        "entitlementPolicy":"unrestricted",
        "notices":["FactoryFX/licenses/ArSonEQ.txt"],
        "files":[{"path":".","sha256":"__HASH__"}]
      }]
    })JSON").replace ("__HASH__", sha);
    const auto manifest = root.getChildFile ("FactoryFX/factoryfx-manifest.json");
    if (! manifest.replaceWithText (original)) return 21;
    auto scan = armix::factory::Registry::discover (root);
    if (! check (scan.ok() && scan.candidates.size() == 1 &&
                scan.candidates.front().productId == "arson-eq" &&
                scan.candidates.front().hostUniqueId == 1234, "good candidate"))
        return 1;

    bundle.replaceWithText ("tampered EQ data");
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok() && scan.candidates.empty(), "tampered binary rejected"))
        return 2;
    bundle.replaceWithText ("original EQ VST3 fixture bytes");

    manifest.replaceWithText (original.replace ("\"approved\"", "\"pending\""));
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok() && scan.candidates.empty(), "unapproved redistribution rejected"))
        return 3;

    manifest.replaceWithText (original.replace ("FactoryFX/ArSonEQ/ArSonEQ.vst3", "../outside.vst3"));
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok() && scan.candidates.empty(), "path traversal rejected"))
        return 4;

    manifest.replaceWithText (original.replace ("\"hostUniqueId\":1234", "\"hostUniqueId\":\"unknown\""));
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok(), "invalid host unique ID rejected"))
        return 5;

    manifest.replaceWithText (original.replace ("engineering-partial", "production-six"));
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok(), "six-effect production manifest cannot claim one effect"))
        return 6;

    manifest.deleteFile();
    scan = armix::factory::Registry::discover (root);
    if (! check (! scan.ok() && scan.candidates.empty(), "missing manifest rejected"))
        return 7;

    std::cout << "FactoryFX registry: valid package + 6 negative gates passed\n";
    return 0;
}
