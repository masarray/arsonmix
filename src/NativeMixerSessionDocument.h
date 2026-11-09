// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <optional>
#include "../engine/core/UnifiedLegacyProductionAdmission.h"
#include "NativeShowStore.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

// The logical session and its hardware binding are deliberately separate.
// This is a DOCUMENT, not another graph, DSP engine or audio callback.
// All validation/conversion occurs on the JUCE CONTROL / I/O threads.
namespace armix::session {
using namespace armix::mixer;

struct HardwareBinding final {
    juce::String backend,capture,playback,inputMask,outputMask;
    double rate=0.;
    int frames=0;
    // Live pin names guard device switches in this process. Older documents
    // still decode without inventing an ASIO pin inventory.
    juce::StringArray inputNames,outputNames;
};
struct StoredInsert final {
    ChannelId channel{};
    std::size_t slot=0;
    juce::String catalogId,path,sha256,name;
    int uid=0;
    bool bypass=false;
};
struct MixerSessionDocument final {
    juce::String id;
    DynamicChannelRoster roster{ChannelRosterBudget{
        32u*1024u*1024u,8u,static_cast<std::size_t>(maxChannelSends+maxAuxSends+2)}};
    DynamicGraphRequest request{};
    LegacyChannelIds legacyIds{};
    VerifiedNativeOutputs outputs{};
    HardwareBinding binding{};
    std::vector<StoredInsert> inserts;
    std::vector<ChannelId> rackBypassed;
    float masterGain=1.f;
    bool masterMute=false,masterMono=false;
    // Internal checkpoint preference, distinct from the hardware monitor pair.
    std::optional<bool> resumeMain;
    std::uint64_t acceptedRevision=0;
    juce::int64 savedMs=0;
};
// Logical topology never authorizes a different physical endpoint. An
// operator-confirmed rebind changes only verified hardware metadata; it
// cannot change the ChannelId graph, vendor state, or ARM an output.
struct MixerDeviceBindingPolicy final {
    static bool sameBinding(const HardwareBinding& a,const HardwareBinding& b) {
        return a.backend==b.backend && a.capture==b.capture &&
               a.playback==b.playback && a.inputMask==b.inputMask &&
               a.outputMask==b.outputMask && a.rate==b.rate;
    }
    static bool sameOutputs(const VerifiedNativeOutputs& a,
                            const VerifiedNativeOutputs& b) {
        return a.activeOutputChannels==b.activeOutputChannels &&
               a.mainL==b.mainL && a.mainR==b.mainR &&
               a.monitorEnabled==b.monitorEnabled &&
               (!a.monitorEnabled ||
                (a.monitorL==b.monitorL && a.monitorR==b.monitorR));
    }
    enum class Mode { exact, verifiedDeviceApply };
    static VerifiedNativeOutputs proposedOutputs(const MixerSessionDocument& document,
            const HardwareBinding& live,int count,Mode mode=Mode::exact) {
        const auto& saved=document.binding;
        if(mode==Mode::verifiedDeviceApply &&
           (saved.backend!=live.backend || saved.playback!=live.playback))
            return count==2?VerifiedNativeOutputs{count,0,1,-1,-1,false}:
                            VerifiedNativeOutputs{count,-1,-1,-1,-1,false};
        // A matching endpoint name does NOT prove an unchanged physical
        // jack inventory. Reject rather than silently rebase MAIN to 1/2
        // if pin labels changed or an unknown ASIO inventory lost pins.
        if(mode==Mode::verifiedDeviceApply &&
           ((!saved.outputNames.isEmpty() && saved.outputNames!=live.outputNames) ||
            (saved.outputNames.isEmpty() && live.backend=="ASIO" &&
             saved.outputMask!=live.outputMask)))
            return {count,-1,-1,-1,-1,false};
        auto outputs=document.outputs;
        outputs.activeOutputChannels=count;
        if(saved.outputMask!=live.outputMask) {
            const auto oldPins=mask(saved.outputMask);
            const auto newPins=mask(live.outputMask);
            outputs.mainL=remapPin(oldPins,newPins,outputs.mainL);
            outputs.mainR=remapPin(oldPins,newPins,outputs.mainR);
            if(outputs.monitorEnabled) {
                outputs.monitorL=remapPin(oldPins,newPins,outputs.monitorL);
                outputs.monitorR=remapPin(oldPins,newPins,outputs.monitorR);
            }
        }
        if(!outputs.valid()) {
            outputs.monitorL=outputs.monitorR=-1;
            outputs.monitorEnabled=false;
        }
        // Never substitute the first available pair when the saved MAIN
        // physical connectors disappear. Audio may only return after the
        // operator explicitly patches MAIN to available output pins.
        return outputs;
    }
    static juce::String apply(MixerSessionDocument& document,
            const HardwareBinding& live,
            const VerifiedNativeOutputs& outputs,
            int callbackInputs,Mode mode) {
        if(!outputs.valid() || callbackInputs<0 || callbackInputs>256 ||
           live.backend.isEmpty() ||
           (callbackInputs>0 && live.capture.isEmpty()) ||
           live.playback.isEmpty() || live.inputMask.isEmpty() ||
           live.outputMask.isEmpty() || !std::isfinite(live.rate) ||
           live.rate<8000. || live.rate>384000. ||
           live.frames<16 || live.frames>8192)
            return "NEEDS_REMAP: live device verification incomplete";
        const auto oldPins=mask(document.binding.inputMask);
        const auto newPins=mask(live.inputMask);
        if(oldPins.countNumberOfSetBits()!=document.request.physicalInputs ||
           newPins.countNumberOfSetBits()!=callbackInputs ||
           mask(live.outputMask).countNumberOfSetBits()!=outputs.activeOutputChannels)
            return "NEEDS_REMAP: callback pin inventory does not match device masks";
        const auto& saved=document.binding;
        const bool knownInputChanged=!saved.inputNames.isEmpty()&&saved.inputNames!=live.inputNames;
        const bool knownOutputChanged=!saved.outputNames.isEmpty()&&saved.outputNames!=live.outputNames;
        const bool exact=sameBinding(saved,live)&&sameOutputs(document.outputs,outputs)&&
                         !knownInputChanged&&!knownOutputChanged;
        if(mode==Mode::exact)
            return exact?juce::String{}:juce::String(
                "NEEDS_REMAP: select the saved audio device and pin layout in Audio I/O");

        // An Apply of the SAME playback endpoint must keep the saved
        // physical MAIN connectors. The current mask only changes their
        // ordinals; it never authorizes quietly switching MAIN to another
        // jack. Compare against the policy-computed remap even when a caller
        // supplies a valid but DIFFERENT output pair.
        if(saved.backend==live.backend && saved.playback==live.playback) {
            const auto mapped=proposedOutputs(document,live,
                outputs.activeOutputChannels,Mode::verifiedDeviceApply);
            if(!mapped.valid() || !sameOutputs(mapped,outputs))
                return "NEEDS_REMAP: saved MAIN output pins are no longer selected; explicitly choose MAIN outputs";
        }

        auto& sources=document.request.sources;
        if(saved.backend!=live.backend || saved.capture!=live.capture ||
           !samePinNames(saved.inputNames,live.inputNames,live.backend)) {
            // Matching pin numbers alone cannot identify another capture source.
            sources.clear();
        } else if(oldPins!=newPins) {
            std::vector<DynamicSourcePatch> mapped;
            mapped.reserve(sources.size());
            for(auto source:sources) {
                source.inputL=remapPin(oldPins,newPins,source.inputL);
                if(source.stereo)source.inputR=remapPin(oldPins,newPins,source.inputR);
                if(source.inputL>=0 && (!source.stereo || source.inputR>=0))
                    mapped.push_back(source);
            }
            sources=std::move(mapped);
        }
        document.request.physicalInputs=callbackInputs;
        document.binding=live;
        document.outputs=outputs;
        return {};
    }
private:
    static bool samePinNames(const juce::StringArray& saved,const juce::StringArray& live,
                             const juce::String& backend) {
        return saved.isEmpty()?backend!="ASIO":saved==live;
    }
    static juce::BigInteger mask(const juce::String& text) {
        juce::BigInteger bits;
        bits.parseString(text,16);
        return bits;
    }
    static int remapPin(const juce::BigInteger& saved,const juce::BigInteger& current,int ordinal) {
        if(ordinal<0)return -1;
        int physical=saved.findNextSetBit(0);
        for(int i=0;i<ordinal && physical>=0;++i)
            physical=saved.findNextSetBit(physical+1);
        if(physical<0 || !current[physical])return -1;
        int index=0;
        for(int pin=current.findNextSetBit(0);pin<physical;pin=current.findNextSetBit(pin+1))
            ++index;
        return index;
    }
};
inline juce::String decimal(std::uint64_t value) {
    return juce::String(std::to_string(value));
}
inline bool parseDecimal(const juce::var& v,std::uint64_t& result,bool allowZero=false) {
    if(!v.isString())return false;
    const auto s=v.toString().toStdString();
    if(s.empty()||s.size()>20||(s[0]=='0'&&s.size()!=1))return false;
    std::uint64_t n=0;
    auto [end,err]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(err!=std::errc{}||end!=s.data()+s.size()||(!allowZero&&!n))return false;
    result=n;return true;
}
inline bool real(const juce::var& v,double& n,double lo,double hi) {
    if(!(v.isDouble()||v.isInt()||v.isInt64()))return false;
    n=static_cast<double>(v);
    return std::isfinite(n)&&n>=lo&&n<=hi;
}
inline bool integral(const juce::var& v,int& n,int lo,int hi) {
    if(!v.isInt())return false;
    n=static_cast<int>(v);
    return n>=lo&&n<=hi;
}
inline juce::var get(const juce::var& v,const char* key) {
    return v.isObject()?v.getProperty(key,juce::var()):juce::var();
}
inline bool strictBool(const juce::var& v,bool& out) {
    if(!v.isBool())return false;
    out=static_cast<bool>(v);return true;
}
inline bool textField(const juce::var& v,juce::String& out,int max,bool empty=false) {
    if(!v.isString()||v.toString().getNumBytesAsUTF8()>max||
       (!empty&&v.toString().isEmpty()))return false;
    out=v.toString();return true;
}
class Codec final {
    static juce::var object(){return juce::var(new juce::DynamicObject());}
    static void set(juce::var& obj,const char* k,const juce::var& v) {
        obj.getDynamicObject()->setProperty(k,v);
    }
    static juce::var ids(const LegacyChannelIds& ids) {
        auto v=object();
        juce::Array<juce::var> in,aux;
        for(auto id:ids.inputs)in.add(decimal(id.value));
        for(auto id:ids.aux)aux.add(decimal(id.value));
        set(v,"inputs",juce::var(in));set(v,"aux",juce::var(aux));
        set(v,"main",decimal(ids.main.value));
        set(v,"monitor",decimal(ids.monitor.value));
        return v;
    }
    static bool readIds(const juce::var& v,LegacyChannelIds& ids) {
        const auto inputVar=get(v,"inputs"),auxVar=get(v,"aux");
        const auto* in=inputVar.getArray(),*aux=auxVar.getArray();
        if(!in||!aux||in->size()!=stripCount||aux->size()!=auxBusCount)
            return false;
        for(int i=0;i<stripCount;++i)
            if(!parseDecimal(in->getReference(i),ids.inputs[i].value))return false;
        for(int i=0;i<auxBusCount;++i)
            if(!parseDecimal(aux->getReference(i),ids.aux[i].value))return false;
        return parseDecimal(get(v,"main"),ids.main.value) &&
               parseDecimal(get(v,"monitor"),ids.monitor.value,true);
    }
public:
    static juce::String encode(const MixerSessionDocument& d) {
        auto root=object();
        set(root,"schema","arsonmix.mixer-session.v1");
        set(root,"id",d.id);
        set(root,"acceptedRevision",decimal(d.acceptedRevision));
        set(root,"savedMs",decimal(static_cast<std::uint64_t>(d.savedMs)));
        if(d.resumeMain.has_value())set(root,"resumeMain",*d.resumeMain);
        set(root,"legacyIds",ids(d.legacyIds));
        auto bind=object();
        set(bind,"backend",d.binding.backend);
        set(bind,"capture",d.binding.capture);
        set(bind,"playback",d.binding.playback);
        set(bind,"inputMask",d.binding.inputMask);
        set(bind,"outputMask",d.binding.outputMask);
        set(bind,"rate",d.binding.rate);
        set(bind,"frames",d.binding.frames);
        set(root,"binding",bind);
        auto output=object();
        set(output,"active",d.outputs.activeOutputChannels);
        set(output,"mainL",d.outputs.mainL);set(output,"mainR",d.outputs.mainR);
        set(output,"monitorL",d.outputs.monitorL);
        set(output,"monitorR",d.outputs.monitorR);
        set(output,"monitorEnabled",d.outputs.monitorEnabled);
        set(root,"outputs",output);
        auto master=object();
        set(master,"gain",d.masterGain);set(master,"mute",d.masterMute);
        set(master,"mono",d.masterMono);set(root,"master",master);
        juce::Array<juce::var> channels;
        for(const auto& row:d.roster.channels()) {
            auto ch=object();
            set(ch,"id",decimal(row.id.value));
            set(ch,"role",static_cast<int>(row.role));
            set(ch,"format",static_cast<int>(row.format));
            set(ch,"name",juce::String::fromUTF8(row.name.c_str()));
            set(ch,"gain",row.faderLinear);
            set(ch,"pan",row.pan);set(ch,"mute",row.mute);set(ch,"solo",row.solo);
            juce::Array<juce::var> catalogs,sends;
            for(const auto& c:row.insertCatalogIds)
                catalogs.add(juce::String::fromUTF8(c.c_str()));
            for(const auto& s:row.sends){
                auto e=object();
                set(e,"edgeId",decimal(s.edgeId));
                set(e,"to",decimal(s.destination.value));
                set(e,"gain",s.gain);set(e,"pan",s.pan);
                set(e,"pre",s.preFader);set(e,"primary",s.primaryOutput);
                sends.add(e);
            }
            set(ch,"catalogs",juce::var(catalogs));
            set(ch,"sends",juce::var(sends));
            channels.add(ch);
        }
        set(root,"channels",juce::var(channels));
        auto request=object();
        set(request,"physicalInputs",d.request.physicalInputs);
        set(request,"maxFrames",d.request.maxFrames);
        set(request,"mainMonoAfterFx",d.request.mainMonoAfterFx);
        juce::Array<juce::var> sources;
        for(const auto& p:d.request.sources) {
            auto source=object();
            set(source,"id",decimal(p.id.value));
            set(source,"left",p.inputL);
            set(source,"right",p.inputR);
            set(source,"stereo",p.stereo);
            sources.add(source);
        }
        set(request,"sources",juce::var(sources));
        set(root,"request",request);
        juce::Array<juce::var> inserts,racks;
        for(const auto& f:d.inserts) {
            auto fx=object();
            set(fx,"channel",decimal(f.channel.value));
            set(fx,"slot",static_cast<int>(f.slot));
            set(fx,"catalogId",f.catalogId);set(fx,"path",f.path);
            set(fx,"uid",f.uid);set(fx,"name",f.name);
            set(fx,"sha256",f.sha256);set(fx,"bypass",f.bypass);
            inserts.add(fx);
        }
        for(auto id:d.rackBypassed)racks.add(decimal(id.value));
        set(root,"inserts",juce::var(inserts));set(root,"rackBypassed",juce::var(racks));
        return juce::JSON::toString(root,false);
    }
    static bool decode(const juce::String& json,MixerSessionDocument& output) {
        if(json.getNumBytesAsUTF8()>4U*1024U*1024U)return false;
        auto root=juce::JSON::parse(json);
        if(!root.isObject()||get(root,"schema").toString()!="arsonmix.mixer-session.v1")
            return false;
        MixerSessionDocument doc;
        if(root.getDynamicObject()->hasProperty("resumeMain")) {
            bool resume=false;
            if(!strictBool(get(root,"resumeMain"),resume))return false;
            doc.resumeMain=resume;
        }
        if(!textField(get(root,"id"),doc.id,48)||
           !armix::shows::detail::uuid(doc.id)||
           !parseDecimal(get(root,"acceptedRevision"),doc.acceptedRevision)||
           !readIds(get(root,"legacyIds"),doc.legacyIds))
            return false;
        std::uint64_t ms=0;
        if(!parseDecimal(get(root,"savedMs"),ms)||ms>static_cast<std::uint64_t>(
            std::numeric_limits<juce::int64>::max()))return false;
        doc.savedMs=static_cast<juce::int64>(ms);
        const auto binding=get(root,"binding");
        if(!textField(get(binding,"backend"),doc.binding.backend,128)||
           !textField(get(binding,"capture"),doc.binding.capture,256)||
           !textField(get(binding,"playback"),doc.binding.playback,256)||
           !textField(get(binding,"inputMask"),doc.binding.inputMask,256)||
           !textField(get(binding,"outputMask"),doc.binding.outputMask,256))
            return false;
        double n=0.;
        if(!real(get(binding,"rate"),n,8000.,384000.))return false;
        doc.binding.rate=n;
        if(!integral(get(binding,"frames"),doc.binding.frames,16,8192))
            return false;
        auto outputs=get(root,"outputs");
        if(!integral(get(outputs,"active"),doc.outputs.activeOutputChannels,2,256)||
           !integral(get(outputs,"mainL"),doc.outputs.mainL,0,255)||
           !integral(get(outputs,"mainR"),doc.outputs.mainR,0,255)||
           !integral(get(outputs,"monitorL"),doc.outputs.monitorL,-1,255)||
           !integral(get(outputs,"monitorR"),doc.outputs.monitorR,-1,255)||
           !strictBool(get(outputs,"monitorEnabled"),doc.outputs.monitorEnabled)||
           !doc.outputs.valid())return false;
        auto master=get(root,"master");
        if(!real(get(master,"gain"),n,0.,4.))return false;
        doc.masterGain=static_cast<float>(n);
        if(!strictBool(get(master,"mute"),doc.masterMute)||
           !strictBool(get(master,"mono"),doc.masterMono))return false;
        const auto channelsVar=get(root,"channels");
        const auto* channels=channelsVar.getArray();
        if(!channels||channels->size()<15||channels->size()>2048)return false;
        std::vector<ChannelStripDefinition> entries;
        entries.reserve(static_cast<std::size_t>(channels->size()));
        for(const auto& ch:*channels) {
            ChannelStripDefinition row;
            int role=0,format=0;
            if(!parseDecimal(get(ch,"id"),row.id.value)||
               !integral(get(ch,"role"),role,0,4)||
               !integral(get(ch,"format"),format,0,1))return false;
            row.role=static_cast<ChannelRole>(role);
            row.format=static_cast<ProcessingFormat>(format);
            juce::String name;
            if(!textField(get(ch,"name"),name,96)||
               !real(get(ch,"gain"),n,0.,4.))return false;
            row.name=name.toStdString();row.faderLinear=static_cast<float>(n);
            if(!real(get(ch,"pan"),n,-1.,1.))return false;
            row.pan=static_cast<float>(n);
            if(!strictBool(get(ch,"mute"),row.mute)||
               !strictBool(get(ch,"solo"),row.solo))return false;
            const auto catalogsVar=get(ch,"catalogs"),sendsVar=get(ch,"sends");
            const auto* catalogs=catalogsVar.getArray();
            const auto* sends=sendsVar.getArray();
            if(!catalogs||!sends||catalogs->size()>8||sends->size()>128)
                return false;
            for(const auto& catalog:*catalogs) {
                juce::String value;
                if(!textField(catalog,value,256))return false;
                row.insertCatalogIds.push_back(value.toStdString());
            }
            for(const auto& send:*sends) {
                ChannelSendRef edge;
                if(!parseDecimal(get(send,"edgeId"),edge.edgeId)||
                   !parseDecimal(get(send,"to"),edge.destination.value)||
                   !real(get(send,"gain"),n,0.,4.))return false;
                edge.gain=static_cast<float>(n);
                if(!real(get(send,"pan"),n,-1.,1.))return false;
                edge.pan=static_cast<float>(n);
                if(!strictBool(get(send,"pre"),edge.preFader)||
                   !strictBool(get(send,"primary"),edge.primaryOutput))return false;
                row.sends.push_back(edge);
            }
            entries.push_back(std::move(row));
        }
        if(!doc.roster.restoreExact(doc.acceptedRevision,entries))return false;
        const auto seek=[&doc](ChannelId id,ChannelRole role){
            const auto* row=doc.roster.find(id);
            return row && row->role==role;
        };
        for(auto id:doc.legacyIds.inputs)if(!seek(id,ChannelRole::input))return false;
        for(auto id:doc.legacyIds.aux)if(!seek(id,ChannelRole::fxReturn))return false;
        if(!seek(doc.legacyIds.main,ChannelRole::main) ||
           (doc.legacyIds.monitor.value&&!seek(doc.legacyIds.monitor,ChannelRole::monitor)))
            return false;
        auto req=get(root,"request");
        if(!integral(get(req,"physicalInputs"),doc.request.physicalInputs,1,256)||
           !integral(get(req,"maxFrames"),doc.request.maxFrames,16,8192)||
           !strictBool(get(req,"mainMonoAfterFx"),doc.request.mainMonoAfterFx))
            return false;
        const auto sourcesVar=get(req,"sources");
        const auto* sources=sourcesVar.getArray();
        if(!sources||sources->size()>channels->size())return false;
        std::set<std::uint64_t> seenSources;
        for(const auto& src:*sources) {
            DynamicSourcePatch p;
            if(!parseDecimal(get(src,"id"),p.id.value)||
               !seenSources.insert(p.id.value).second ||
               !seek(p.id,ChannelRole::input) ||
               !integral(get(src,"left"),p.inputL,0,doc.request.physicalInputs-1)||
               !integral(get(src,"right"),p.inputR,-1,doc.request.physicalInputs-1)||
               !strictBool(get(src,"stereo"),p.stereo)||
               (p.stereo && (p.inputR<0||p.inputR==p.inputL))||
               (!p.stereo&&p.inputR>=0))
                return false;
            doc.request.sources.push_back(p);
        }
        const auto fxVar=get(root,"inserts"),racksVar=get(root,"rackBypassed");
        const auto* fx=fxVar.getArray();
        const auto* racks=racksVar.getArray();
        if(!fx||!racks||fx->size()>16384||racks->size()>channels->size())return false;
        std::set<std::pair<std::uint64_t,std::size_t>> seenFx;
        for(const auto& item:*fx) {
            StoredInsert p;int slot=0;
            if(!parseDecimal(get(item,"channel"),p.channel.value)||
               !doc.roster.find(p.channel)||
               !integral(get(item,"slot"),slot,0,7))return false;
            p.slot=static_cast<std::size_t>(slot);
            if(!seenFx.emplace(p.channel.value,p.slot).second||
               !textField(get(item,"catalogId"),p.catalogId,256)||
               !textField(get(item,"path"),p.path,2048,true)||
               !textField(get(item,"name"),p.name,256,true)||
               !integral(get(item,"uid"),p.uid,std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max())||
               !textField(get(item,"sha256"),p.sha256,64)||
               p.sha256.length()!=64 ||
               !p.sha256.containsOnly("0123456789abcdef")||
               !strictBool(get(item,"bypass"),p.bypass))return false;
            doc.inserts.push_back(std::move(p));
        }
        for(const auto& item:*racks) {
            ChannelId id;
            if(!parseDecimal(item,id.value)||!doc.roster.find(id)||
               std::find(doc.rackBypassed.begin(),doc.rackBypassed.end(),id)!=
               doc.rackBypassed.end())return false;
            doc.rackBypassed.push_back(id);
        }
        // Encode always emits in ChannelId/slot order. A manually shuffled or
        // duplicated slot manifest is rejected; its vendor states must never
        // attach to a different processor or channel.
        for(const auto& ch:doc.roster.channels()) {
            std::size_t slot=0;
            const bool legacyMarker=ch.insertCatalogIds.size()==1 &&
                ch.insertCatalogIds[0]=="legacy:aux-rack";
            for(const auto& fxRow:doc.inserts)
                if(fxRow.channel==ch.id) {
                    if(fxRow.slot!=slot++)return false;
                    // A malformed, otherwise SHA-correct document can claim
                    // more FX slots than the accepted roster's catalog.
                    // Bound-check BEFORE indexing: never allow UB on restore.
                    if(!legacyMarker &&
                       (fxRow.slot>=ch.insertCatalogIds.size() ||
                        ch.insertCatalogIds[fxRow.slot]!=fxRow.catalogId.toStdString()))
                        return false;
                }
            // The old four AUX are declared as legacy:aux-rack placeholders
            // even when no plugin is installed. They are NOT vendor leases.
            // No orphan leases, and no phantom roster FX. In particular an
            // empty catalog cannot authorize an installed vendor processor.
            if(!legacyMarker&&slot!=ch.insertCatalogIds.size())return false;
        }
        output=std::move(doc);
        return true;
    }
};
} // namespace armix::session
