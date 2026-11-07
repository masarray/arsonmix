// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

// Durable UI identity, not DSP topology. The accepted native ChannelId is the
// address; rename/color/icon never rebuild PCM or touch a realtime callback.
// Disk IO is only performed on the JUCE message thread, never on the audio
// thread. Fail closed on an invalid existing document (no silent overwrite).
namespace armix::appearance {
class Store final {
public:
    explicit Store(juce::File directory)
        : file_(directory.getChildFile("channel-appearance.v1.json"))
    { load(); }

    struct Entry final {juce::String name,color,icon;};
    juce::String error() const {return loadError_;}
    static bool decimal(const juce::var& input,std::uint64_t& value,bool zero=false) {
        if(!input.isString())return false;
        const auto text=input.toString().toStdString();
        if(text.empty()||text.size()>20||(text.size()>1&&text[0]=='0'))return false;
        std::uint64_t next=0;
        auto [end,err]=std::from_chars(text.data(),text.data()+text.size(),next);
        if(err!=std::errc{}||end!=text.data()+text.size()||(!zero&&!next))return false;
        value=next;return true;
    }
    static bool validName(const juce::String& name) {
        const auto utf8=name.toStdString();
        if(name.isEmpty()||name!=name.trim()||name.length()>30||utf8.size()>96)return false;
        for(auto ch:utf8)if(static_cast<unsigned char>(ch)<32||ch==127)return false;
        return true;
    }
    static bool validColor(const juce::String& color) {
        if(color.length()!=7||color[0]!='#')return false;
        for(int i=1;i<7;++i){
            auto c=color[i];
            if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
        }
        return true;
    }
    static bool validIcon(const juce::String& icon) {
        // Compiler-source codepages and JUCE's const-char* overloads differ on
        // Windows. Decode the canonical wire UTF-8 bytes explicitly; no
        // emoji literals or locale-sensitive constructors in native identity.
        static const std::array<juce::String,12> allowed {
            juce::String::fromUTF8("\xE2\x99\xA9"),
            juce::String::fromUTF8("\xE2\x99\xAB"),
            juce::String::fromUTF8("\xE2\x99\xAC"),
            juce::String::fromUTF8("\xF0\x9F\x8E\x99"),
            juce::String::fromUTF8("\xF0\x9F\x8E\xB8"),
            juce::String::fromUTF8("\xF0\x9F\x8E\xB9"),
            juce::String::fromUTF8("\xF0\x9F\xA5\x81"),
            juce::String::fromUTF8("\xF0\x9F\x8E\xA7"),
            juce::String::fromUTF8("\xE2\x96\xA5"),
            juce::String::fromUTF8("\xE2\x97\x89"),
            juce::String::fromUTF8("\xE2\x97\x8E"),
            juce::String::fromUTF8("\xE2\x8A\x9E")
        };
        return std::find(allowed.begin(),allowed.end(),icon)!=allowed.end();
    }

    // Only the accepted control-thread graph may authorize a ChannelId edit.
    // No ChannelId JS Number, no ordinal alias and no stale hardware epoch.
    juce::String apply(const juce::var& request,const juce::var& accepted) {
        if(loadError_.isNotEmpty())return loadError_;
        const auto graph=accepted.getProperty("graph",{});
        const auto device=accepted.getProperty("device",{});
        const auto channels=graph.getProperty("channels",{});
        if(!channels.isArray())return "NO_NATIVE_CHANNEL_GRAPH";
        const auto caps=graph.getProperty("capabilities",{});
        const auto session=accepted.getProperty("mixerSession",{});
        if(session.getProperty("authority","").toString()!="CHANNEL_ID"||
           !(static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false))||
             graph.getProperty("bindingState","").toString()=="NEEDS_REMAP"))
            return "NATIVE_CHANNEL_GRAPH_NOT_ACCEPTED";
        std::uint64_t channel=0,epoch=0,rev=0,roster=0,appearanceRev=0;
        const auto id=request.getProperty("channelId",{});
        if(!decimal(id,channel)||
           !decimal(request.getProperty("deviceEpoch",{}),epoch,true)||
           !decimal(request.getProperty("graphRevision",{}),rev,true)||
           !decimal(request.getProperty("rosterRevision",{}),roster,true)||
           !decimal(request.getProperty("appearanceRevision",{}),appearanceRev,true)||
           epoch==0||rev==0||roster==0||
           device.getProperty("epoch","").toString()!=juce::String(std::to_string(epoch))||
           graph.getProperty("revision","").toString()!=juce::String(std::to_string(rev))||
           graph.getProperty("rosterRevision","").toString()!=juce::String(std::to_string(roster)))
            return "STALE_CHANNEL_IDENTITY_OR_GRAPH";
        if(appearanceRev!=revision_)return "STALE_CHANNEL_APPEARANCE_REVISION";
        const auto nameVar=request.getProperty("name",{});
        const auto colorVar=request.getProperty("color",{});
        const auto iconVar=request.getProperty("icon",{});
        if(!nameVar.isString()||!colorVar.isString()||!iconVar.isString())
            return "INVALID_CHANNEL_APPEARANCE_FIELDS";
        const Entry next{nameVar.toString(),colorVar.toString(),iconVar.toString()};
        if(!validName(next.name)||!validColor(next.color)||!validIcon(next.icon))
            return "INVALID_CHANNEL_NAME_COLOR_OR_ICON";
        bool found=false;
        const auto idText=id.toString();
        for(const auto& row:*channels.getArray()) {
            const auto rowId=row.getProperty("id",{});
            if(!rowId.isString())return "INVALID_NATIVE_ROSTER";
            if(rowId.toString()==idText){found=true;continue;}
            const auto it=entries_.find(rowId.toString().toStdString());
            const auto other=it==entries_.end()?row.getProperty("name","").toString():it->second.name;
            if(next.name.equalsIgnoreCase(other))
                return "CHANNEL_NAME_ALREADY_IN_USE";
        }
        if(!found)return "CHANNEL_NOT_IN_NATIVE_ROSTER";
        const auto key=idText.toStdString();
        const auto existing=entries_.find(key);
        if(existing!=entries_.end() && existing->second.name==next.name &&
           existing->second.color==next.color && existing->second.icon==next.icon)
            return {}; // idempotent; no flash, no unnecessary disk IO
        if(entries_.size()>=512&&existing==entries_.end())
            return "CHANNEL_APPEARANCE_LIMIT";
        if(revision_==std::numeric_limits<std::uint64_t>::max())
            return "CHANNEL_APPEARANCE_REVISION_EXHAUSTED";
        auto candidate=entries_;candidate[key]=next;
        const auto writeError=commit(candidate,revision_+1);
        if(writeError.isNotEmpty())return writeError;
        entries_.swap(candidate);++revision_;
        return {};
    }

    juce::var snapshot(const juce::var& graph) const {
        auto root=std::make_unique<juce::DynamicObject>();
        root->setProperty("schema","arsonmix.native.channel-appearance.v1");
        root->setProperty("revision",juce::String(std::to_string(revision_)));
        root->setProperty("readOnly",loadError_.isNotEmpty());
        juce::Array<juce::var> rows;
        const auto channels=graph.getProperty("channels",{});
        if(const auto* live=channels.getArray()){
            for(const auto& ch:*live){
                const auto id=ch.getProperty("id",{}).toString();
                const auto found=entries_.find(id.toStdString());
                if(found==entries_.end())continue;
                auto row=std::make_unique<juce::DynamicObject>();
                row->setProperty("id",id);
                row->setProperty("name",found->second.name);
                row->setProperty("color",found->second.color);
                row->setProperty("icon",found->second.icon);
                rows.add(juce::var(row.release()));
            }
        }
        root->setProperty("entries",juce::var(rows));
        return juce::var(root.release());
    }

