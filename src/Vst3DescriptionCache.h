// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include "Vst3ScanIsolation.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>
#include <map>
#include <optional>

namespace armix::vst {
// Machine-local discovery metadata. The host still creates a fresh processor
// and restores its state. No module scanning or file IO belongs in the callback.
class DescriptionCache final {
public:
    using Descriptions=juce::OwnedArray<juce::PluginDescription>;

    // WaveShell can expose hundreds of classes from the same module.
    // Enumeration index and JUCE's 32-bit UID alone cannot disambiguate.
    // Reject all ambiguous matches rather than load a different product.
    static std::optional<juce::PluginDescription> choose(
            const Descriptions& descriptions,int uid,
            const juce::String& name={}) {
        const juce::PluginDescription* selected=nullptr;
        for(const auto* item:descriptions) {
            if(!item||item->isInstrument||item->uniqueId!=uid||
               (name.isNotEmpty()&&!item->name.equalsIgnoreCase(name)))continue;
            if(selected)return {};
            selected=item;
        }
        return selected?std::optional<juce::PluginDescription>(*selected):std::nullopt;
    }

    static std::optional<juce::PluginDescription> resolve(
            juce::AudioPluginFormat& format,const juce::String& path,int uid,
            const juce::String& expectedName={}) {
        if(!format.fileMightContainThisPluginType(path)||ScanIsolation::blocked(path))return {};
        DescriptionCache cache;
        if(auto saved=cache.find(path,uid,expectedName))return saved;
        const auto stamp=moduleStamp(juce::File(path));
        Descriptions discovered;
        if(ScanIsolation::inspect(path,discovered)!=ScanIsolation::Outcome::ok)return {};
        // A module updated while scanning must not seed a warm cache.
        if(stamp.isNotEmpty()&&stamp==moduleStamp(juce::File(path)))
            cache.remember(discovered);
        return choose(discovered,uid,expectedName);
    }

    explicit DescriptionCache(juce::File root=storage::PortableWorkspace::machineDataRoot()
            .getChildFile("Vst3Descriptions")) : root_(std::move(root)) {}

    void remember(const Descriptions& descriptions) const {
        std::map<juce::String,std::vector<const juce::PluginDescription*>> modules;
        for(const auto* item:descriptions)
            if(item && item->pluginFormatName=="VST3" &&
               juce::File::isAbsolutePath(item->fileOrIdentifier))
                modules[item->fileOrIdentifier].push_back(item);
        for(const auto& [path,items]:modules) {
            const auto stamp=moduleStamp(juce::File(path));
            if(stamp.isEmpty())continue;
            juce::XmlElement root("VST3_DESCRIPTIONS_V2");
            root.setAttribute("stamp",stamp);
            for(const auto* item:items)root.addChildElement(item->createXml().release());
            if(!root_.createDirectory().wasOk())return;
            const auto xml=root.toString();
            if(xml.getNumBytesAsUTF8()<=maxBytes)fileFor(path).replaceWithText(xml);
        }
    }

    std::optional<juce::PluginDescription> find(const juce::String& path,int uid,
            const juce::String& name={}) const {
        const auto descriptions=read(path);
        return choose(descriptions,uid,name);
    }

    Descriptions read(const juce::String& path) const {
        Descriptions descriptions;
        if(!juce::File::isAbsolutePath(path)||ScanIsolation::blocked(path))return descriptions;
        const auto file=fileFor(path);
        if(!file.existsAsFile()||file.getSize()>maxBytes)return descriptions;
        const auto stamp=moduleStamp(juce::File(path));
        if(stamp.isEmpty())return descriptions;
        const auto text=file.loadFileAsString();
        if(text.contains("<!")||text.getNumBytesAsUTF8()>maxBytes||
           text.retainCharacters("<").length()>ScanIsolation::maxDescriptions*2+4)return descriptions;
        const auto root=juce::XmlDocument::parse(text);
        if(!root||!root->hasTagName("VST3_DESCRIPTIONS_V2")||
           root->getStringAttribute("stamp")!=stamp)return descriptions;
        for(auto* child:root->getChildIterator()) {
            auto item=std::make_unique<juce::PluginDescription>();
            if(child->getNumChildElements()!=0||!item->loadFromXml(*child) || item->fileOrIdentifier!=path ||
               !ScanIsolation::validDescription(*item,path)||descriptions.size()>=ScanIsolation::maxDescriptions)return {};
            descriptions.add(item.release());
        }
        return descriptions;
    }

private:
    static constexpr int maxBytes=1024*1024;
    juce::File root_;

    juce::File fileFor(const juce::String& path) const {
        return root_.getChildFile(juce::SHA256(path.toRawUTF8(),
            path.getNumBytesAsUTF8()).toHexString()+".xml");
    }

    static juce::String moduleStamp(const juce::File& module) {
        return ScanIsolation::moduleStamp(module);
    }
};
}
