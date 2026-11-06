// SPDX-License-Identifier: AGPL-3.0-only
// Windows' actual approved decoder -> accepted publisher -> sample renderer.
// Offline PCM evidence, never an assertion of physical ASIO/VST vendor FAT.
#include "../integration/ApprovedNativeControlIntent.h"
#include "../engine/core/DynamicAcceptedSession.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <new>

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

using namespace armix::mixer;
using namespace armix::console;
static bool near(float a,float b){return std::abs(a-b)<3.e-5f;}
static juce::String textId(std::uint64_t n){return juce::String(std::to_string(n));}
static juce::var object(){return juce::var(new juce::DynamicObject());}
static void put(const juce::var& v,const char* k,const juce::var& value) {
    v.getDynamicObject()->setProperty(k,value);
}
static const char* roleName(ChannelRole role) {
    switch(role) {
        case ChannelRole::input:return "INPUT";
        case ChannelRole::group:return "GROUP";
        case ChannelRole::fxReturn:return "FX_RETURN";
        case ChannelRole::main:return "MAIN";
        case ChannelRole::monitor:return "MONITOR";
    }
    return "INVALID";
}

struct PcmHost {
    DynamicAcceptedSession pcm;
    DynamicGraphRequest io;
    std::uint64_t epoch=91,serial=0;
    std::array<ChannelId,10> channels{};
    ChannelId bus{},main{};
    std::array<float,64> a{},b{},mic{},left{},right{};
    std::array<float,12> peaksL{},peaksR{};
    bool fxBypass=false;
    std::array<int,12> calls{};
    DynamicAcceptedSession::Result last;
    juce::String lastError;
    struct Result {bool accepted=false;const char* error="PCM_REJECTED";};

