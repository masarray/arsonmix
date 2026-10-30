// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/MixerRoutingPlan.h"
#include "../engine/core/DynamicChannelRoster.h"
#include <array>
#if defined(NDEBUG)
#undef NDEBUG // Never skip P1A assertions in Release native CTest.
#endif
#include <cassert>
#include <cmath>
#include <limits>

using namespace armix::mixer;
static bool close(float a,float b,float tol=1.e-5f){return std::abs(a-b)<tol;}


static void verifyDynamicChannelRoster() {
    DynamicChannelRoster bank;
    std::vector<ChannelEdit> addOps;
    for(std::size_t i=0;i<128;++i){
        ChannelEdit a;a.kind=ChannelEdit::Kind::add;a.position=i;addOps.push_back(a);
    }
    auto result=bank.edit(0,addOps);
    assert(result.ok()&&bank.size()==128&&result.createdIds.size()==128);
    const auto first=result.createdIds.front(),last=result.createdIds.back();
    auto format=ChannelEdit{};format.kind=ChannelEdit::Kind::setFormat;
    format.id=last;format.format=ProcessingFormat::stereo;
    auto fx=ChannelEdit{};fx.kind=ChannelEdit::Kind::addInsertReference;
    fx.id=last;fx.text="vst3:example";
    const std::vector<ChannelEdit> cfg{format,fx};
    result=bank.edit(1,cfg);
    assert(result.ok()&&bank.find(last)->format==ProcessingFormat::stereo);
    assert(bank.find(last)->insertCatalogIds.size()==1);
    assert(bank.find(first)->insertCatalogIds.empty());
    auto move=ChannelEdit{};move.kind=ChannelEdit::Kind::move;
    move.id=last;move.position=0;
    result=bank.edit(2,std::span<const ChannelEdit>(&move,1));
    assert(result.ok()&&bank.channels().front().id==last);
    auto dup=ChannelEdit{};dup.kind=ChannelEdit::Kind::duplicate;
    dup.id=last;dup.position=1;
    result=bank.edit(3,std::span<const ChannelEdit>(&dup,1));
    assert(result.ok()&&result.createdIds.size()==1);
    const auto copyId=result.createdIds.front();
    assert(copyId!=last&&bank.find(copyId)->insertCatalogIds==bank.find(last)->insertCatalogIds);
    assert(bank.find(copyId)->sends.empty());
    auto send=ChannelEdit{};send.kind=ChannelEdit::Kind::addSendReference;
    send.id=last;send.target=copyId;send.value=.5f;
    result=bank.edit(4,std::span<const ChannelEdit>(&send,1));
    assert(result.ok()&&bank.find(last)->sends[0].destination==copyId);
    auto erase=ChannelEdit{};erase.kind=ChannelEdit::Kind::remove;erase.id=copyId;
    result=bank.edit(5,std::span<const ChannelEdit>(&erase,1));
    assert(result.fault==ChannelEditFault::linkedDestination&&bank.revision()==5);
    auto rename=ChannelEdit{};rename.kind=ChannelEdit::Kind::rename;
    rename.id=last;rename.text="Lead Vocal";
    auto invalid=ChannelEdit{};invalid.kind=ChannelEdit::Kind::setPan;
    invalid.id=first;invalid.value=std::numeric_limits<float>::quiet_NaN();
    const std::vector<ChannelEdit> reject{rename,invalid};
    result=bank.edit(5,reject);
    assert(result.fault==ChannelEditFault::invalidValue&&bank.revision()==5);
    assert(bank.find(last)->name!="Lead Vocal");
    result=bank.edit(4,std::span<const ChannelEdit>(&rename,1));
    assert(result.fault==ChannelEditFault::staleRevision);
    DynamicChannelRoster small({sizeof(ChannelStripDefinition)*2+100,8,32});
    const std::vector<ChannelEdit> over{addOps[0],addOps[1],addOps[2]};
    result=small.edit(0,over);
    assert(result.fault==ChannelEditFault::budgetExceeded&&small.size()==0);
    assert(small.revision()==0);
    result=small.edit(0,std::span<const ChannelEdit>(&addOps[0],1));
    assert(result.ok()&&small.channels().front().id.value==1);
    DynamicChannelRoster large;
    std::vector<ChannelEdit> bulk;bulk.reserve(1024);
    for(std::size_t i=0;i<1024;++i){
        ChannelEdit a;a.kind=ChannelEdit::Kind::add;a.position=i;bulk.push_back(a);
    }
    result=large.edit(0,bulk);
    assert(result.ok()&&large.size()==1024);
    assert(large.channels().back().id.value==1024);
    assert(large.channels().back().insertCatalogIds.empty());
}

