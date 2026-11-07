// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
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

    static std::optional<juce::PluginDescription> resolve(
            juce::AudioPluginFormat& format,const juce::String& path,int uid) {
        if(!format.fileMightContainThisPluginType(path))return {};
        DescriptionCache cache;
        if(auto saved=cache.find(path,uid))return saved;
        const auto stamp=moduleStamp(juce::File(path));
        Descriptions discovered;
        format.findAllTypesForFile(discovered,path);
        // A module updated while scanning must not seed a warm cache.
        if(stamp.isNotEmpty()&&stamp==moduleStamp(juce::File(path)))
            cache.remember(discovered);
        for(const auto* item:discovered)
            if(item && item->uniqueId==uid && !item->isInstrument)return *item;
        return {};
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
            juce::XmlElement root("VST3_DESCRIPTIONS_V1");
            root.setAttribute("stamp",stamp);
            for(const auto* item:items)root.addChildElement(item->createXml().release());
            if(!root_.createDirectory().wasOk())return;
            const auto xml=root.toString();
            if(xml.getNumBytesAsUTF8()<=maxBytes)fileFor(path).replaceWithText(xml);
        }
    }

    std::optional<juce::PluginDescription> find(const juce::String& path,int uid) const {
        const auto descriptions=read(path);
        for(const auto* item:descriptions)
            if(item->uniqueId==uid && !item->isInstrument)return *item;
        return {};
    }

    Descriptions read(const juce::String& path) const {
        Descriptions descriptions;
        if(!juce::File::isAbsolutePath(path))return descriptions;
        const auto file=fileFor(path);
        if(!file.existsAsFile()||file.getSize()>maxBytes)return descriptions;
        const auto stamp=moduleStamp(juce::File(path));
        if(stamp.isEmpty())return descriptions;
        const auto root=juce::XmlDocument::parse(file);
        if(!root||!root->hasTagName("VST3_DESCRIPTIONS_V1")||
           root->getStringAttribute("stamp")!=stamp)return descriptions;
        for(auto* child:root->getChildIterator()) {
            auto item=std::make_unique<juce::PluginDescription>();
            if(!item->loadFromXml(*child) || item->fileOrIdentifier!=path ||
               item->pluginFormatName!="VST3")return {};
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
        if(!module.hasFileExtension("vst3")||!module.exists())return {};
        juce::StringArray parts;
        const auto add=[&](const juce::File& file) {
            parts.add(file.getFullPathName()+"|"+juce::String(file.getSize())+"|"+
                juce::String(file.getLastModificationTime().toMilliseconds())+"|"+
                juce::String(file.getCreationTime().toMilliseconds()));
        };
        if(module.existsAsFile())add(module);
        else {
            // Only known VST3 binary/manifest paths, not vendor asset trees.
            const auto contents=module.getChildFile("Contents");
            const auto binary=contents.getChildFile("x86_64-win").getChildFile(module.getFileName());
            if(!binary.existsAsFile())return {};
            add(binary);
            const auto manifest=contents.getChildFile("Resources").getChildFile("moduleinfo.json");
            if(manifest.existsAsFile())add(manifest);
        }
        const auto stamp=parts.joinIntoString("\n");
        return juce::SHA256(stamp.toRawUTF8(),stamp.getNumBytesAsUTF8()).toHexString();
    }
};
}
