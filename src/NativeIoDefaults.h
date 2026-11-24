// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionDocument.h"
#include <type_traits>
#include "../engine/device/JuceActivePortSnapshot.h"

// Machine-local port definitions, deliberately independent of session IDs,
// references, graph, mixer parameters, plugin state and output permission.
namespace armix::session {
struct IoPortLayout final {
    using I=armix::device::LogicalInputRegistry;
    using O=armix::device::LogicalOutputRegistry;
    I inputs;
    O outputs;
    bool empty() const noexcept{return inputs.empty()&&outputs.empty();}
    bool definitionsOnly() const noexcept{return inputs.references().empty()&&outputs.references().empty();}
    static IoPortLayout capture(const I& in,const O& out){
        IoPortLayout result;
        for(const auto& p:in.ports()){
            if(!result.inputs.add(p.id,p.name,p.format)||
               (p.binding&&!result.inputs.restoreIntent(p.id,*p.binding)))return {};
        }
        for(const auto& p:out.ports()){
            if(!result.outputs.add(p.id,p.name)||
               (p.binding&&!result.outputs.restoreIntent(p.id,*p.binding)))return {};
        }
        return result;
    }
    // Re-attest actual enabled labels and exact endpoint identity. A
    // different or missing pin leaves a named unbound port, never fallback.
    IoPortLayout qualify(const HardwareBinding& binding,
                         const armix::device::JuceActivePortSnapshot& pins) const {
        IoPortLayout out;
        std::vector<I::Pin> ins;
        std::vector<O::Pin> outs;
        for(int i=0;i<pins.inputs.enabledCount;++i){
            const int n=pins.inputs.physicalIndex(i);
            auto name=binding.inputNames[n];
            if(armix::device::uniqueNamedPin(binding.inputNames,name)!=n)name.clear();
            ins.push_back({n,i,name.toStdString()});
        }
        for(int i=0;i<pins.outputCount;++i){
            const int n=pins.outputPhysicalPort(i);
            auto name=pins.outputNames[n];
            if(armix::device::uniqueNamedPin(pins.outputNames,name)!=n)name.clear();
            outs.push_back({n,i,name.toStdString()});
        }
        const I::Inventory input{{binding.backend.toStdString(),binding.capture.toStdString(),
            binding.playback.toStdString()},pins.epoch,ins,true};
        const O::Inventory output{{pins.typeName.toStdString(),pins.deviceName.toStdString()},
            pins.epoch,outs,{},true};
        for(const auto& p:inputs.ports()){
            if(!out.inputs.add(p.id,p.name,p.format))return {};
            if(p.binding)(void)out.inputs.bind(p.id,*p.binding,input,pins.epoch);
        }
        for(const auto& p:outputs.ports()){
            if(!out.outputs.add(p.id,p.name))return {};
            // This merely restores metadata, with no ChannelId references.
            if(p.binding)(void)out.outputs.bind(p.id,*p.binding,output,pins.epoch,true);
        }
        return out;
    }
};

class NativeIoDefaults final {
public:
    explicit NativeIoDefaults(juce::File directory):file_(directory.getChildFile("io-defaults.v1.json")){load();}
    const IoPortLayout& layout() const noexcept{return layout_;}
    std::uint64_t revision() const noexcept{return revision_;}
    bool readOnly() const noexcept{return error_.isNotEmpty();}
    juce::var snapshot(bool canLoad) const{
        auto root=object();
        put(root,"revision",decimal(revision_));put(root,"readOnly",readOnly());
        put(root,"error",error_);put(root,"canLoad",canLoad&&!readOnly()&&!layout_.empty());
        put(root,"inputs",static_cast<int>(layout_.inputs.ports().size()));
        put(root,"outputs",static_cast<int>(layout_.outputs.ports().size()));
        return root;
    }
    juce::String save(std::uint64_t expected,const IoPortLayout& candidate){
        if(readOnly())return error_;
        if(expected!=revision_||revision_==std::numeric_limits<std::uint64_t>::max())
            return "I/O default revision changed";
        if(!candidate.definitionsOnly())return "Default must contain port definitions only";
        const auto next=revision_+1;
        const auto json=encode(candidate,next);
        IoPortLayout verified;std::uint64_t rev=0;
        if(!decode(json,verified,rev)||rev!=next||encode(verified,rev)!=json)
            return "Invalid default I/O layout";
        if(!file_.getParentDirectory().createDirectory().wasOk())return "Cannot create I/O settings folder";
        juce::TemporaryFile temporary(file_);
        auto stream=temporary.getFile().createOutputStream();
        if(!stream||!stream->writeText(json,false,false,"\n"))return "Cannot write I/O default";
        stream->flush();if(stream->getStatus().failed())return "Cannot flush I/O default";
        stream.reset();
        IoPortLayout reread;std::uint64_t rereadRevision=0;
        if(!decode(temporary.getFile().loadFileAsString(),reread,rereadRevision)||
           rereadRevision!=next||encode(reread,next)!=json)return "I/O default readback mismatch";
        if(!temporary.overwriteTargetFileWithTemporary())return "Cannot commit I/O default";
        layout_=std::move(reread);revision_=next;return {};
    }
private:
    static constexpr juce::int64 maxBytes=512*1024;
    static juce::var object(){return juce::var(new juce::DynamicObject());}
    static void put(juce::var& v,const char* key,const juce::var& value){v.getDynamicObject()->setProperty(key,value);}
    template<class Registry> static juce::var encodePorts(const Registry& registry){
        juce::Array<juce::var> rows;
        for(const auto& p:registry.ports()){
            auto row=object();put(row,"id",decimal(p.id));put(row,"name",juce::String(p.name));
            if constexpr(std::is_same_v<Registry,IoPortLayout::I>)
                put(row,"format",p.format==Registry::Format::mono?"MONO":"STEREO");
            if(p.binding){
                const auto& b=*p.binding;auto v=object();
                put(v,"backend",juce::String(b.device.backend));
                if constexpr(std::is_same_v<Registry,IoPortLayout::I>)put(v,"capture",juce::String(b.device.capture));
                put(v,"playback",juce::String(b.device.playback));put(v,"left",b.left);put(v,"right",b.right);
                put(v,"leftName",juce::String(b.leftName));put(v,"rightName",juce::String(b.rightName));put(row,"binding",v);
            }
            rows.add(row);
        }
        return juce::var(rows);
    }
    static juce::String encode(const IoPortLayout& layout,std::uint64_t revision){
        auto root=object();put(root,"schema","arsonmix.io-defaults.v1");
        put(root,"revision",decimal(revision));put(root,"inputs",encodePorts(layout.inputs));
        put(root,"outputs",encodePorts(layout.outputs));return juce::JSON::toString(root,false);
    }
    template<class Registry> static bool decodePorts(const juce::var& list,Registry& registry){
        const auto* rows=list.getArray();if(!rows||rows->size()>static_cast<int>(Registry::maxPorts))return false;
        constexpr bool input=std::is_same_v<Registry,IoPortLayout::I>;
        for(const auto& row:*rows){
            std::uint64_t id=0;juce::String name;
            if(!row.isObject()||!parseDecimal(get(row,"id"),id)||!textField(get(row,"name"),name,192))return false;
            if constexpr(input){
                const auto format=get(row,"format").toString();
                if((format!="MONO"&&format!="STEREO")||
                   !registry.add(id,name.toStdString(),format=="MONO"?Registry::Format::mono:Registry::Format::stereo))return false;
            }else if(!registry.add(id,name.toStdString()))return false;
            if(!row.getDynamicObject()->hasProperty("binding"))continue;
            const auto v=get(row,"binding");typename Registry::Binding b;
            juce::String backend,capture,playback,l,r;
            if(!v.isObject()||!textField(get(v,"backend"),backend,128)||!textField(get(v,"playback"),playback,256)||
               !integral(get(v,"left"),b.left,0,255)||!integral(get(v,"right"),b.right,input?-1:0,255)||
               !textField(get(v,"leftName"),l,256)||!textField(get(v,"rightName"),r,256,input))return false;
            if constexpr(input){
                if(!textField(get(v,"capture"),capture,256))return false;
                b.device={backend.toStdString(),capture.toStdString(),playback.toStdString()};
            }else b.device={backend.toStdString(),playback.toStdString()};
            b.leftName=l.toStdString();b.rightName=r.toStdString();
            if(!registry.restoreIntent(id,b))return false;
        }
        return true;
    }
    static bool decode(const juce::String& json,IoPortLayout& out,std::uint64_t& revision){
        if(json.getNumBytesAsUTF8()>maxBytes)return false;
        const auto root=juce::JSON::parse(json);
        IoPortLayout candidate;std::uint64_t rev=0;
        if(!root.isObject()||get(root,"schema").toString()!="arsonmix.io-defaults.v1"||
           !parseDecimal(get(root,"revision"),rev)||!decodePorts(get(root,"inputs"),candidate.inputs)||
           !decodePorts(get(root,"outputs"),candidate.outputs))return false;
        // Reject extra fields (including smuggled refs/ARM), duplicate JSON
        // keys that do not survive canonical parse, and noncanonical payloads.
        if(encode(candidate,rev)!=json)return false;
        out=std::move(candidate);revision=rev;return true;
    }
    void load(){
        if(!file_.exists())return;
        if(!file_.existsAsFile()||file_.getSize()<1||file_.getSize()>maxBytes||
           !decode(file_.loadFileAsString(),layout_,revision_))error_="I/O defaults damaged; preserve file for recovery";
    }
    juce::File file_;IoPortLayout layout_;std::uint64_t revision_=0;juce::String error_;
};
} // namespace armix::session
