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
}
