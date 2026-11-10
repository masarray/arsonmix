// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PortableWorkspace.h"
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// CONTROL / IO THREAD ONLY. A per-built-in FX sound is deliberately NOT a
// .smix session or a historical .armix multi-insert My Sounds archive.
// This data service never touches a published JUCE AudioProcessor. The host
// must capture the exact accepted DSP readback before save and must apply a
// recalled preset through the existing audio-owner SPSC / ACK transaction.
namespace armix::storage {
struct BuiltInFxUserPreset {
    juce::String id,catalogId,name,category;
    juce::var settings;
};
class BuiltInFxUserPresetStore final {
public:
    explicit BuiltInFxUserPresetStore(juce::File workspace=PortableWorkspace::dataRoot())
        : root_(workspace.getChildFile("BuiltInFxPresets")) {}

    static constexpr juce::int64 maxBytes=16*1024;
    static constexpr int maxFilesPerEffect=512;
    static constexpr int schemaVersion=1;

    static juce::String effectFolder(const juce::String& catalog) {
        if(catalog=="armix.builtin.channel-eq.v1")return "eq";
        if(catalog=="armix.builtin.channel-compressor.v1")return "compressor";
        if(catalog=="armix.builtin.arson-harmony.v1")return "harmonice";
        if(catalog=="armix.builtin.vocal-throw.v1")return "vocal-throw";
        return {};
    }
    static bool safeText(const juce::String& value,int maxLen) {
        if(value.isEmpty()||value.length()>maxLen||value.trim()!=value)
            return false;
        for(const auto ch:value)
            if(ch<32||ch==127||ch=='/'||ch=='\\')return false;
        return true;
    }
    static bool safeId(const juce::String& id) {
        if(id.length()!=36)return false;
        for(int i=0;i<36;++i) {
            const auto ch=id[i];
            if(i==8||i==13||i==18||i==23){if(ch!='-')return false;}
            else if(!((ch>='0'&&ch<='9')||
                       (ch>='a'&&ch<='f')||
                       (ch>='A'&&ch<='F')))return false;
        }
        return true;
    }
    static bool number(const juce::var& obj,const char* key,double a,double b) {
        const auto v=obj.getProperty(key,{});
        if(!(v.isInt()||v.isDouble()))return false;
        const double x=static_cast<double>(v);
        return std::isfinite(x)&&x>=a&&x<=b;
    }
    static bool integer(const juce::var& obj,const char* key,int a,int b) {
        const auto v=obj.getProperty(key,{});
        return v.isInt()&&static_cast<int>(v)>=a&&static_cast<int>(v)<=b;
    }
    static bool boolean(const juce::var& obj,const char* key) {
        return obj.getProperty(key,{}).isBool();
    }
    static bool containsOnly(const juce::var& obj,const juce::StringArray& names) {
        if(!obj.isObject())return false;
        const auto& properties=obj.getDynamicObject()->getProperties();
        for(int i=0;i<properties.size();++i)
            if(!names.contains(properties.getName(i).toString()))return false;
        return true;
    }

