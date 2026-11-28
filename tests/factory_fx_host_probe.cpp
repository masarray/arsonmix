// SPDX-License-Identifier: AGPL-3.0-only
// Isolated Windows qualification: no vendor execution in ordinary CTest/build.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/VstBusLayout.h"
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include "../src/NativeFxRackArchive.h"
#include <windows.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
int fail(const char* reason) { std::cerr << "FACTORYFX_HOST_REJECTED " << reason << "\n"; return 1; }

// The module is already loaded by JUCE. Query its actual VST3 factory,
// not moduleinfo.json, without a second InitDll or independent audio host.
// Windows TUID layout follows the SDK's COM-compatible INLINE_UID order.
juce::String runtimeClassId(const juce::File& module, const juce::PluginDescription& desc) {
    const auto binary = module.getChildFile("Contents/x86_64-win").getChildFile(module.getFileName());
    auto handle = GetModuleHandleW(binary.getFullPathName().toWideCharPointer());
    if (!handle) return {};
    using FactoryProc = Steinberg::IPluginFactory* (PLUGIN_API*)();
    auto proc = reinterpret_cast<FactoryProc>(GetProcAddress(handle, "GetPluginFactory"));
    if (!proc) return {};
    auto* factory = proc();
    if (!factory) return {};
    struct Release { Steinberg::IPluginFactory* p; ~Release() { p->release(); } } release{factory};
    const auto count = factory->countClasses();
    if (count < 1 || count > 256) return {};
    juce::String result;
    for (int i = 0; i < count; ++i) {
        Steinberg::PClassInfo info{};
        if (factory->getClassInfo(i, &info) != Steinberg::kResultOk) return {};
        if (std::strncmp(info.category, "Audio Module Class", sizeof(info.category)) != 0) continue;
        if (std::memchr(info.name, 0, sizeof(info.name)) == nullptr) return {};
        if (juce::String::fromUTF8(info.name).trim() != desc.name) continue;
        constexpr std::array<int,16> order{3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
        std::uint32_t hash = 0, word = 0;
        juce::String cid;
        for (int n = 0; n < 16; ++n) {
            const auto byte = static_cast<unsigned char>(info.cid[order[n]]);
            cid += juce::String::toHexString(static_cast<int>(byte)).paddedLeft('0', 2);
            word = (word << 8) | byte;
            if (n % 4 == 3) { hash = hash * 31u + word; word = 0; }
        }
        if (std::bit_cast<std::int32_t>(hash) != desc.uniqueId) continue;
        if (result.isNotEmpty()) return {}; // ambiguous runtime identity
        result = cid.toUpperCase();
    }
    return result;
}
struct Render {
    bool valid = true;
    double energy = 0., delta = 0., peak = 0.;
};
Render render(juce::AudioPluginInstance& p, double rate, int frames, bool mono, bool bypass) {
    Render result;
    juce::AudioBuffer<float> pcm(2, frames);
    juce::MidiBuffer midi;
    const int blocks = juce::jmax(192, static_cast<int>(rate / frames));
    for (int block=0; block<blocks; ++block) {
        pcm.clear(); midi.clear();
        for (int i=0; i<frames; ++i) {
            const double t = (block * frames + i) / rate;
            const float left = static_cast<float>(0.15 * std::sin(t * 6283.185307179586) + 0.07 * std::sin(t * 1256.637061435917));
            const float right = mono ? left : static_cast<float>(0.12 * std::sin(t * 4404.513) + 0.06 * std::sin(t * 1884.955592));
            pcm.setSample(0,i,left); pcm.setSample(1,i,right);
        }
        if (bypass) p.processBlockBypassed(pcm,midi);
        else p.processBlock(pcm,midi);
        for (int i=0; i<frames; ++i) {
            const double t = (block * frames + i) / rate;
            const double dryL=0.15*std::sin(t*6283.185307179586)+0.07*std::sin(t*1256.637061435917);
            const double dryR=mono?dryL:0.12*std::sin(t*4404.513)+0.06*std::sin(t*1884.955592);
            for (int ch=0; ch<2; ++ch) {
                const double v=pcm.getSample(ch,i);
                if (!std::isfinite(v)) { result.valid=false; return result; }
                const double d=v-(ch==0?dryL:dryR);
                result.energy+=v*v; result.delta+=d*d;
                result.peak=juce::jmax(result.peak,std::abs(v));
            }
        }
    }
    result.valid=result.energy>1.e-12 && result.peak<32.;
    return result;
}
bool prepare(juce::AudioPluginInstance& p,double rate,int frames) {
    const auto layoutError=armix::vst::prepareStereoEffectLayout(p);
    if(layoutError.isNotEmpty()) { std::cerr<<layoutError<<"\n"; return false; }
    if(p.getTotalNumInputChannels()!=2 || p.getTotalNumOutputChannels()!=2) return false;
    p.setRateAndBufferSizeDetails(rate,frames);
    p.prepareToPlay(rate,frames);
    return true;
}
juce::AudioProcessorParameter* findParam(juce::AudioPluginInstance& p,const juce::String& name) {
    for(auto* v:p.getParameters()) if(v->getName(128).trim().equalsIgnoreCase(name)&&v->isAutomatable()) return v;
    return nullptr;
}
bool set(juce::AudioProcessorParameter* p,float value) {
    if(!p) return false;
    p->beginChangeGesture(); p->setValueNotifyingHost(value); p->endChangeGesture();
    return std::isfinite(p->getValue()) && std::abs(p->getValue()-value)<.025f;
}

// Actual vendor instances in the production ChannelId/EdgeId PCM publisher,
// not a second mixer or a mock AudioProcessor. The direct and parallel AUX
// paths reconverge at MAIN; the two leases contribute their real latency.
bool qualifyGraph(std::unique_ptr<juce::AudioPluginInstance> first,
                  std::unique_ptr<juce::AudioPluginInstance> second,
                  const juce::PluginDescription& desc,
                  const juce::String& parameter, float value,
                  juce::DynamicObject& receipt) {
    using namespace armix::mixer;
    using Bank = DynamicChannelFxLeaseBank;
    constexpr int frames=128;
    constexpr std::uint64_t epoch=991;
    if (!prepare(*first,48000.,frames)||!prepare(*second,48000.,frames)) return false;
    DynamicChannelRoster roster;
    std::array<ChannelEdit,3> adds{};
    for (int i=0;i<3;++i) {
        adds[i].kind=ChannelEdit::Kind::add;
        adds[i].position=static_cast<std::size_t>(i);
        adds[i].role=i==0?ChannelRole::input:i==1?ChannelRole::fxReturn:ChannelRole::main;
        adds[i].format=i==0?ProcessingFormat::mono:ProcessingFormat::stereo;
        adds[i].text=i==0?"CH1":i==1?"AUX RETURN":"MAIN";
    }
    const auto created=roster.edit(0,adds);
    if (!created.ok()||created.createdIds.size()!=3) return false;
    const auto ch=created.createdIds[0],aux=created.createdIds[1],main=created.createdIds[2];
    std::array<ChannelEdit,3> routes{};
    for (auto& op:routes) op.kind=ChannelEdit::Kind::addSendReference;
    routes[0].id=ch;routes[0].target=main;routes[0].primaryOutput=true;routes[0].value=1.f;
    routes[1].id=ch;routes[1].target=aux;routes[1].value=.5f;
    routes[2].id=aux;routes[2].target=main;routes[2].primaryOutput=true;routes[2].value=1.f;
    if (!roster.edit(roster.revision(),routes).ok()) return false;

    const auto catalog=juce::String("qualification-vst3-")+juce::String(desc.uniqueId);
    auto voice=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(first),catalog,desc.name,frames);
    auto ret=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(second),catalog,desc.name,frames);
    Bank::Assignment v,a;
    v.id=ch;v.format=ProcessingFormat::mono;v.inserts={voice};
    a.id=aux;a.format=ProcessingFormat::stereo;a.inserts={ret};
    std::array<Bank::Assignment,2> assignments{v,a};
    auto bank=std::make_shared<Bank>();
    if (Bank::prepare(epoch,1,assignments,*bank)!=Bank::Fault::none) return false;
    DynamicNativeSessionPublisher::FxPlan plan;
    plan.owner=bank;plan.context=bank.get();plan.deviceEpoch=epoch;plan.graphRevision=1;
    plan.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {
        (*static_cast<Bank*>(p))(id,l,r,n);
    };
    plan.latency=+[](void* p,ChannelId id) noexcept {
        return static_cast<Bank*>(p)->latencySamples(id);
    };
    DynamicGraphRequest request;
    request.physicalInputs=1;request.maxFrames=frames;request.sources={{ch,0,-1,false}};
    DynamicNativeSessionPublisher pub;
    if (!pub.commit(roster,request,0,epoch,epoch,true,plan).ok()) return false;
    const int latency=voice->latencySamples+ret->latencySamples;
    if (pub.latencySamples(epoch,1)!=latency) return false;
    std::array<float,frames> in{},l{},r{};
    const float* pins[]{in.data()};
    int fallback=0;
    const auto renderBlock=[&](bool muted=false) {
        return pub.render(epoch,pins,1,l.data(),r.data(),frames,muted,
            [&](ChannelId,float*,float*,int) noexcept { ++fallback; });
    };
    double energy=0.;
    const auto beforeVoice=voice->dspProcessedBlocks.load();
    const auto beforeReturn=ret->dspProcessedBlocks.load();
    for (int b=0;b<256;++b) {
        for (int i=0;i<frames;++i) {
            const double t=(b*frames+i)/48000.;
            in[i]=static_cast<float>(.12*std::sin(t*6283.185307179586)+.04*std::sin(t*1256.637061435917));
        }
        if (!renderBlock()) return false;
        for (int i=0;i<frames;++i) {
            if (!std::isfinite(l[i])||!std::isfinite(r[i])||
                std::abs(l[i])>32.f||std::abs(r[i])>32.f) return false;
            energy+=static_cast<double>(l[i])*l[i]+static_cast<double>(r[i])*r[i];
        }
    }
    if (energy<=1.e-9||fallback!=0||voice->processFaulted()||ret->processFaulted()||
        voice->latencyChanged.load()||ret->latencyChanged.load()||
        voice->dspProcessedBlocks.load()-beforeVoice!=256||
        ret->dspProcessedBlocks.load()-beforeReturn!=256) return false;

    // Use the actual CONTROL capture and hashed rack codec, then recall the
    // opaque vendor state into the other real instance. Corrupt bytes must
    // not produce a replacement rack. This is rack recall, not a full Show.
    armix::storage::FxState state;
    state.name=desc.name;state.catalogId=catalog;state.path=desc.fileOrIdentifier;state.uid=desc.uniqueId;
    if (!voice->tryCaptureState(state.binary)||state.binary.getSize()==0) return false;
    armix::storage::FxRackFile rack,decoded;
    rack.single=true;rack.effects.push_back(state);
    juce::MemoryBlock bytes;
    if (armix::storage::NativeFxRackArchive::encode(rack,bytes).isNotEmpty()||
        armix::storage::NativeFxRackArchive::decode(bytes,decoded).isNotEmpty()||
        decoded.effects.size()!=1||decoded.effects[0].uid!=desc.uniqueId||
        decoded.effects[0].catalogId!=catalog||decoded.effects[0].binary!=state.binary) return false;
    auto damaged=bytes;
    static_cast<char*>(damaged.getData())[0]^=1;
    armix::storage::FxRackFile rejected;
    if (armix::storage::NativeFxRackArchive::decode(damaged,rejected).isEmpty()||
        !rejected.effects.empty()) return false;
    auto* other=dynamic_cast<juce::AudioPluginInstance*>(ret->processor.get());
    if (!other) return false;
    auto* control=findParam(*other,parameter);
    if (!set(control,value<.55f?.78f:.22f)) return false;
    other->setStateInformation(decoded.effects[0].binary.getData(),
                               static_cast<int>(decoded.effects[0].binary.getSize()));
    control=findParam(*other,parameter);
    if (!control||std::abs(control->getValue()-value)>.025f) return false;

    if (bank->setInsertBypass(epoch,0,ch,0,voice->instanceId,true)||
        !bank->setInsertBypass(epoch,1,ch,0,voice->instanceId,true)||
        !bank->setInsertBypass(epoch,1,aux,0,ret->instanceId,true)) return false;
    // Drain bypass envelope and all compensation histories, then measure
    // the actual aligned direct+AUX impulse, not merely reported latency.
    in.fill(0.f);
    for (int b=0;b<64+latency/frames;++b) if (!renderBlock()) return false;
    const auto parkedVoice=voice->dspProcessedBlocks.load();
    const auto parkedReturn=ret->dspProcessedBlocks.load();
    int impulse=-1;
    for (int b=0;b<4+latency/frames;++b) {
        in.fill(0.f);if (b==0) in[0]=.1f;
        if (!renderBlock()) return false;
        for (int i=0;i<frames;++i) {
            if (!std::isfinite(l[i])||!std::isfinite(r[i])) return false;
            if (std::abs(l[i])>1.e-5f||std::abs(r[i])>1.e-5f) {
                if (impulse!=-1) return false;
                impulse=b*frames+i;
            }
        }
    }
    if (impulse!=latency||voice->dspProcessedBlocks.load()!=parkedVoice||
        ret->dspProcessedBlocks.load()!=parkedReturn||fallback!=0) return false;
    // Reject an invalid candidate without replacing the accepted route.
    auto bad=plan;bad.graphRevision=2;bad.context=nullptr;
    if (pub.commit(roster,request,1,epoch,epoch,true,bad).ok()||
        pub.acceptedRevision()!=1) return false;
    in.fill(.1f);
    if (!renderBlock(true)) {
        // Muted render may report false; silence is the invariant.
    }
    for (int i=0;i<frames;++i) if (l[i]!=0.f||r[i]!=0.f) return false;
    receipt.setProperty("graphChannelAuxMain",true);
    receipt.setProperty("graphLatencySamples",latency);
    receipt.setProperty("graphBypassedImpulseSample",impulse);
    receipt.setProperty("graphWetEnergy",energy);
    receipt.setProperty("graphRenderBlocksPerLease",256);
    receipt.setProperty("graphRackStateRecall",true);
    receipt.setProperty("graphRejectedCandidatePreservedRevision",true);
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File module(juce::String::fromUTF8(argv[1])), receipt(juce::String::fromUTF8(argv[2]));
    if(!module.isDirectory()||!module.hasFileExtension("vst3")||receipt.exists()) return 2;
    juce::AudioPluginFormatManager formats;
    auto format=std::make_unique<juce::VST3PluginFormat>();
    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile(types,module.getFullPathName());
    formats.addFormat(std::move(format));
    if(types.size()!=1||types[0]->isInstrument||types[0]->uniqueId==0) return fail("discovery");
    const auto& d=*types[0];
    juce::String error;
    auto first=formats.createPluginInstance(d,48000.,128,error);
    auto second=formats.createPluginInstance(d,48000.,128,error);
    if(!first||!second) return fail("two-instance-load");
    const auto loaded=first->getPluginDescription(), loaded2=second->getPluginDescription();
    if(loaded.uniqueId!=d.uniqueId||loaded2.uniqueId!=d.uniqueId||loaded.name!=d.name||loaded.version!=d.version)
        return fail("loaded-description-mismatch");
    const auto cid=runtimeClassId(module,loaded);
    if(cid.length()!=32) return fail("actual-factory-cid");
    if(!prepare(*first,48000.,128)||!prepare(*second,48000.,128)) return fail("production-stereo-layout");
    auto before=render(*first,48000.,128,false,false);
    if(!before.valid) return fail("baseline-pcm");
    juce::AudioProcessorParameter* parameter=nullptr;
    if(d.name=="ArSonEQ") {
        if(!set(findParam(*first,"Band 1 Enabled"),1.f)) return fail("eq-band-enable");
        parameter=findParam(*first,"Band 1 Gain");
    } else if(d.name=="ArSonComp") parameter=findParam(*first,"Makeup");
    else if(d.name=="ArSonVerb") parameter=findParam(*first,"Mix");
    else if(d.name=="ArSonKuPik") {
        for(const auto* name:{"Output Gain","Output","Master Gain","Makeup","Mix","Intensity","Amount"}) {
            parameter=findParam(*first,name);
            if(parameter&&!parameter->isDiscrete()) break;
            parameter=nullptr;
        }
        // Only automate a normal continuous DSP control; never touch licence,
        // activation, preset, update or payment operations.
        if(!parameter) for(auto* p:first->getParameters()) {
            const auto name=p->getName(128).toLowerCase();
            if(p->isAutomatable()&&!p->isDiscrete()&&!p->isMetaParameter()&&
                (name.contains("gain")||name.contains("mix")||name.contains("amount")||
                 name.contains("intensity")||name.contains("level"))&&
                !name.contains("match")&&!name.contains("auto")&&!name.contains("license")&&
                !name.contains("activat")&&!name.contains("payment")&&!name.contains("update")) {
                parameter=p; break;
            }
        }
    }
    if(!parameter) return fail("known-dsp-automation-control");
    const auto paramName=parameter->getName(128);
    const auto initial=parameter->getValue();
    const float changed=initial<.55f?.78f:.22f;
    if(!set(parameter,changed)) return fail("automation-readback");
    const auto wet=render(*first,48000.,128,false,false);
    if(!wet.valid||wet.delta<1.e-6) return fail("active-dsp-pcm");
    juce::MemoryBlock state;
    first->getStateInformation(state);
    if(state.getSize()==0||state.getSize()>64u*1024u*1024u) return fail("state-bound");
    second->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    auto* recalled=findParam(*second,paramName);
    if(!recalled||std::abs(recalled->getValue()-parameter->getValue())>.025f)
        return fail("state-parameter-recall");
    if(d.name=="ArSonEQ") {
        const auto* enabled=findParam(*second,"Band 1 Enabled");
        if(!enabled||enabled->getValue()<.5f) return fail("eq-enabled-recall");
    }
    const auto bypass=render(*first,48000.,128,false,true);
    if(!bypass.valid) return fail("bypass-pcm");
    first->releaseResources(); second->releaseResources();
    juce::Array<juce::var> profiles;
    for(const auto rate:{44100.,48000.,96000.}) for(const auto frames:{64,128,256}) {
        if(!prepare(*first,rate,frames)||!prepare(*second,rate,frames)) return fail("multirate-layout");
        const auto stereo=render(*first,rate,frames,false,false);
        const auto mono=render(*second,rate,frames,true,false);
        if(!stereo.valid||!mono.valid) return fail("multirate-pcm");
        auto row=std::make_unique<juce::DynamicObject>();
        row->setProperty("sampleRate",rate); row->setProperty("buffer",frames);
        row->setProperty("latencySamples",first->getLatencySamples());
        row->setProperty("stereoPeak",stereo.peak); row->setProperty("monoAdaptedPeak",mono.peak);
        profiles.add(juce::var(row.release()));
        first->releaseResources(); second->releaseResources();
    }
    if(!prepare(*first,48000.,128)) return fail("editor-prepare");
    if(!first->hasEditor()) return fail("editor-unavailable");
    for(int i=0;i<2;++i) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(first->createEditorIfNeeded());
        if(!editor) return fail("editor-create-reopen");
    }
    first->releaseResources();
    auto row=std::make_unique<juce::DynamicObject>();
    row->setProperty("hostUniqueId",d.uniqueId); row->setProperty("runtimeClassId",cid);
    row->setProperty("name",d.name); row->setProperty("version",d.version);
    row->setProperty("automationParameter",paramName); row->setProperty("automationValue",changed);
    row->setProperty("stateBytes",static_cast<int>(state.getSize()));
    row->setProperty("wetDifferenceEnergy",wet.delta); row->setProperty("bypassPeak",bypass.peak);
    row->setProperty("editorCreated",true); row->setProperty("editorReopened",true);
    row->setProperty("profiles",juce::var(profiles));
    if (!qualifyGraph(std::move(first),std::move(second),d,paramName,changed,*row)) return fail("production-channel-aux-main-graph");
    row->setProperty("scope","actual-factory-cid-production-stereo-layout-two-instance-automation-state-pcm-bypass-9-profiles-mono-adapted-editor-reopen; production ChannelId graph with CH/AUX/MAIN, PDC impulse, rack recall, rollback; no physical IO or full application UI/Show qualification");
    return receipt.replaceWithText(juce::JSON::toString(juce::var(row.release())))?0:9;
}