int main() {
    verifyDynamicChannelRoster();
    Configuration cfg;
    Plan plan;Error err;
    assert(Plan::compile(cfg,2,plan,err));
    assert(plan.activeStrips==0);
    const std::array<float,4> a={1.f,2.f,3.f,4.f};
    const std::array<float,4> b={.25f,.5f,.75f,1.f};
    const std::array<float,4> c={-1.f,-2.f,-3.f,-4.f};
    const float* input[]={a.data(),b.data(),c.data()};
    std::array<float,4> l{},r{},ml{},mr{};
    assert(!plan.extract(0,input,3,l.data(),r.data(),4));
    for(int i=0;i<4;++i)assert(l[i]==0.f&&r[i]==0.f);
    cfg.strips[0]={SourceMode::stereo,0,1,1.f,0.f,false,false};
    cfg.strips[1]={SourceMode::mono,2,-1,1.f,0.f,false,false};
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==2);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    Plan::clearMaster(ml.data(),mr.data(),4);
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    assert(close(ml[0],1.f)&&close(mr[0],.25f)); // stereo transparency
    assert(plan.extract(1,input,3,l.data(),r.data(),4));
    plan.accumulate(1,l.data(),r.data(),ml.data(),mr.data(),4);
    constexpr float center=.7071067811865475f;
    assert(close(ml[0],1.f-center)&&close(mr[0],.25f-center));
    plan.finishMaster(ml.data(),mr.data(),4);
    // Muting other bus must be independent, never change strip 0.
    cfg.strips[1].mute=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    Plan::clearMaster(ml.data(),mr.data(),4);
    assert(!plan.extract(1,input,3,l.data(),r.data(),4));
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    assert(close(ml[3],4.f)&&close(mr[3],1.f));
    // Solo arbitration is determined off RT, and mute wins over solo.
    cfg.strips[1].mute=false;cfg.strips[1].solo=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    assert(!plan.extract(0,input,3,l.data(),r.data(),4));
    assert(plan.extract(1,input,3,l.data(),r.data(),4));
    cfg.strips[1].mute=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    // Stereo balance and true mono master happen after summing.
    cfg.strips[1]={};
    cfg.master={2.f,1.f,false,true};
    assert(Plan::compile(cfg,3,plan,err));
    Plan::clearMaster(ml.data(),mr.data(),4);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    plan.finishMaster(ml.data(),mr.data(),4);
    assert(close(ml[0],.25f)&&close(mr[0],.25f)); // right-only, doubled, mono
    // Reject illegal hardware routes, NaNs and stereo self-alias; NO partial commit.
    const auto old=plan;
    cfg.strips[2]={SourceMode::stereo,1,9,1.f,0.f,false,false};
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::inputOutOfRange&&err.strip==2);
    assert(plan.activeStrips==old.activeStrips&&plan.masterMono==old.masterMono);
    cfg.strips[2].inputB=1;
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::duplicateStereoInput);
    cfg.strips[2]={};
    cfg.strips[0].pan=std::numeric_limits<float>::quiet_NaN();
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidPan&&err.strip==0);
    cfg.strips[0].pan=0.f;
    cfg.strips[3].source=static_cast<SourceMode>(255);
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidSourceMode&&err.strip==3);
    cfg.strips[3]={};
    cfg.master.gainLinear=10.f;
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidGain&&err.strip==-1);
    cfg.master.gainLinear=1.f;
    assert(!Plan::compile(cfg,-1,plan,err)&&err.code==Fault::invalidInputCount);
    // 10 independent mono strips, no fixed 2-channel or 1-channel shortcut.
    cfg=Configuration{};
    std::array<std::array<float,1>,stripCount> sources{};
    std::array<const float*,stripCount> pointers{};
    for(int i=0;i<stripCount;++i){
        sources[i][0]=static_cast<float>(i+1);
        pointers[i]=sources[i].data();
        cfg.strips[i]={SourceMode::mono,i,-1,1.f,-1.f,false,false};
    }
    assert(Plan::compile(cfg,stripCount,plan,err)&&plan.activeStrips==stripCount);
    float sumL=0.f,sumR=0.f,cl=0.f,cr=0.f;
    for(int i=0;i<stripCount;++i){
        assert(plan.extract(i,pointers.data(),stripCount,&cl,&cr,1));
        plan.accumulate(i,&cl,&cr,&sumL,&sumR,1);
    }
    assert(close(sumL,55.f)&&close(sumR,0.f));
    // Bad/null device input fails closed and does not leak another strip.
    assert(!plan.extract(9,pointers.data(),2,&cl,&cr,1));
    assert(cl==0.f&&cr==0.f);
}
