// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include "WetReturnCatalog.h"
namespace armix::mixer {
// One roster revision for an entire rack replacement, preserving all other
// channel fields and edges. Empty replacement is a real latency-free rack.
inline bool prepareRackCatalogReplacement(const DynamicChannelRoster& prior,
        std::uint64_t revision,ChannelId target,
        const std::vector<std::string>& catalogs,DynamicChannelRoster& next) {
    const auto* row=prior.find(target);
    if(!row||row->role==ChannelRole::monitor||revision!=prior.revision()||
       catalogs.size()>8)return false;
    const auto hasWet=[](const auto& ids){return std::any_of(ids.begin(),ids.end(),isWetReturnCatalog);};
    if(hasWet(catalogs)&&row->role!=ChannelRole::fxReturn)return false;
    // A wet-only return remains a wet-only return even with zero incoming
    // WIREs. Otherwise a later assignment would silently become dry gain.
    // Delete the return channel explicitly, or replace with a wet catalog.
    if(hasWet(row->insertCatalogIds)&&!hasWet(catalogs))return false;
    std::vector<ChannelEdit> ops;
    for(std::size_t n=row->insertCatalogIds.size();n>0;--n){
        ChannelEdit edit;edit.kind=ChannelEdit::Kind::removeInsertReference;
        edit.id=target;edit.position=n-1;ops.push_back(std::move(edit));
    }
    for(const auto& id:catalogs){
        if(id.empty()||id.size()>256)return false;
        ChannelEdit edit;edit.kind=ChannelEdit::Kind::addInsertReference;
        edit.id=target;edit.text=id;ops.push_back(std::move(edit));
    }
    if(ops.empty()){
        ChannelEdit edit;edit.kind=ChannelEdit::Kind::touchDocumentRevision;
        edit.id=target;ops.push_back(edit);
    }
    auto candidate=prior;
    if(!candidate.edit(revision,ops).ok())return false;
    next=std::move(candidate);return true;
}
}