    static bool validateSettings(const juce::String& catalog,const juce::var& settings) {
        if(!settings.isObject()||effectFolder(catalog).isEmpty())return false;
        if(catalog=="armix.builtin.channel-compressor.v1"){
            if(!containsOnly(settings,{"thresholdDb","ratio","attackMs","releaseMs",
                    "kneeDb","makeupDb","mix"}))return false;
            return number(settings,"thresholdDb",-60,0)&&number(settings,"ratio",1,20)&&
                number(settings,"attackMs",.1,100)&&number(settings,"releaseMs",10,2000)&&
                number(settings,"kneeDb",0,24)&&number(settings,"makeupDb",-12,24)&&
                number(settings,"mix",0,1);
        }
        if(catalog=="armix.builtin.arson-harmony.v1"){
            // Source ChannelId is session-specific and intentionally forbidden
            // in the portable user library. Recall reuses the host's current
            // source or falls safely back to Manual Key.
            if(!containsOnly(settings,{"key","scale","voices","correction",
                        "wet","space","color","gainDb","voiceMode"}))return false;
            const auto mode=settings.getProperty("voiceMode",{});
            return integer(settings,"key",0,11)&&integer(settings,"scale",0,1)&&
                integer(settings,"voices",2,5)&&
                (mode.isVoid()||(mode.isInt()&&static_cast<int>(mode)>=0&&
                                    static_cast<int>(mode)<=1))&&
                boolean(settings,"correction")&&number(settings,"wet",0,1)&&
                number(settings,"space",0,1)&&number(settings,"color",0,1)&&
                number(settings,"gainDb",-24,12);
        }
        if(catalog=="armix.builtin.vocal-throw.v1"){
            if(!containsOnly(settings,{"mode","bpm","division","repeats","captureMs",
                    "silenceMs","thresholdDb","wet","decay","ducking"}))return false;
            return integer(settings,"mode",0,2)&&number(settings,"bpm",60,200)&&
                integer(settings,"division",0,3)&&integer(settings,"repeats",1,6)&&
                number(settings,"captureMs",80,1500)&&number(settings,"silenceMs",80,900)&&
                number(settings,"thresholdDb",-65,-15)&&number(settings,"wet",0,1)&&
                number(settings,"decay",.15,.9)&&number(settings,"ducking",0,1);
        }
        if(catalog=="armix.builtin.channel-eq.v1"){
            if(!containsOnly(settings,{"bands"}))return false;
            const auto bands=settings.getProperty("bands",{});
            if(!bands.isArray()||bands.getArray()->size()!=8)return false;
            for(const auto& band:*bands.getArray()){
                if(!containsOnly(band,{"enabled","frequencyHz","gainDb","q","slope"})||
                   !boolean(band,"enabled")||!number(band,"frequencyHz",20,20000)||
                   !number(band,"gainDb",-18,18)||!number(band,"q",.2,12)||
                   !(integer(band,"slope",12,12)||integer(band,"slope",24,24)))
                    return false;
            }
            return true;
        }
        return false;
    }

    juce::String saveNew(BuiltInFxUserPreset& item) const {
        if(!safeText(item.name,80)||!safeText(item.category,40)||
           !validateSettings(item.catalogId,item.settings))
            return "Preset name/category/parameters invalid";
        // Count physical files, not only valid parsed items. A corrupted
        // 513-file library must not look empty and silently bypass quota.
        // Fail closed on corrupt entries before creating a new user file.
        juce::Array<juce::File> files;
        folder(item.catalogId).findChildFiles(
            files,juce::File::findFiles,false,"*.armixfx");
        if(files.size()>=maxFilesPerEffect)
            return "Per-effect user preset quota reached";
        for(const auto& file:files) {
            BuiltInFxUserPreset existing;
            if(read(item.catalogId,file.getFileNameWithoutExtension(),
                    existing).isNotEmpty())
                return "Preset library contains an invalid/corrupt entry";
        }
        auto list=enumerate(item.catalogId);
        for(const auto& existing:list)
            if(existing.name.equalsIgnoreCase(item.name)&&
               existing.category.equalsIgnoreCase(item.category))
                return "Preset name already exists in this effect/category";
        const auto uuid=juce::Uuid().toDashedString().toLowerCase();
        if(!safeId(uuid))return "Unable to allocate preset ID";
        item.id=uuid;
        return persist(item,false);
    }

    juce::String overwrite(const BuiltInFxUserPreset& item) const {
        BuiltInFxUserPreset old;
        const auto error=read(item.catalogId,item.id,old);
        if(error.isNotEmpty())return error;
        if(!safeText(item.name,80)||!safeText(item.category,40)||
           !validateSettings(item.catalogId,item.settings))
            return "Invalid user preset edit";
        for(const auto& other:enumerate(item.catalogId))
            if(other.id!=item.id&&other.name.equalsIgnoreCase(item.name)&&
               other.category.equalsIgnoreCase(item.category))
                return "Name collision with another user preset";
        return persist(item,true);
    }

    juce::String rename(const juce::String& catalog,const juce::String& id,
                        const juce::String& newName) const {
        BuiltInFxUserPreset old;
        if(const auto error=read(catalog,id,old);error.isNotEmpty())return error;
        old.name=newName;
        return overwrite(old);
    }

    juce::String remove(const juce::String& catalog,const juce::String& id) const {
        BuiltInFxUserPreset existing;
        if(const auto error=read(catalog,id,existing);error.isNotEmpty())return error;
        const auto file=filename(catalog,id);
        return file.deleteFile()?juce::String{}:"Unable to delete user preset";
    }

