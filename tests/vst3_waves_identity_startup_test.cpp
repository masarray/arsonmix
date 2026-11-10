// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/Vst3DescriptionCache.h"
#include "../src/StartupVst3RecoveryGuard.h"
#include <cassert>
#include <memory>

int main() {
    using armix::vst::DescriptionCache;
    DescriptionCache::Descriptions shell;
    auto add=[&](const juce::String& name,int uid) {
        auto d=std::make_unique<juce::PluginDescription>();
        d->pluginFormatName="VST3";
        d->fileOrIdentifier="C:\\VST3\\WaveShell1-VST3 17.1.vst3";
        d->manufacturerName="Waves";
        d->name=name;d->uniqueId=uid;d->isInstrument=false;
        shell.add(d.release());
    };
    // Synthetic UID collision and unstable WaveShell class order. The
    // visible catalog name must NEVER resolve to another Waves product.
    add("Saphira Stereo",42);
    add("GW MixCentric Mono",42);
    assert(DescriptionCache::choose(shell,42,"Saphira Stereo")->name=="Saphira Stereo");
    assert(DescriptionCache::choose(shell,42,"GW MixCentric Mono")->name=="GW MixCentric Mono");
    assert(!DescriptionCache::choose(shell,42)); // arbitrary first forbidden
    assert(!DescriptionCache::choose(shell,42,"Unknown product"));
    assert(!DescriptionCache::choose(shell,9,"Saphira Stereo"));
    // Duplicate name + UID must not select the first arbitrary class.
    add("Saphira Stereo",42);
    assert(!DescriptionCache::choose(shell,42,"Saphira Stereo"));

    const auto temp=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("arsonmix-vst3-crash-guard-"+juce::Uuid().toString());
    assert(temp.createDirectory().wasOk());
    const auto session=temp.getChildFile("last-used.smix");
    assert(session.replaceWithText("PRESERVE_USER_SESSION"));
    {
        armix::startup::Vst3RestoreCrashGuard first(temp);
        assert(!first.interrupted());
        assert(first.arm());
        assert(first.interrupted());
        // An actual plugin fault terminates the host before any C++ catch.
    }
    armix::startup::Vst3RestoreCrashGuard nextLaunch(temp);
    assert(nextLaunch.interrupted());
    assert(!nextLaunch.arm());
    assert(session.loadFileAsString()=="PRESERVE_USER_SESSION");
    // In safe recovery, an explicit retry must keep the marker during
    // potentially crashing vendor creation and for the stability window.
    assert(nextLaunch.armExplicitRetry());
    assert(nextLaunch.interrupted());
    assert(session.loadFileAsString()=="PRESERVE_USER_SESSION");
    // Another simulated crash during manual Open still blocks auto replay.
    armix::startup::Vst3RestoreCrashGuard retryAfterCrash(temp);
    assert(retryAfterCrash.interrupted());
    assert(!retryAfterCrash.arm());
    assert(retryAfterCrash.armExplicitRetry());
    assert(retryAfterCrash.interrupted());
    // Successful manual Open plus stable timer/graceful shutdown clears it;
    // next application launch can safely resume normal automatic restores.
    assert(retryAfterCrash.complete());
    assert(!retryAfterCrash.interrupted());
    assert(retryAfterCrash.arm());
    assert(retryAfterCrash.complete());
    assert(session.loadFileAsString()=="PRESERVE_USER_SESSION");
    assert(temp.deleteRecursively());
}
