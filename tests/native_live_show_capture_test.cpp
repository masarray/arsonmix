// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeLiveShowCapture.h"
#include "../src/NativeShowStore.h"

namespace {
juce::var obj(){return juce::var(new juce::DynamicObject());}
void set(juce::var& x,const char* k,juce::var v){
    x.getDynamicObject()->setProperty(juce::Identifier(k),std::move(v));
}
juce::var routeRow(const juce::String& mode,int a=-1,int b=-1){
    auto x=obj();
    set(x,"mode",mode);set(x,"left",a);set(x,"right",b);
    set(x,"gain",0.5);set(x,"pan",0.0);
    set(x,"main",true);set(x,"mute",false);set(x,"solo",false);
    set(x,"fxId","");
    return x;
}
juce::var send(int from,int to,const char* tap){
    auto x=obj();
    set(x,"from",from);set(x,"to",to);set(x,"tap",tap);
    set(x,"gain",0.35);set(x,"pan",-0.2);
    return x;
}
juce::var setup(){
    juce::Array<juce::var> channelRows,liveControls,auxes,sends,ports;
    for(int i=0;i<10;++i) {
        channelRows.add(i==0?routeRow("MONO",3):
                        i==1?routeRow("STEREO",6,8):
                        i==2?routeRow("BUS"):routeRow("NONE"));
        auto control=obj();
        set(control,"linear",i==0?0.0:i==1?0.75:0.25);
        set(control,"pan",i==1?22.0:0.0);
        set(control,"mute",i==0);set(control,"solo",i==1);
        liveControls.add(control);
    }
    for(int i=0;i<4;++i) {
        auto a=obj();set(a,"enabled",i<2);set(a,"gain",i==0?0.7:1.0);
        set(a,"main",true);set(a,"mute",false);set(a,"fxId","");
        auxes.add(a);
    }
    sends.add(send(0,2,"POST"));
    sends.add(send(1,10,"PRE"));
    sends.add(send(0,11,"POST"));
    for(int n:{3,6,8}) {
        auto p=obj();set(p,"port",n);ports.add(p);
    }
    auto route=obj();set(route,"deviceEpoch",static_cast<juce::int64>(9));
    set(route,"revision",static_cast<juce::int64>(4));
    set(route,"channels",juce::var(channelRows));
    set(route,"auxes",juce::var(auxes));
    set(route,"sends",juce::var(sends));
    auto lab=obj();
    set(lab,"active",true);set(lab,"ready",true);
    set(lab,"epoch",static_cast<juce::int64>(9));
    set(lab,"revision",static_cast<juce::int64>(5));
    set(lab,"fxCount",0);
    set(lab,"acceptedRoute",route);
    set(lab,"channelControls",juce::var(liveControls));
    set(lab,"ports",juce::var(ports));
    auto root=obj();
    set(root,"monitor",false);set(root,"adaptiveClockActive",false);
    set(root,"diagnosticRoute","NORMAL");
    set(root,"masterLinear",0.55);
    set(root,"masterMute",false);set(root,"masterMono",true);
    set(root,"dryRoutingLab",lab);
    return root;
}
}
int main() {
    using namespace armix::shows;
    auto status=setup();
    SnapshotV2 captured;
    const auto success=NativeLiveShowCapture::captureDry(status,"Dry Routing Test",captured);
    if(success.isNotEmpty())std::cerr<<"Capture failure: "<<success<<"\n";
    assert(success.isEmpty());
    assert(detail::uuid(captured.id));
    assert(captured.strips[0].source==1&&captured.strips[0].inputA==3);
    assert(captured.strips[0].fader==0.0); // true hard -infinity, no dB rounding
    assert(captured.strips[0].mute&&!captured.strips[0].solo);
    assert(captured.strips[1].source==2&&captured.strips[1].inputB==8);
    assert(captured.strips[1].pan==0.22&&captured.strips[1].solo);
    assert(captured.strips[2].busReturn&&captured.strips[2].source==0);
    assert(captured.strips[0].sends.size()==1&&captured.strips[0].sends[0].destination==2);
    assert(captured.aux[0].enabled&&captured.aux[0].gain>0.69);
    assert(captured.auxSends.size()==2&&captured.auxSends[0].preFader);
    assert(captured.auxSends[0].source==1&&captured.auxSends[0].destination==0);
    assert(!captured.auxSends[1].preFader);
    assert(captured.masterGain==0.55&&captured.masterMono);
    const auto stable=Codec::encode(captured);
    SnapshotV2 decoded;
    assert(Codec::decode(stable,decoded));
    assert(decoded.strips[2].busReturn&&decoded.strips[0].fader==0.0);

    // Never let untrusted or stale UI state replace an accepted capture.
    const auto prior=Codec::encode(captured);
    auto armed=setup();set(armed,"monitor",true);
    assert(NativeLiveShowCapture::captureDry(armed,"Dry Routing Test",captured).isNotEmpty());
    assert(Codec::encode(captured)==prior);
    auto mutated=setup();
    auto lab=mutated.getProperty("dryRoutingLab",{});
    set(lab,"revision",static_cast<juce::int64>(6));
    assert(NativeLiveShowCapture::captureDry(mutated,"Dry Routing Test",captured).isNotEmpty());
    auto plugin=setup();
    lab=plugin.getProperty("dryRoutingLab",{});
    set(lab,"fxCount",1);
    assert(NativeLiveShowCapture::captureDry(plugin,"Dry Routing Test",captured).isNotEmpty());
    auto portsLost=setup();lab=portsLost.getProperty("dryRoutingLab",{});
    juce::Array<juce::var> ports;auto p=obj();set(p,"port",3);ports.add(p);
    set(lab,"ports",juce::var(ports));
    assert(NativeLiveShowCapture::captureDry(portsLost,"Dry Routing Test",captured).isNotEmpty());
    assert(Codec::encode(captured)==prior);

    const auto home=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixNativeDryCaptureV3","",false);
    assert(home.createDirectory().wasOk());
    NativeShowStore store(home);
    ShowV2 show;
    show.id=juce::Uuid().toDashedString().toLowerCase();
    show.name=captured.name;
    show.createdMs=captured.createdMs;show.modifiedMs=captured.modifiedMs;
    show.currentSnapshotId=captured.id;show.snapshotOrder={captured.id};
    assert(store.commit(show,{captured},{}).isEmpty());
    ShowV2 reread;std::vector<SnapshotV2> snaps;
    assert(store.load(show.id,reread,snaps).isEmpty());
    assert(snaps.size()==1&&Codec::encode(snaps[0])==prior);
    assert(home.deleteRecursively());
    std::cout<<"PASS: actual ACK dry routing capture, 10 INPUT / 4 AUX, bus identity, live linear, master, PRE/POST, stale epoch/plugin/ports rejection, transactional persistence\n";
}
