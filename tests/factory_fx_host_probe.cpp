// SPDX-License-Identifier: AGPL-3.0-only
// Isolated Windows qualification: no vendor execution in ordinary CTest/build.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/VstBusLayout.h"
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
    first->releaseResources(); first.reset(); second.reset();
    auto row=std::make_unique<juce::DynamicObject>();
    row->setProperty("hostUniqueId",d.uniqueId); row->setProperty("runtimeClassId",cid);
    row->setProperty("name",d.name); row->setProperty("version",d.version);
    row->setProperty("automationParameter",paramName); row->setProperty("automationValue",changed);
    row->setProperty("stateBytes",static_cast<int>(state.getSize()));
    row->setProperty("wetDifferenceEnergy",wet.delta); row->setProperty("bypassPeak",bypass.peak);
    row->setProperty("editorCreated",true); row->setProperty("editorReopened",true);
    row->setProperty("profiles",juce::var(profiles));
    row->setProperty("scope","actual-factory-cid-production-stereo-layout-two-instance-automation-state-pcm-bypass-9-profiles-mono-adapted-editor-reopen; no physical IO or full mixer graph");
    return receipt.replaceWithText(juce::JSON::toString(juce::var(row.release())))?0:9;
}