    PcmHost() {
        io.physicalInputs=3;io.maxFrames=64;
        std::vector<ChannelEdit> create;
        for(int i=0;i<12;++i) {
            ChannelEdit e;e.kind=ChannelEdit::Kind::add;e.position=i;
            e.role=i<10?ChannelRole::input:i==10?ChannelRole::group:ChannelRole::main;
            e.format=ProcessingFormat::stereo;e.text="CH "+std::to_string(i+1);
            create.push_back(e);
        }
        auto added=pcm.edit(0,0,create,io,epoch,epoch,true);
        assert(added.ok());
        for(int i=0;i<10;++i)channels[i]=added.edit.createdIds[i];
        bus=added.edit.createdIds[10];main=added.edit.createdIds[11];
        io.sources={{channels[2],0,1,true},{channels[6],2,-1,false}};
        std::vector<WireChange> links;
        for(auto from:{channels[2],channels[3],channels[6],bus}) {
            WireChange w;w.kind=WireChange::Kind::routeOnly;
            w.from=from;w.to=main;links.push_back(w);
        }
        assert(pcm.wireTransaction(1,1,links,io,epoch,epoch,true).ok());
        a.fill(.25f);b.fill(.125f);mic.fill(.1f);
        assert(pcm.setLiveMix(epoch,2,channels[3],.5f,0.f));
    }
    Result applyNativeWireTransaction(std::uint64_t ep,std::uint64_t rev,
        std::uint64_t roster,std::span<const WireChange> edits) {
        last=pcm.wireTransaction(roster,rev,edits,io,ep,epoch,false);
        return {last.ok(),last.ok()?"":"PCM_REJECTED"};
    }
    bool setNativeInputMix(std::uint64_t ep,std::uint64_t rev,
                           ChannelId id,float gain,float pan) {
        return pcm.setLiveMix(ep,rev,id,gain,pan);
    }
    bool setNativeBusMix(std::uint64_t ep,std::uint64_t rev,
                         ChannelId id,float gain,float pan) {
        return pcm.setLiveMix(ep,rev,id,gain,pan);
    }
    bool setNativeInputMuteSolo(std::uint64_t ep,std::uint64_t rev,
                                ChannelId id,bool mute,bool solo) {
        return pcm.setLiveMuteSolo(ep,rev,id,mute,solo);
    }
    // Serialize ONLY the accepted native roster/edge inventory. No guessed
    // channel labels or browser graph can authorize a destination here.
    juce::var readback() const {
        const auto rev=pcm.acceptedGraphRevision();
        const auto* view=pcm.acceptedRouting(epoch,rev);
        assert(view);
        auto result=object();put(result,"graphRevision",textId(rev));
        put(result,"rosterRevision",textId(pcm.acceptedRosterRevision()));
        juce::Array<juce::var> rows,edges,ops;
        ops.add("NATIVE_APPLY_WIRE_TRANSACTION");
        for(std::size_t i=0;i<view->channels.size();++i) {
            const auto& ch=view->channels[i];
            const auto* def=pcm.acceptedRoster().find(ch.id);assert(def);
            auto row=object();put(row,"id",textId(ch.id.value));
            put(row,"name",juce::String(ch.name));put(row,"role",roleName(ch.role));
            put(row,"format",ch.format==ProcessingFormat::stereo?"STEREO":"MONO");
            put(row,"primaryOutput",textId(ch.primaryOutput.value));
            put(row,"fader",def->faderLinear);put(row,"pan",def->pan);
            put(row,"mute",def->mute);put(row,"solo",def->solo);
            put(row,"inserts",juce::var(juce::Array<juce::var>{}));
            put(row,"peakL",peaksL[i]);put(row,"peakR",peaksR[i]);rows.add(row);
        }
        for(const auto& e:view->edges) {
            auto edge=object();put(edge,"edgeId",textId(e.edgeId));
            put(edge,"from",textId(e.from.value));put(edge,"to",textId(e.to.value));
            put(edge,"gainLinear",e.gainLinear);put(edge,"primaryOutput",e.primaryOutput);
            put(edge,"tap",e.preFader?"PRE_FADER":"POST_FADER");edges.add(edge);
        }
        put(result,"channels",juce::var(rows));put(result,"edges",juce::var(edges));
        put(result,"supportedOps",juce::var(ops));return result;
    }
    juce::var snapshot(const juce::var& native) const {
        auto s=juce::JSON::parse(R"({"device":{"epoch":"91"},"graph":{
          "capabilities":{}},"telemetry":{}})");
        put(s.getProperty("device",{}),"epoch",textId(epoch));
        assert(projectApprovedNativeV8(s,native,true));return s;
    }
    juce::var request(const juce::var& edit) {
        auto r=object();put(r,"requestId","pcm-wire-"+textId(++serial));
        put(r,"operation","APPLY_WIRE_TRANSACTION");
        put(r,"targetChannelId",edit.getProperty("from",{}));
        put(r,"deviceEpoch",textId(epoch));
        put(r,"baseGraphRevision",textId(pcm.acceptedGraphRevision()));
        put(r,"baseRosterRevision",textId(pcm.acceptedRosterRevision()));
        auto payload=object();juce::Array<juce::var> edits;edits.add(edit);
        put(payload,"changes",juce::var(edits));put(r,"payload",payload);return r;
    }
    bool apply(const juce::var& edit) {
        const auto native=readback();const auto r=request(edit);
        const auto decoded=translateApprovedNativeControl(r,snapshot(native),native);
        assert(decoded.ok());
        lastError.clear();
        const bool result=executeApprovedNativeControl(*this,decoded.translated,&lastError);
        pcm.pollRetirement();return result;
    }
    void render() {
        const float* pins[]{a.data(),b.data(),mic.data()};calls.fill(0);
        const auto fx=[&](ChannelId id,float* l,float* r,int n) noexcept {
            const auto slot=static_cast<std::size_t>(id.value-1);++calls[slot];
            if(id==channels[3]&&!fxBypass)
                for(int f=0;f<n;++f){l[f]*=.5f;r[f]*=.5f;}
        };
        const auto before=allocations.load();
        assert(pcm.render(epoch,pins,3,left.data(),right.data(),64,false,
                          fx,nullptr,nullptr,peaksL,peaksR));
        assert(allocations.load()==before); // real callback path, all cases
        assert(calls[2]==1&&calls[6]==1&&calls[11]==1);
    }
};
static juce::var link(const char* kind,ChannelId from,ChannelId to,float gain=1.f) {
    auto e=object();put(e,"op",kind);put(e,"from",textId(from.value));
    put(e,"to",textId(to.value));put(e,"tap","POST_FADER");
    put(e,"gainLinear",gain);return e;
}
static juce::var removePrimary(PcmHost& host,ChannelId from) {
    auto e=object();put(e,"op","REMOVE");put(e,"from",textId(from.value));
    const auto native=host.readback();
    for(const auto& edge:*native.getProperty("edges",{}).getArray())
        if(edge.getProperty("from","").toString()==textId(from.value)&&
           static_cast<bool>(edge.getProperty("primaryOutput",false))) {
            put(e,"edgeId",edge.getProperty("edgeId",{}));return e;
        }
    assert(false);return {};
}
int main() {
    PcmHost h;const auto ch3=h.channels[2],ch4=h.channels[3];
    constexpr float mono=.1f*.7071067811865475f;
    h.render();assert(near(h.left[63],.25f+mono));assert(h.peaksL[3]==0.f);
    // Ordinary WIRE CH03 -> CH04: same decoder used by Windows Main.cpp.
    assert(h.apply(link("CONNECT",ch3,ch4)));
    h.render();assert(near(h.peaksL[3],.0625f));
    assert(near(h.left[63],.3125f+mono)); // deliberate parallel path
    auto rb=h.readback();auto s=h.snapshot(rb);
    assert(s.getProperty("graph",{}).getProperty("destinations",{}).size()==12);
    assert(near(static_cast<float>(s.getProperty("telemetry",{})
        .getProperty("channelMeterRmsPeak",{}).getProperty(textId(ch4.value),{})
        .getProperty("peakL",0.)),.0625f));
    // ROUTE ONLY promotes the existing send EdgeId without double-dry.
    juce::String wireId;
    for(const auto& edge:*rb.getProperty("edges",{}).getArray())
        if(edge.getProperty("from","").toString()==textId(ch3.value)&&
           edge.getProperty("to","").toString()==textId(ch4.value))
            wireId=edge.getProperty("edgeId",{}).toString();
    assert(wireId.isNotEmpty());
    assert(h.apply(link("ROUTE_ONLY",ch3,ch4)));
    h.render();assert(near(h.left[63],.0625f+mono));
    const auto* view=h.pcm.acceptedRouting(h.epoch,h.pcm.acceptedGraphRevision());
    assert(view->find(ch3)->primaryOutput==ch4);
    bool retained=false;
    for(const auto& e:view->edges)if(e.from==ch3&&e.to==ch4)
        retained=textId(e.edgeId)==wireId;
    assert(retained);
    // Disconnect MAIN from receiver: both source and receiver DSP/meters
    // remain alive. Only the unrelated CH07 still reaches MAIN.
    const auto staleRemove=removePrimary(h,ch4);
    const auto staleRequest=h.request(staleRemove);
    assert(h.apply(staleRemove));h.render();
    assert(near(h.left[63],mono)&&near(h.peaksL[2],.25f));
    assert(near(h.peaksL[3],.0625f)&&h.calls[3]==1);
    // Live fader, stereo PAN, FX bypass, MUTE/SOLO while source-only.
    auto rev=h.pcm.acceptedGraphRevision();
    assert(h.pcm.setLiveMix(h.epoch,rev,ch4,.25f,1.f));
    h.render();h.render();assert(h.peaksL[3]==0.f&&near(h.peaksR[3],.015625f));
    h.fxBypass=true;h.render();assert(near(h.peaksR[3],.03125f));
    assert(h.pcm.setLiveMuteSolo(h.epoch,rev,ch4,true,false));
    h.render();h.render();assert(h.peaksR[3]==0.f);
    assert(h.pcm.setLiveMuteSolo(h.epoch,rev,ch4,false,true));
    h.render();h.render();assert(near(h.peaksR[3],.03125f));
    assert(h.left[63]==0.f&&h.right[63]==0.f); // unrelated CH07 solo-suppressed
    assert(h.pcm.setLiveMuteSolo(h.epoch,rev,ch4,false,false));
    assert(h.pcm.setLiveMix(h.epoch,rev,ch4,.5f,0.f));h.fxBypass=false;
    h.render();h.render();
    // Serial CH03 -> CH04 -> BUS -> MAIN, then source-only at CH03.
    assert(h.apply(link("ROUTE_ONLY",ch4,h.bus)));h.render();
    assert(near(h.left[63],.0625f+mono)&&near(h.peaksL[10],.0625f));
    assert(h.apply(removePrimary(h,ch3)));h.render();
    assert(near(h.peaksL[2],.25f)&&h.peaksL[3]==0.f);
    assert(near(h.left[63],mono));
    // Reconnect without touching physical IN; retarget the SAME EdgeId.
    assert(h.apply(link("ROUTE_ONLY",ch3,ch4)));h.render();
    auto retarget=removePrimary(h,ch3);put(retarget,"op","RETARGET");
    put(retarget,"to",textId(h.bus.value));
    assert(h.apply(retarget));h.render();assert(near(h.left[63],.25f+mono));
    // Hardware + WIRE are summed BEFORE receiver FX/fader, exactly once.
    auto io=h.io;io.sources.push_back({ch4,0,1,true});
    ChannelEdit width;width.kind=ChannelEdit::Kind::setFormat;
    width.id=ch4;width.format=ProcessingFormat::stereo;
    assert(h.pcm.edit(h.pcm.acceptedRosterRevision(),h.pcm.acceptedGraphRevision(),
        std::span<const ChannelEdit>(&width,1),io,h.epoch,h.epoch,true).ok());
    h.io=io;h.pcm.pollRetirement();
    assert(h.apply(link("ROUTE_ONLY",ch3,ch4)));h.render();
    assert(near(h.peaksL[3],.125f)&&near(h.left[63],.125f+mono));
    // Feedback cycle is rejected atomically by the actual graph compiler.
    const auto beforeRev=h.pcm.acceptedGraphRevision();
    assert(!h.apply(link("CONNECT",ch4,ch3)));
    assert(h.lastError=="PCM_REJECTED"); // preserve native reason through IPC executor
    assert(h.last.native.graphError.fault==DynamicGraphFault::cycle);
    assert(h.pcm.acceptedGraphRevision()==beforeRev);h.render();
    assert(near(h.left[63],.125f+mono));
    const auto current=h.readback();const auto accepted=h.snapshot(current);
    assert(translateApprovedNativeControl(staleRequest,accepted,current).error==
           "STALE_NATIVE_CONTROL_REVISION");
    auto wrongRole=juce::JSON::parse(juce::JSON::toString(current));
    put(wrongRole.getProperty("channels",{})[3],"role","MONITOR");
    auto fresh=h.request(link("CONNECT",ch3,ch4));
    assert(translateApprovedNativeControl(fresh,accepted,wrongRole).error==
           "WIRE_UNSUPPORTED_DESTINATION");
    h.epoch=92; // stale generation must not receive cached source PCM
    const float* pins[]{h.a.data(),h.b.data(),h.mic.data()};
    assert(!h.pcm.render(h.epoch,pins,3,h.left.data(),h.right.data(),64,false,
        +[](ChannelId,float*,float*,int) noexcept {}));
    assert(h.left[63]==0.f&&h.right[63]==0.f);
    PcmHost reconnected;reconnected.render();
    assert(near(reconnected.left[63],.25f+mono));
}
