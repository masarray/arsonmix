// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>
#include "PortableWorkspace.h"
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace armix::vst {
// Process isolation for discovery only. Native FX processing stays in-process.
// This is a crash boundary, NOT an OS privilege/security sandbox.
struct ScanIsolation final {
    using Descriptions = juce::OwnedArray<juce::PluginDescription>;
    using Cancel = std::function<bool()>;
    enum class Outcome { ok, unavailable, busy, cancelled, timeout, failed, quarantined, changed };
    static constexpr int maxBytes = 1024 * 1024;
    static constexpr int maxDescriptions = 1024;
    static constexpr int timeoutMs = 30000;
    static constexpr const char* workerFlag = "--arsonmix-vst3-scan-worker";
    static constexpr const char* tempPrefix = "arsonmix-vst3-probe-";

    static juce::String message(Outcome o) {
        switch(o) {
            case Outcome::ok: return {};
            case Outcome::unavailable: return "VST3 scanner unavailable";
            case Outcome::busy: return "VST3 scanner busy; retry after current scan";
            case Outcome::cancelled: return "VST3 scan cancelled";
            case Outcome::timeout: return "VST3 scan timed out; quarantined";
            case Outcome::failed: return "VST3 scan failed or crashed; quarantined";
            case Outcome::quarantined: return "VST3 quarantined; explicitly import to retry";
            case Outcome::changed: return "VST3 changed during scan; rescan required";
        }
        return "VST3 scan failed";
    }

    // Cheap metadata fingerprint, not a signature or publisher verification.
    // Include known binary directories on all supported platforms; no vendor
    // asset tree traversal or hashing in realtime code.
    static juce::String moduleStamp(const juce::File& module) {
        if (!module.hasFileExtension("vst3") || !module.exists()) return {};
        juce::StringArray parts;
        const auto add = [&](const juce::File& f) {
            parts.add(f.getFullPathName()+"|"+juce::String(f.getSize())+"|"+
                juce::String(f.getLastModificationTime().toMilliseconds())+"|"+
                juce::String(f.getCreationTime().toMilliseconds()));
        };
        add(module);
        if (module.isDirectory()) {
            auto contents=module.getChildFile("Contents");
            auto dirs=juce::StringArray{"x86_64-win","arm64-win","MacOS","x86_64-linux","aarch64-linux"};
            int count=0;
            for (const auto& name:dirs) {
                auto dir=contents.getChildFile(name);
                if (!dir.isDirectory()) continue;
                add(dir);
                for (const auto& entry:juce::RangedDirectoryIterator(dir,false,"*",juce::File::findFiles)) {
                    if (++count>64) return {};
                    add(entry.getFile());
                }
            }
            const auto manifest=contents.getChildFile("Resources/moduleinfo.json");
            if (manifest.existsAsFile()) add(manifest);
        }
        parts.sort(false);
        const auto stamp=parts.joinIntoString("\n");
        return juce::SHA256(stamp.toRawUTF8(),stamp.getNumBytesAsUTF8()).toHexString();
    }

    static juce::File quarantineFile(const juce::String& path,
            const juce::File& root=storage::PortableWorkspace::machineDataRoot().getChildFile("Vst3ScanQuarantine")) {
        return root.getChildFile(juce::SHA256(path.toRawUTF8(),
                    path.getNumBytesAsUTF8()).toHexString()+".txt");
    }
    static bool blocked(const juce::String& path,
            const juce::File& root=storage::PortableWorkspace::machineDataRoot().getChildFile("Vst3ScanQuarantine")) {
        const auto file=quarantineFile(path,root);
        const auto stamp=moduleStamp(juce::File(path));
        return file.existsAsFile() && (file.getSize()>256 ||
            (stamp.isNotEmpty() && file.loadFileAsString().trim()==stamp));
    }
    static bool markFailure(const juce::String& path,const juce::String& stamp,
            const juce::File& root=storage::PortableWorkspace::machineDataRoot().getChildFile("Vst3ScanQuarantine")) {
        return stamp.isNotEmpty() && root.createDirectory().wasOk() &&
            quarantineFile(path,root).replaceWithText(stamp+"\n");
    }

    // The child cannot fill a captured stdout/stderr pipe and deadlock the
    // supervisor. Cancellation and timeout never terminate a JUCE thread.
    // getExitCode alone is insufficient; parent also checks response receipt.
    static Outcome launchAndWait(const juce::StringArray& args,int budgetMs,
                                 const Cancel& cancel={}) {
        if (cancel && cancel()) return Outcome::cancelled;
        juce::ChildProcess child;
        if (!child.start(args,0)) return Outcome::unavailable;
        const auto started=juce::Time::getMillisecondCounter();
        while (child.isRunning()) {
            const bool stopping=cancel && cancel();
            if (stopping || juce::Time::getMillisecondCounter()-started>=static_cast<juce::uint32>(budgetMs)) {
                // kill() is OS best-effort; no unbounded wait on a vendor.
                if (child.isRunning()) {
                    child.kill();
                    child.waitForProcessToFinish(1000);
                }
                return stopping?Outcome::cancelled:Outcome::timeout;
            }
            juce::Thread::sleep(25);
        }
        return child.getExitCode()==0?Outcome::ok:Outcome::failed;
    }

    static bool validDescription(const juce::PluginDescription& d,const juce::String& path) {
        return d.pluginFormatName=="VST3" && d.fileOrIdentifier==path &&
            d.name.isNotEmpty() && d.name.length()<=512 &&
            d.descriptiveName.length()<=512 && d.manufacturerName.length()<=512 &&
            d.category.length()<=512 && d.version.length()<=128 &&
            d.numInputChannels>=0 && d.numInputChannels<=4096 &&
            d.numOutputChannels>=0 && d.numOutputChannels<=4096;
    }

    static bool readReceipt(const juce::File& file,const juce::String& nonce,
                            const juce::String& path,Descriptions& out) {
        if (!file.existsAsFile() || file.isSymbolicLink() || file.getSize()<=0 ||
            file.getSize()>maxBytes) return false;
        const auto data=file.loadFileAsString();
        // No DTD/entity declarations or arbitrarily nested markup.
        if (data.contains("<!") || data.getNumBytesAsUTF8()>maxBytes ||
            data.retainCharacters("<").length()>maxDescriptions*2+4) return false;
        const auto xml=juce::XmlDocument::parse(data);
        if (!xml || !xml->hasTagName("ARSONMIX_VST3_SCAN_V1") ||
            xml->getStringAttribute("nonce")!=nonce ||
            xml->getStringAttribute("path")!=path ||
            xml->getNumChildElements()>maxDescriptions) return false;
        Descriptions parsed;
        for (auto* node:xml->getChildIterator()) {
            if (!node->hasTagName("PLUGIN") || node->getNumChildElements()!=0) return false;
            auto d=std::make_unique<juce::PluginDescription>();
            if (!d->loadFromXml(*node) || !validDescription(*d,path)) return false;
            parsed.add(d.release());
        }
        out=std::move(parsed);
        return true;
    }

    static Outcome inspect(const juce::String& path,Descriptions& out,
                           bool explicitRetry=false,const Cancel& cancel={}) {
        out.clear();
        if (!juce::File::isAbsolutePath(path) || path.length()>4096) return Outcome::unavailable;
        const auto stamp=moduleStamp(juce::File(path));
        if (stamp.isEmpty()) return Outcome::unavailable;
        // Avoid competing helpers (including simultaneous control-path
        // cache miss and catalog scan). Never block waiting for this lock.
        static std::mutex serial;
        const std::unique_lock<std::mutex> lock(serial,std::try_to_lock);
        if (!lock.owns_lock()) return Outcome::busy;
        const bool previouslyBlocked=blocked(path);
        if (!explicitRetry && previouslyBlocked) return Outcome::quarantined;
        if (cancel && cancel()) return Outcome::cancelled;
        const auto nonce=juce::Uuid().toString().removeCharacters("-{}");
        const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(juce::String(tempPrefix)+nonce);
        if (dir.exists() || dir.createDirectory().failed()) return Outcome::unavailable;
        struct Cleanup { juce::File dir; ~Cleanup(){dir.deleteRecursively();} } cleanup{dir};
        const auto receipt=dir.getChildFile("result.xml");
        // Arm persistent evidence before loading any third-party code in
        // the child. A killed parent must not endlessly replay this module.
        if (!markFailure(path,stamp)) return Outcome::unavailable;
        const auto executable=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        auto result=launchAndWait(juce::StringArray{executable.getFullPathName(),
                       workerFlag,path,nonce},timeoutMs,cancel);
        if (result==Outcome::ok && !readReceipt(receipt,nonce,path,out)) result=Outcome::failed;
        if (stamp!=moduleStamp(juce::File(path))) {
            out.clear();
            return Outcome::changed;
        }
        if (result==Outcome::ok) {
            if (!quarantineFile(path).deleteFile()) { out.clear(); return Outcome::unavailable; }
        } else {
            out.clear();
            // No plugin fault implied by a failed launch or user cancellation.
            // Keep earlier quarantine on a cancelled explicit retry.
            if ((result==Outcome::unavailable || result==Outcome::cancelled) && !previouslyBlocked)
                quarantineFile(path).deleteFile();
        }
        return result;
    }

    // The same installed executable enters this mode before the mixer mutex,
    // splash, device/session initialization, or updater. No IPC command shell.
    static bool isWorker(const juce::StringArray& args) {
        return !args.isEmpty() && args[0]==workerFlag;
    }
   #if JUCE_PLUGINHOST_VST3
    static int worker(const juce::StringArray& args) {
        if (args.size()!=3 || !isWorker(args) || !juce::File::isAbsolutePath(args[1]) ||
            args[1].length()>4096 || args[2].length()!=32 ||
            args[2].retainCharacters("0123456789abcdefABCDEF")!=args[2]) return 2;
        // Child-only last resort if the supervising mixer disappears. No
        // shared references, no audio threads, no wait in normal shutdown.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs+5000));
            std::_Exit(124);
        }).detach();
        const auto path=args[1], nonce=args[2];
        const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(juce::String(tempPrefix)+nonce);
        if (!dir.isDirectory() || dir.isSymbolicLink() || dir.getChildFile("result.xml").exists() ||
            moduleStamp(juce::File(path)).isEmpty()) return 2;
        juce::VST3PluginFormat format;
        if (!format.fileMightContainThisPluginType(path)) return 3;
        Descriptions descriptions;
        format.findAllTypesForFile(descriptions,path);
        if (descriptions.size()>maxDescriptions) return 4;
        juce::XmlElement xml("ARSONMIX_VST3_SCAN_V1");
        xml.setAttribute("nonce",nonce);
        xml.setAttribute("path",path);
        for (const auto* item:descriptions) {
            if (!item || !validDescription(*item,path)) return 4;
            xml.addChildElement(item->createXml().release());
        }
        const auto data=xml.toString();
        if (data.getNumBytesAsUTF8()>maxBytes) return 4;
        return dir.getChildFile("result.xml").replaceWithText(data)?0:5;
    }
   #endif
};
} // namespace armix::vst
