// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelFxLeaseBank.h"
namespace armix::mixer {
// CONTROL-only preparation. Roster and actual processor inventory move
// together; a failed candidate never mutates the currently audible graph.
inline bool prepareNativeFxRackEdit(std::uint64_t epoch,std::uint64_t graphRevision,
        std::uint64_t rosterRevision,const DynamicChannelRoster& prior,
        const DynamicChannelFxLeaseBank& bank,ChannelId source,std::size_t slot,
        const juce::String& token,ChannelId target,std::size_t position,
        DynamicChannelRoster& next,DynamicChannelFxLeaseBank& nextBank) {
    if(bank.epoch()!=epoch||bank.revision()!=graphRevision||prior.revision()!=rosterRevision)
        return false;
    const auto* from=prior.find(source);
    const auto* to=target.value?prior.find(target):nullptr;
    const auto* lease=bank.ownerForControl(source,slot);
    if(!from||!lease||!*lease||(*lease)->instanceId!=token||
       from->insertCatalogIds.size()!=bank.insertCount(source)||
       slot>=from->insertCatalogIds.size()||
       from->insertCatalogIds[slot]!=(*lease)->catalogId.toStdString()||
       (target.value&&(!to||to->role==ChannelRole::monitor||
                      to->insertCatalogIds.size()!=bank.insertCount(target))))return false;
    // Wet-only lease is a distinct operator contract, not an ordinary
    // insert. Never expose dry on an already patched return when its final
    // wet stage disappears. Disconnect its incoming WIREs or delete the bus.
    if((*lease)->silenceBypass){
        if(from->role!=ChannelRole::fxReturn||
           (to&&to->role!=ChannelRole::fxReturn))return false;
        if(target!=source){
            bool another=false;
            for(std::size_t i=0;i<bank.insertCount(source);++i){
                const auto* p=bank.ownerForControl(source,i);
                if(i!=slot&&p&&*p&&(*p)->silenceBypass)another=true;
            }
            if(!another)for(const auto& row:prior.channels())
                for(const auto& send:row.sends)
                    if(send.destination==source)return false;
        }
    }
    DynamicChannelFxLeaseBank prepared;
    if(DynamicChannelFxLeaseBank::editOwned(epoch,graphRevision+1,bank,source,slot,
           token,target,position,to?to->format:from->format,prepared)!=
           DynamicChannelFxLeaseBank::Fault::none)return false;
    auto roster=prior;
    ChannelEdit edit;edit.kind=target.value?ChannelEdit::Kind::moveInsertReference:
                                              ChannelEdit::Kind::removeInsertReference;
    edit.id=source;edit.position=slot;edit.target=target;edit.insertPosition=position;
    if(!roster.edit(rosterRevision,std::span<const ChannelEdit>(&edit,1)).ok())return false;
    next=std::move(roster);nextBank=std::move(prepared);return true;
}

// DMX5: pure CONTROL candidate. Only the last Reverb-Sis insert on a direct
// MAIN input is converted; other insert ordering and existing sends survive.
// One output publication follows this preparation; failure is non-destructive.
inline bool prepareNativeReverbReturnConversion(std::uint64_t epoch,
        std::uint64_t graphRevision,std::uint64_t rosterRevision,
        const DynamicChannelRoster& prior,const DynamicChannelFxLeaseBank& bank,
        ChannelId source,std::size_t slot,const juce::String& token,
        DynamicChannelFxLeaseBank::Lease replacement,float returnGain,
        DynamicChannelRoster& next,DynamicChannelFxLeaseBank& nextBank,ChannelId& created) {
    constexpr auto insertId="armix.builtin.vocal-reverb.v1";
    constexpr auto returnId="armix.builtin.vocal-reverb-return.v1";
    if(bank.epoch()!=epoch||bank.revision()!=graphRevision||prior.revision()!=rosterRevision||
       !replacement||replacement->catalogId!=returnId||!replacement->silenceBypass||
       !std::isfinite(returnGain)||returnGain<0.f||returnGain>1.f)return false;
    const auto* from=prior.find(source);
    const auto* old=bank.ownerForControl(source,slot);
    if(!from||from->role!=ChannelRole::input||!old||!*old||
       (*old)->catalogId!=insertId||(*old)->instanceId!=token||
       from->insertCatalogIds.size()!=bank.insertCount(source)||
       slot>=from->insertCatalogIds.size()||slot+1!=from->insertCatalogIds.size()||
       from->insertCatalogIds[slot]!=insertId)return false;
    ChannelId main{},monitor{};
    for(const auto& row:prior.channels()){
        if(row.role==ChannelRole::main)main=row.id;
        if(row.role==ChannelRole::monitor)monitor=row.id;
    }
    if(!main.value||std::none_of(from->sends.begin(),from->sends.end(),
        [&](const auto& e){return e.primaryOutput&&e.destination==main;}))return false;
    auto roster=prior;
    const std::array<ChannelId,1> selected{source};
    auto bus=roster.createBusForSelected(rosterRevision,selected,"Reverb-Sis Return",
                                         ChannelRole::fxReturn,1.f);
    if(!bus.ok()||bus.createdIds.size()!=1)return false;
    const auto id=bus.createdIds.front();
    ChannelEdit remove;remove.kind=ChannelEdit::Kind::removeInsertReference;
    remove.id=source;remove.position=slot;
    ChannelEdit add;add.kind=ChannelEdit::Kind::addInsertReference;add.id=id;add.text=returnId;
    ChannelEdit level;level.kind=ChannelEdit::Kind::setFader;level.id=id;level.value=returnGain;
    std::vector<ChannelEdit> edits{remove,add,level};
    if(monitor.value){
        // Explicit conversion creates a PRE_FADER monitor send at silence.
        // No unexpected wet on the singer's headphones; SOF owns its gain.
        ChannelEdit cue;cue.kind=ChannelEdit::Kind::addSendReference;
        cue.id=id;cue.target=monitor;cue.preFader=true;cue.value=0.f;edits.push_back(cue);
    }
    if(!roster.edit(roster.revision(),edits).ok())return false;
    DynamicChannelFxLeaseBank prepared;
    if(DynamicChannelFxLeaseBank::replaceOwnedOnNewChannel(epoch,graphRevision+1,
        bank,source,slot,token,id,std::move(replacement),prepared)!=
        DynamicChannelFxLeaseBank::Fault::none)return false;
    next=std::move(roster);nextBank=std::move(prepared);created=id;return true;
}
}
