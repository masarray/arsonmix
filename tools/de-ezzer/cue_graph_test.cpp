// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/core/DynamicNativeSessionPublisher.h"
#include "../../engine/builtin_fx/de_ezzer/DeEzzerControl.h"
#include <cstdlib>
#include <iostream>
using namespace armix::mixer;
namespace dz=armix::builtin_fx::de_ezzer;
static void require(bool v,const char* why){if(!v){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    constexpr int n=480;constexpr std::uint64_t ep=19,rev=1,route=123,instance=42,instanceEpoch=3;
    DynamicChannelRoster roster;
    std::array<ChannelEdit,3> rows{};
    const std::array<ChannelRole,3> roles{ChannelRole::input,ChannelRole::main,ChannelRole::monitor};
    const std::array<const char*,3> names{"VOICE","MAIN","MONITOR"};
    for(std::size_t i=0;i<rows.size();++i){rows[i].kind=ChannelEdit::Kind::add;rows[i].position=i;rows[i].role=roles[i];rows[i].format=ProcessingFormat::stereo;rows[i].text=names[i];}
    const auto made=roster.edit(0,rows);require(made.ok(),"roster");
    const auto voice=made.createdIds[0],main=made.createdIds[1],monitor=made.createdIds[2];
    ChannelEdit out;out.kind=ChannelEdit::Kind::setPrimaryOutput;out.id=voice;out.target=main;
    ChannelEdit send;send.kind=ChannelEdit::Kind::addSendReference;send.id=voice;send.target=monitor;send.value=.5f;
    std::array<ChannelEdit,2> links{out,send};require(roster.edit(1,links).ok(),"links");
    DynamicGraphRequest request;request.physicalInputs=2;request.maxFrames=n;request.sources={{voice,0,1,true}};
    DynamicNativeSessionPublisher ref,live;
    const std::array<int,4> reserved{0,1,2,3};
    const std::array<DynamicNativeSessionPublisher::InitialExternalTarget,1> targets{{{voice,4,5}}};
    const DynamicNativeSessionPublisher::InitialExternalPlan ext{6,reserved,targets};
    require(ref.commit(roster,request,0,ep,ep,true,{nullptr,nullptr,nullptr,0,0,nullptr},false,false,nullptr,&ext).ok(),"reference graph");
    require(live.commit(roster,request,0,ep,ep,true,{nullptr,nullptr,nullptr,0,0,nullptr},false,false,nullptr,&ext).ok(),"live graph");
    dz::Settings settings;settings.amount=1;settings.sensitivity=1;settings.rangeDb=18;settings.shape=dz::Shape::Focused;
    dz::Control baseline,candidate;require(baseline.prepare({instance,instanceEpoch},48000,2,settings)&&candidate.prepare({instance,instanceEpoch},48000,2,settings),"prepare");
    std::array<float,n> l{},r{};const float* in[]{l.data(),r.data()};
    std::array<std::array<float,n>,6> a{},b{};std::array<float*,6> pa{},pb{};
    for(std::size_t k=0;k<pa.size();++k){pa[k]=a[k].data();pb[k]=b[k].data();}
    int calls=0,claims=0,block=0;
    bool allowTap=true;
    const auto reference=[&](ChannelId id,float* x,float* y,int count) noexcept {if(id==voice)(void)baseline.processAudio(x,y,count);};
    const auto effect=[&](ChannelId id,float* x,float* y,int count) noexcept {
        if(id!=voice)return;
        ++calls;
        if(allowTap){
            require(!FxCueScope::claim(instance+1,instanceEpoch,count),"wrong instance");
            require(!FxCueScope::claim(instance,instanceEpoch+1,count),"wrong epoch");
            if(auto* cue=FxCueScope::claim(instance,instanceEpoch,count)){
                ++claims;require(!FxCueScope::claim(instance,instanceEpoch,count),"duplicate claim");
                if(candidate.processAudioWithCue(x,y,count,static_cast<dz::CueMode>(cue->mode),cue->left,cue->right)){cue->written=true;return;}
            }
        }
        (void)candidate.processAudio(x,y,count);
    };
    const auto render=[&](std::uint64_t cookie=route){
        for(int i=0;i<n;++i){const double t=(block*n+i)/48000.;
            l[i]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.22*std::sin(2*std::numbers::pi*6300*t));r[i]=-l[i];}
        ++block;const int before=calls;
        require(ref.render(ep,in,2,pa[0],pa[1],n,false,reference,pa[2],pa[3],{},{},{},pa.data(),6),"reference render");
        require(live.render(ep,in,2,pb[0],pb[1],n,false,effect,pb[2],pb[3],{},{},{},pb.data(),6,{},cookie),"native render");
        require(calls==before+1,"single render per voice");
        require(a[0]==b[0]&&a[1]==b[1],"MAIN/OBS exact equality");
        require(a[4]==b[4]&&a[5]==b[5],"external output exact equality");
    };
    auto& bus=live.cueControl();const FxCueKey key{ep,rev,route,instance,instanceEpoch};
    render();require(a[2]==b[2]&&a[3]==b[3]&&claims==0,"normal monitor unchanged");
    require(!bus.start({},FxCueMode::Ess,48000,0),"invalid key");
    require(bus.start(key,FxCueMode::Ess,48000,0),"ESS start");
    require(bus.state(key).applied==FxCueMode::Mix,"pending not applied");
    for(int i=0;i<12;++i)render();
    require(bus.state(key).applied==FxCueMode::Ess&&claims==12&&a[2]!=b[2],"ESS private monitor");
    float peak=0;for(auto v:b[2])peak=std::max(peak,std::abs(v));require(peak>.001f&&peak<=.5f,"audible bounded ESS");
    require(bus.start(key,FxCueMode::Delta,48000,100),"delta start");render();
    require(bus.state(key).applied==FxCueMode::Delta,"delta applied");
    bus.cancel(true);render();require(a[2].back()==b[2].back()&&a[3].back()==b[3].back(),"smooth stop settles to mix");
    render();require(a[2]==b[2]&&a[3]==b[3],"normal restored");
    require(bus.start(key,FxCueMode::Sidechain,48000,200),"SC start");render();
    require(bus.state(key).applied==FxCueMode::Sidechain,"SC applied");
    render(route+1);require(a[2]==b[2]&&bus.state(key).requested==FxCueMode::Mix,"route changed fails closed");
    require(!bus.renew(key,201),"cannot revive revoked cue");render();require(a[2]==b[2],"route restore does not rearm");
    require(bus.start(key,FxCueMode::Ess,48000,300),"explicit rearm");render(0);require(a[2]==b[2]&&!bus.renew(key,301),"physical gate revoked");
    require(bus.start(key,FxCueMode::Ess,48000,400),"rearm for missing tap");allowTap=false;render();
    require(a[2]==b[2]&&!bus.renew(key,401),"missing/bypassed tap revoked");allowTap=true;
    require(bus.start(key,FxCueMode::Ess,48000,500),"wall expiry");bus.expire(1000);render();require(a[2]==b[2],"expired no audio");
    require(bus.start(key,FxCueMode::Ess,48000,1100),"sample expiry");for(int i=0;i<51;++i)render();
    require(a[2]==b[2]&&!bus.renew(key,1101),"sample expiry cannot rearm");
    require(bus.start(key,FxCueMode::Ess,48000,1150),"mute gate");
    render();bus.revokePublished();render();require(a[2]==b[2]&&!bus.renew(key,1151),"asynchronous mute revokes");
    require(bus.start(key,FxCueMode::Ess,48000,1160),"failed callback");
    require(!live.render(ep,in,2,pb[0],pb[1],n,true,effect,pb[2],pb[3],{},{},{},pb.data(),6,{},route),"muted callback");
    render();require(a[2]==b[2]&&!bus.renew(key,1161),"muted callback cannot rearm");
    require(bus.start(key,FxCueMode::Ess,48000,1200),"graph replacement");
    require(live.commit(roster,request,rev,ep,ep,true,{nullptr,nullptr,nullptr,0,0,nullptr},false,false,nullptr,&ext).ok(),"new graph");
    require(bus.state(key).requested==FxCueMode::Mix,"graph invalidates selection");
    std::cout<<"PASS: actual ChannelId render, isolated MONITOR cue, exact MAIN/external PCM, route/identity/lease fences and single DSP pass\n";
}
