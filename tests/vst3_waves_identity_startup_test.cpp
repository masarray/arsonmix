// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/Vst3DescriptionCache.h"
#include "../src/StartupVst3RecoveryGuard.h"
#include <cassert>
#include <memory>
#include <chrono>
#include <string>

int main(int argc,char** argv) {
    using Isolation=armix::vst::ScanIsolation;
    using Outcome=Isolation::Outcome;
    if(argc==2 && std::string(argv[1])=="--fixture-ok")return 0;
    if(argc==2 && std::string(argv[1])=="--fixture-fail")return 7;
    if(argc==2 && std::string(argv[1])=="--fixture-hang") {
        juce::Thread::sleep(4000);
        return 0;
    }
    const auto self=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
    assert(Isolation::launchAndWait({self,"--fixture-ok"},5000)==Outcome::ok);
    assert(Isolation::launchAndWait({self,"--fixture-fail"},5000)==Outcome::failed);
    const auto before=std::chrono::steady_clock::now();
    assert(Isolation::launchAndWait({self,"--fixture-hang"},150)==Outcome::timeout);
    assert(std::chrono::steady_clock::now()-before<std::chrono::seconds(3));
    const auto cancelStart=std::chrono::steady_clock::now();
    assert(Isolation::launchAndWait({self,"--fixture-hang"},5000,[&]{
        return std::chrono::steady_clock::now()-cancelStart>=std::chrono::milliseconds(150);
    })==Outcome::cancelled);
    // Real installed application entrypoint must reject malformed helper
    // arguments without opening the mixer or running the updater.
    if(argc==2) {
        assert(Isolation::launchAndWait({juce::String::fromUTF8(argv[1]),Isolation::workerFlag},5000)==Outcome::failed);
    }
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
    // Machine-local quarantine survives a new object/process and is keyed
    // by module identity. A changed file can be scanned again.
    const auto module=temp.getChildFile("Synthetic plugin.vst3");
    assert(module.replaceWithText("fake VST3 bytes"));
    const auto modulePath=module.getFullPathName();
    const auto quarantine=temp.getChildFile("quarantine");
    const auto stamp=Isolation::moduleStamp(module);
    assert(stamp.isNotEmpty());
    assert(!Isolation::blocked(modulePath,quarantine));
    assert(Isolation::markFailure(modulePath,stamp,quarantine));
    assert(Isolation::blocked(modulePath,quarantine));
    assert(module.appendText("changed"));
    assert(!Isolation::blocked(modulePath,quarantine));
    // Strict receipt binding and schema checks, preserving WaveShell index
    // and both UID fields through the real JUCE descriptor serializer.
    const auto receipt=temp.getChildFile("result.xml");
    juce::PluginDescription d;
    d.name="Test stereo";d.pluginFormatName="VST3";d.fileOrIdentifier=modulePath;
    d.uniqueId=42;d.deprecatedUid=17;d.numInputChannels=2;d.numOutputChannels=2;
    juce::XmlElement response("ARSONMIX_VST3_SCAN_V1");
    response.setAttribute("nonce","abc");response.setAttribute("path",modulePath);
    response.addChildElement(d.createXml().release());
    assert(receipt.replaceWithText(response.toString()));
    Isolation::Descriptions parsed;
    assert(Isolation::readReceipt(receipt,"abc",modulePath,parsed));
    assert(parsed.size()==1&&parsed[0]->uniqueId==42&&parsed[0]->deprecatedUid==17);
    assert(!Isolation::readReceipt(receipt,"wrong",modulePath,parsed));
    assert(!Isolation::readReceipt(receipt,"abc",modulePath+"other",parsed));
    assert(receipt.replaceWithText("<!DOCTYPE x [<!ENTITY bad SYSTEM 'file:///no'>]><x/>"));
    assert(!Isolation::readReceipt(receipt,"abc",modulePath,parsed));
    assert(temp.deleteRecursively());
}