private:
    juce::File file_;
    std::map<std::string,Entry> entries_;
    std::uint64_t revision_=0;
    juce::String loadError_;

    static juce::var encode(const std::map<std::string,Entry>& entries,
                            std::uint64_t revision) {
        auto root=std::make_unique<juce::DynamicObject>();
        root->setProperty("schema","arsonmix.native.channel-appearance.v1");
        root->setProperty("revision",juce::String(std::to_string(revision)));
        juce::Array<juce::var> rows;
        for(const auto& [key,entry]:entries) {
            auto item=std::make_unique<juce::DynamicObject>();
            item->setProperty("id",juce::String(key));
            item->setProperty("name",entry.name);
            item->setProperty("color",entry.color);
            item->setProperty("icon",entry.icon);
            rows.add(juce::var(item.release()));
        }
        root->setProperty("entries",juce::var(rows));
        return juce::var(root.release());
    }
    juce::String commit(const std::map<std::string,Entry>& candidate,
                        std::uint64_t revision) const {
        const auto directory=file_.getParentDirectory();
        if(!directory.isDirectory()&&directory.createDirectory().failed())
            return "CANNOT_CREATE_CHANNEL_APPEARANCE_DIRECTORY";
        const auto json=juce::JSON::toString(encode(candidate,revision),false);
        if(json.getNumBytesAsUTF8()>128*1024)
            return "CHANNEL_APPEARANCE_DOCUMENT_TOO_LARGE";
        juce::TemporaryFile temp(file_);
        auto output=temp.getFile().createOutputStream();
        if(!output||!output->writeText(json,false,false,"\n"))
            return "CANNOT_STAGE_CHANNEL_APPEARANCE";
        output->flush();
        if(output->getStatus().failed())return "CANNOT_FLUSH_CHANNEL_APPEARANCE";
        output.reset();
        if(!temp.overwriteTargetFileWithTemporary())
            return "CANNOT_ATOMICALLY_COMMIT_CHANNEL_APPEARANCE";
        return {};
    }
    void load() {
        if(!file_.exists())return;
        if(!file_.existsAsFile()||file_.getSize()<1||file_.getSize()>128*1024){
            loadError_="CHANNEL_APPEARANCE_FILE_INVALID";return;
        }
        const auto root=juce::JSON::parse(file_.loadFileAsString());
        if(!root.isObject()||root.getProperty("schema","").toString()!="arsonmix.native.channel-appearance.v1"){
            loadError_="CHANNEL_APPEARANCE_SCHEMA_INVALID";return;
        }
        const auto data=root.getProperty("entries",{});
        std::uint64_t revision=0;
        if(!decimal(root.getProperty("revision",{}),revision,true)||!data.isArray()||
           data.getArray()->size()>512){
            loadError_="CHANNEL_APPEARANCE_CONTENT_INVALID";return;
        }
        std::map<std::string,Entry> candidate;
        for(const auto& item:*data.getArray()) {
            const auto id=item.getProperty("id",{});
            const auto name=item.getProperty("name",{});
            const auto color=item.getProperty("color",{});
            const auto icon=item.getProperty("icon",{});
            std::uint64_t n=0;
            if(!decimal(id,n)||!name.isString()||!color.isString()||!icon.isString()||
               !validName(name.toString())||!validColor(color.toString())||
               !validIcon(icon.toString())||
               !candidate.emplace(id.toString().toStdString(),
                   Entry{name.toString(),color.toString(),icon.toString()}).second) {
                loadError_="CHANNEL_APPEARANCE_ENTRY_INVALID";return;
            }
        }
        entries_.swap(candidate);revision_=revision;
    }
};
} // namespace armix::appearance
