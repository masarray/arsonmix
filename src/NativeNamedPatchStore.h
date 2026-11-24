// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <memory>

// Native CONTROL-thread named routing presets. These are explicit reusable
// physical selections, NOT session SourceBinding IDs or output authorization.
// No device opening, callback work, automatic rebind or output arming.
namespace armix::io {
class NamedPatchStore final {
public:
    explicit NamedPatchStore(juce::File directory)
        : file_(directory.getChildFile("named-io-patches.v1.json")) {load();}
    juce::var snapshot(const juce::var& active) const {
        auto root=object();
        put(root,"revision",revision_);
        put(root,"fileExchange",true);
        put(root,"readOnly",error_.isNotEmpty());
        put(root,"error",error_);
        juce::Array<juce::var> rows;
        for(const auto& entry:entries_) {
            if(!sameDevice(entry,active))continue;
            auto copy=clone(entry);
            put(copy,"available",verified(entry,active));
            rows.add(copy);
        }
        put(root,"entries",juce::var(rows));
        return root;
    }
    // File dialog requests carry the same device/revision lease as edits.
    // Export is a projection of native accepted metadata, never browser JSON.
    bool fileLease(const juce::var& request,const juce::var& active) const {
        const auto rev=request.getProperty("revision",{});
        const auto gen=request.getProperty("deviceGeneration",{});
        const auto actual=active.getProperty("deviceGeneration",{});
        const auto side=request.getProperty("side",{});
        return error_.isEmpty()&&rev.isInt()&&static_cast<int>(rev)==revision_&&
            (gen.isInt()||gen.isInt64())&&(actual.isInt()||actual.isInt64())&&
            static_cast<juce::int64>(gen)==static_cast<juce::int64>(actual)&&
            active.getProperty("open",{})==juce::var(true)&&
            active.getProperty("authorized",{})==juce::var(true)&&
            sameDevice(request,active)&&(side==juce::var("input")||side==juce::var("output"));
    }
    juce::var exportProfile(const juce::var& request,const juce::var& active) const {
        if(!fileLease(request,active))return {};
        auto root=object();
        put(root,"schema","arsonmix.io-profile.v1");
        for(const char* key:{"backend","captureDevice","playbackDevice","side"})
            put(root,key,request.getProperty(key,{}));
        juce::Array<juce::var> rows;
        for(const auto& entry:entries_){
            if(!sameDevice(entry,active)||entry.getProperty("side",{})!=request.getProperty("side",{}))continue;
            auto row=object();
            for(const char* key:{"name","mode","left","right","leftName","rightName"})
                put(row,key,entry.getProperty(key,{}));
            rows.add(row);
        }
        if(rows.isEmpty())return {};
        put(root,"entries",juce::var(rows));
        return root;
    }
    static juce::String writeProfileFile(const juce::File& file,const juce::var& profile) {
        if(!file.hasFileExtension(".ario")||!profile.isObject()||
           profile.getProperty("schema",{})!=juce::var("arsonmix.io-profile.v1"))
            return "Invalid .ario profile";
        const auto json=juce::JSON::toString(profile,false);
        if(json.getNumBytesAsUTF8()>256*1024)return "I/O profile file too large";
        juce::TemporaryFile temp(file);
        auto stream=temp.getFile().createOutputStream();
        if(!stream||!stream->writeText(json,false,false,"\n"))return "Cannot write I/O profile";
        stream->flush();
        if(stream->getStatus().failed())return "Cannot flush I/O profile";
        stream.reset();
        if(!temp.overwriteTargetFileWithTemporary())return "Cannot commit I/O profile file";
        return {};
    }
    // Read only a bounded document; actual import always goes through apply.
    static juce::String readProfileFile(const juce::File& file,juce::var& result) {
        result={};
        if(!file.hasFileExtension(".ario")||!file.existsAsFile()||
           file.getSize()<1||file.getSize()>256*1024)return "Invalid or oversized .ario profile";
        juce::FileInputStream stream(file);
        if(!stream.openedOk())return "Cannot open I/O profile";
        juce::MemoryBlock bytes;
        stream.readIntoMemoryBlock(bytes,256*1024+1);
        if(bytes.getSize()==0||bytes.getSize()>256*1024||stream.getStatus().failed())
            return "Cannot read bounded I/O profile";
        const auto profile=juce::JSON::parse(juce::String::fromUTF8(
            static_cast<const char*>(bytes.getData()),static_cast<int>(bytes.getSize())));
        const auto rows=profile.getProperty("entries",{});
        if(!profile.isObject()||profile.getProperty("schema",{})!=juce::var("arsonmix.io-profile.v1")||
           !rows.isArray()||rows.getArray()->isEmpty()||rows.getArray()->size()>128)
            return "Invalid I/O profile document";
        result=profile;
        return {};
    }
    juce::String apply(const juce::var& request,const juce::var& active) {
        if(error_.isNotEmpty())return error_;
        const auto rev=request.getProperty("revision",{});
        const auto gen=request.getProperty("deviceGeneration",{});
        const auto actual=active.getProperty("deviceGeneration",{});
        if(!rev.isInt()||static_cast<int>(rev)!=revision_||
           !(gen.isInt()||gen.isInt64())||!(actual.isInt()||actual.isInt64())||
           static_cast<juce::int64>(gen)!=static_cast<juce::int64>(actual)||
           active.getProperty("open",{})!=juce::var(true)||
           active.getProperty("authorized",{})!=juce::var(true)||
           !sameDevice(request,active))return "I/O preset: stale device or revision";
        if(revision_>=2147483646)return "I/O preset revision exhausted";
        const auto op=request.getProperty("operation","").toString();
        const auto id=request.getProperty("id","").toString();
        if(!request.getProperty("id",{}).isString()||
           !request.getProperty("operation",{}).isString())
            return "Invalid I/O preset operation or ID";
        auto candidate=entries_;
        int found=-1;
        for(int i=0;i<candidate.size();++i)
            if(candidate[i].getProperty("id","").toString()==id){found=i;break;}
        if(!id.isEmpty()&&(found<0||!sameDevice(candidate[found],active)))
            return "I/O preset not found on current device";
        if(op=="IMPORT") {
            // An interchange profile is desired metadata only. Import is
            // additive, all-or-nothing, and must match the current device.
            const auto profile=request.getProperty("profile",{});
            const auto rows=profile.getProperty("entries",{});
            const auto side=request.getProperty("side",{});
            if(!id.isEmpty()||!profile.isObject()||
               profile.getProperty("schema",{})!=juce::var("arsonmix.io-profile.v1")||
               (side!=juce::var("input")&&side!=juce::var("output"))||
               profile.getProperty("side",{})!=side||!sameDevice(profile,active)||
               !rows.isArray()||rows.getArray()->isEmpty()||
               rows.getArray()->size()>128-candidate.size()||
               juce::JSON::toString(profile,false).getNumBytesAsUTF8()>256*1024)
                return "Invalid or incompatible I/O profile";
            for(const auto& row:*rows.getArray()){
                if(!row.isObject())return "Invalid I/O profile entry";
                auto entry=object();
                put(entry,"id",juce::Uuid().toDashedString());
                for(const char* key:{"backend","captureDevice","playbackDevice"})
                    put(entry,key,profile.getProperty(key,{}));
                put(entry,"side",side);
                for(const char* key:{"name","mode","left","right","leftName","rightName"})
                    put(entry,key,row.getProperty(key,{}));
                if(!valid(entry)||!verified(entry,active))
                    return "I/O profile: unavailable or reserved physical pins";
                for(const auto& old:candidate)
                    if(sameDevice(old,entry)&&old.getProperty("side",{})==side&&
                       old.getProperty("name","").toString().equalsIgnoreCase(
                           entry.getProperty("name","").toString()))
                        return "I/O profile: duplicate preset name";
                candidate.add(entry);
            }
        }else if(op=="REMOVE") {
            if(found<0||candidate[found].getProperty("side",{})!=request.getProperty("side",{}))
                return "I/O preset not found on requested side";
            candidate.remove(found);
        }else if(op=="SAVE"){
            auto entry=object();
            put(entry,"id",found<0?juce::Uuid().toDashedString():id);
            for(const char* key:{"backend","captureDevice","playbackDevice","side",
                "name","mode","left","right","leftName","rightName"})
                put(entry,key,request.getProperty(key,{}));
            if(!valid(entry)||!verified(entry,active)||
               (found>=0&&candidate[found].getProperty("side",{})!=entry.getProperty("side",{})))
                return "I/O preset: select exact enabled hardware pins and side";
            for(int i=0;i<candidate.size();++i)
                if(i!=found&&sameDevice(candidate[i],entry)&&
                   candidate[i].getProperty("side","")==entry.getProperty("side","")&&
                   candidate[i].getProperty("name","").toString().equalsIgnoreCase(
                       entry.getProperty("name","").toString()))
                    return "I/O preset name already used";
            if(found>=0)candidate.set(found,entry);
            else {
                if(candidate.size()>=128)return "I/O preset limit reached";
                candidate.add(entry);
            }
        }else return "Invalid I/O preset operation";
        auto root=object();
        put(root,"schema","arsonmix.named-io-patches.v1");
        put(root,"revision",revision_+1);
        put(root,"entries",juce::var(candidate));
        const auto json=juce::JSON::toString(root,false);
        if(json.getNumBytesAsUTF8()>256*1024)return "I/O preset file too large";
        const auto dir=file_.getParentDirectory();
        if(!dir.isDirectory()&&dir.createDirectory().failed())
            return "Cannot create I/O preset directory";
        juce::TemporaryFile temp(file_);
        auto output=temp.getFile().createOutputStream();
        if(!output||!output->writeText(json,false,false,"\n"))return "Cannot write I/O presets";
        output->flush();
        if(output->getStatus().failed())return "Cannot flush I/O presets";
        output.reset();
        if(!temp.overwriteTargetFileWithTemporary())return "Cannot commit I/O presets";
        entries_.swapWith(candidate);++revision_;
        return {};
    }
private:
    juce::File file_;
    juce::Array<juce::var> entries_;
    int revision_=0;
    juce::String error_;
    static juce::var object(){return juce::var(new juce::DynamicObject());}
    static void put(juce::var& v,const char* k,const juce::var& value){
        v.getDynamicObject()->setProperty(k,value);
    }
    static juce::var clone(const juce::var& v){
        return juce::JSON::parse(juce::JSON::toString(v,false));
    }
    static bool sameDevice(const juce::var& a,const juce::var& b){
        for(const char* k:{"backend","captureDevice","playbackDevice"}){
            const auto x=a.getProperty(k,{}),y=b.getProperty(k,{});
            if(!x.isString()||!y.isString()||x.toString()!=y.toString())return false;
        }
        return a.getProperty("backend","").toString().isNotEmpty()&&
               a.getProperty("playbackDevice","").toString().isNotEmpty();
    }
    static bool text(const juce::var& e,const char* key,int max,bool empty=false){
        const auto v=e.getProperty(key,{});
        if(!v.isString())return false;
        const auto s=v.toString();
        if((!empty&&s.isEmpty())||s.length()>max)return false;
        for(const auto c:s.toStdString())if(static_cast<unsigned char>(c)<32||c==127)return false;
        return true;
    }
    static bool valid(const juce::var& e){
        if(!e.isObject()||!text(e,"id",64)||!text(e,"name",48)||
           !text(e,"backend",128)||!text(e,"captureDevice",256,true)||
           !text(e,"playbackDevice",256)||!text(e,"leftName",256)||
           !text(e,"rightName",256,true))return false;
        const auto name=e.getProperty("name","").toString();
        if(name!=name.trim())return false;
        const auto side=e.getProperty("side","").toString(),mode=e.getProperty("mode","").toString();
        const auto l=e.getProperty("left",{}),r=e.getProperty("right",{});
        if(!l.isInt()||!r.isInt())return false;
        const int left=static_cast<int>(l),right=static_cast<int>(r);
        if(left<0||left>=64)return false;
        if(side=="input"&&mode=="MONO")return right==-1&&e.getProperty("rightName","").toString().isEmpty();
        if(mode!="STEREO"||right<0||right>=64||right==left||
           e.getProperty("rightName","").toString().isEmpty())return false;
        return side=="input"||(side=="output"&&e.getProperty("backend","").toString()=="ASIO"&&
            left%2==0&&right==left+1);
    }
    static bool verified(const juce::var& e,const juce::var& a){
        if(!valid(e)||!sameDevice(e,a)||a.getProperty("open",{})!=juce::var(true)||
           a.getProperty("authorized",{})!=juce::var(true))return false;
        const bool input=e.getProperty("side","").toString()=="input";
        const auto names=a.getProperty(input?"inputNames":"outputNames",{});
        const auto enabled=a.getProperty(input?"enabledInputs":"enabledOutputs",{});
        if(!names.isArray()||!enabled.isArray())return false;
        const auto matches=[&](int pin,const char* key){
            return pin>=0&&pin<names.getArray()->size()&&
                names[pin].isString()&&names[pin].toString()==e.getProperty(key,"").toString()&&
                enabled.getArray()->contains(juce::var(pin));
        };
        const int l=static_cast<int>(e.getProperty("left",{})),r=static_cast<int>(e.getProperty("right",{}));
        if(!matches(l,"leftName")||(r!=-1&&!matches(r,"rightName")))return false;
        if(input)return true;
        // External presets never authorize a MAIN or MONITOR destination.
        // Both reservations must be known from the installed native graph,
        // not inferred from enabled pins or a browser-provided suggestion.
        const auto main=a.getProperty("mainOutputs",{});
        const auto monitor=a.getProperty("monitorOutputs",{});
        const auto validPair=[&](const juce::var& value,bool optional){
            const auto* p=value.getArray();
            if(!p)return false;
            if(optional&&p->isEmpty())return true;
            return p->size()==2&&(*p)[0].isInt()&&(*p)[1].isInt()&&
                (*p)[0]!=(*p)[1]&&enabled.getArray()->contains((*p)[0])&&
                enabled.getArray()->contains((*p)[1]);
        };
        if(!validPair(main,false)||!validPair(monitor,true))return false;
        const auto contains=[&](const juce::var& pair,int pin){
            return pair.getArray()->contains(juce::var(pin));
        };
        for(const auto& pin:*monitor.getArray())
            if(contains(main,static_cast<int>(pin)))return false;
        return !contains(main,l)&&!contains(main,r)&&
               !contains(monitor,l)&&!contains(monitor,r);
    }
    void load(){
        if(!file_.exists())return;
        if(!file_.existsAsFile()||file_.getSize()<1||file_.getSize()>256*1024){
            error_="Invalid I/O preset file; preserved";return;
        }
        const auto root=juce::JSON::parse(file_.loadFileAsString());
        const auto rev=root.getProperty("revision",{}),rows=root.getProperty("entries",{});
        if(root.getProperty("schema","").toString()!="arsonmix.named-io-patches.v1"||
           !rev.isInt()||static_cast<int>(rev)<0||!rows.isArray()||rows.getArray()->size()>128){
            error_="Invalid I/O preset document; preserved";return;
        }
        juce::Array<juce::var> candidate;
        for(const auto& entry:*rows.getArray()){
            if(!valid(entry)){error_="Invalid I/O preset entry; preserved";return;}
            for(const auto& previous:candidate)
                if(previous.getProperty("id","")==entry.getProperty("id","")||
                   (sameDevice(previous,entry)&&previous.getProperty("side","")==entry.getProperty("side","")&&
                    previous.getProperty("name","").toString().equalsIgnoreCase(entry.getProperty("name","").toString()))){
                    error_="Duplicate I/O preset entry; preserved";return;
                }
            candidate.add(entry);
        }
        entries_.swapWith(candidate);revision_=static_cast<int>(rev);
    }
};
}