    juce::String read(const juce::String& catalog,const juce::String& id,
                      BuiltInFxUserPreset& result) const {
        if(effectFolder(catalog).isEmpty()||!safeId(id))
            return "Invalid effect or preset ID";
        const auto file=filename(catalog,id);
        if(!file.existsAsFile()||file.getSize()<1||file.getSize()>maxBytes)
            return "Preset missing or exceeds size budget";
        const auto json=file.loadFileAsString();
        if(json.getNumBytesAsUTF8()>maxBytes)return "Preset size budget exceeded";
        auto record=juce::JSON::parse(json);
        if(!record.isObject()||
           !containsOnly(record,{"schemaVersion","presetId","catalogId",
                                 "name","category","settings","sha256"}))
            return "Preset JSON schema invalid";
        if(!integer(record,"schemaVersion",schemaVersion,schemaVersion)||
           !record.getProperty("presetId","").toString().equalsIgnoreCase(id)||
           record.getProperty("catalogId","").toString()!=catalog)
            return "Preset version/identity mismatch";
        BuiltInFxUserPreset entry{id,catalog,record.getProperty("name","").toString(),
            record.getProperty("category","").toString(),
            record.getProperty("settings",{})};
        if(!safeText(entry.name,80)||!safeText(entry.category,40)||
           !validateSettings(catalog,entry.settings))return "Preset parameters invalid";
        const auto payload=juce::JSON::toString(entry.settings,false);
        const auto hash=juce::SHA256(payload.toRawUTF8(),
            static_cast<size_t>(payload.getNumBytesAsUTF8())).toHexString();
        if(record.getProperty("sha256","").toString()!=hash)
            return "Preset settings checksum mismatch";
        result=std::move(entry);
        return {};
    }

    std::vector<BuiltInFxUserPreset> enumerate(const juce::String& catalog) const {
        std::vector<BuiltInFxUserPreset> items;
        if(effectFolder(catalog).isEmpty())return items;
        juce::Array<juce::File> files;
        folder(catalog).findChildFiles(files,juce::File::findFiles,false,"*.armixfx");
        if(files.size()>maxFilesPerEffect)return items; // fail-closed; no unbounded scan
        for(const auto& file:files){
            BuiltInFxUserPreset entry;
            if(read(catalog,file.getFileNameWithoutExtension(),entry).isEmpty())
                items.push_back(std::move(entry));
        }
        std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){
            const auto order=a.category.compareIgnoreCase(b.category);
            return order?order<0:a.name.compareIgnoreCase(b.name)<0;
        });
        return items;
    }
private:
    juce::File folder(const juce::String& catalog) const {
        return root_.getChildFile(effectFolder(catalog));
    }
    juce::File filename(const juce::String& catalog,const juce::String& id) const {
        if(effectFolder(catalog).isEmpty()||!safeId(id))return {};
        return folder(catalog).getChildFile(id.toLowerCase()+".armixfx");
    }
    juce::String persist(const BuiltInFxUserPreset& entry,bool replacing) const {
        if(!safeId(entry.id)||effectFolder(entry.catalogId).isEmpty()||
           !validateSettings(entry.catalogId,entry.settings))
            return "Preset identity/parameters invalid";
        const auto dir=folder(entry.catalogId);
        if(!dir.isDirectory()&&dir.createDirectory().failed())
            return "Cannot create user preset directory";
        const auto file=filename(entry.catalogId,entry.id);
        if(file.existsAsFile()!=replacing)
            return "Preset write conflict; old data preserved";
        const auto payload=juce::JSON::toString(entry.settings,false);
        const auto hash=juce::SHA256(payload.toRawUTF8(),
             static_cast<size_t>(payload.getNumBytesAsUTF8())).toHexString();
        auto obj=juce::var(new juce::DynamicObject());
        auto* fields=obj.getDynamicObject();
        fields->setProperty("schemaVersion",schemaVersion);
        fields->setProperty("presetId",entry.id);
        fields->setProperty("catalogId",entry.catalogId);
        fields->setProperty("name",entry.name);
        fields->setProperty("category",entry.category);
        fields->setProperty("settings",entry.settings);
        fields->setProperty("sha256",hash);
        const auto json=juce::JSON::toString(obj,false);
        if(json.getNumBytesAsUTF8()>maxBytes)return "Preset exceeds size budget";
        juce::TemporaryFile stage(file);
        if(!stage.getFile().replaceWithText(json))return "Could not stage user preset";
        // Verify stage content without publishing a partial file.
        const auto candidate=juce::JSON::parse(stage.getFile().loadFileAsString());
        if(!candidate.isObject()||
           candidate.getProperty("sha256","").toString()!=hash)
            return "Staged preset verification failed";
        return stage.overwriteTargetFileWithTemporary()?juce::String{}:
            juce::String("Could not commit user preset atomically");
    }
    juce::File root_;
};
} // namespace armix::storage
