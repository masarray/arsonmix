// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DeviceIndependentMixerSession.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace armix::mixer;
static ChannelId id(std::uint64_t n) {return {n};}
static void valid(bool condition) {assert(condition);}

int main() {
    DeviceIndependentMixerSession logical;
    valid(logical.initialized());
    valid(logical.mainId()==id(15));
    valid(logical.roster().size()==15);
    valid(logical.revision()==1);

    // 1) A genuine session document needs NO ASIO, WASAPI, fake pin,
    // callback, graph publisher or monitor before a normal track edit.
    ChannelEdit create;
    create.kind=ChannelEdit::Kind::add;
    create.role=ChannelRole::input;
    create.format=ProcessingFormat::stereo;
    create.text="Backing";
    create.position=10;
    create.connectMainOnCreate=true;
    auto added=logical.edit(1,std::span<const ChannelEdit>{&create,1});
    valid(added.accepted() && added.createdIds.size()==1);
    const auto backing=added.createdIds.front();
    valid(backing.value>15);
    const auto* newTrack=logical.roster().find(backing);
    valid(newTrack && newTrack->role==ChannelRole::input &&
          newTrack->name=="Backing");
    valid(newTrack->sends.size()==1 &&
          newTrack->sends.front().destination==logical.mainId() &&
          newTrack->sends.front().primaryOutput);
    const auto stableMain=logical.mainId();

    // 2) Optimistic stale writes are rejected without changing the
    // authoritative native descriptor. No browser-only edits.
    ChannelEdit rename;
    rename.kind=ChannelEdit::Kind::rename;
    rename.id=backing;rename.text="Backing Vocal";
    const auto stale=logical.edit(1,std::span<const ChannelEdit>{&rename,1});
    valid(!stale.accepted() && stale.fault==ChannelEditFault::staleRevision);
    valid(logical.roster().find(backing)->name=="Backing");
    auto renamed=logical.edit(logical.revision(),
                             std::span<const ChannelEdit>{&rename,1});
    valid(renamed.accepted() &&
          logical.roster().find(backing)->name=="Backing Vocal");

    // 3) Independent BUS and serial / parallel routes preserve stable IDs.
    const auto bus=logical.createBus(logical.revision(),"Music BUS",ChannelRole::group);
    valid(bus.accepted() && bus.createdIds.size()==1);
    const auto busId=bus.createdIds.front();
    valid(busId!=backing && busId!=stableMain);
    WireChange serial;
    serial.kind=WireChange::Kind::routeOnly;
    serial.from=backing;serial.to=busId;serial.gain=1.f;
    const auto routed=logical.wire(logical.revision(),
                                  std::span<const WireChange>{&serial,1});
    valid(routed.accepted());
    const auto* track=logical.roster().find(backing);
    valid(track && track->sends.size()==1 &&
          track->sends.front().destination==busId);
    const auto serialEdge=track->sends.front().edgeId;

    WireChange send;
    send.kind=WireChange::Kind::connect;
    send.from=backing;send.to=stableMain;send.gain=.25f;
    const auto parallel=logical.wire(logical.revision(),
                                    std::span<const WireChange>{&send,1});
    valid(parallel.accepted());
    track=logical.roster().find(backing);
    valid(track->sends.size()==2);
    valid(track->sends.front().edgeId==serialEdge);
    valid(track->sends.back().destination==stableMain &&
          !track->sends.back().primaryOutput);

    // 4) SOURCE ONLY removes primary MAIN but leaves the strip and
    // its explicit parallel SEND. Removing the last send keeps the channel.
    ChannelEdit sourceOnly;
    sourceOnly.kind=ChannelEdit::Kind::setPrimaryOutput;
    sourceOnly.id=backing;
    sourceOnly.target={};
    auto disconnected=logical.edit(logical.revision(),
                                   std::span<const ChannelEdit>{&sourceOnly,1});
    valid(disconnected.accepted());
    track=logical.roster().find(backing);
    valid(track && track->sends.size()==1 && !track->sends[0].primaryOutput);
    WireChange remove;
    remove.kind=WireChange::Kind::remove;
    remove.from=backing;
    remove.edgeId=track->sends[0].edgeId;
    valid(logical.wire(logical.revision(),
                       std::span<const WireChange>{&remove,1}).accepted());
    valid(logical.roster().find(backing)->sends.empty());

    // 5) A WIRE cycle is rejected at the logical authority. The candidate
    // is never committed, even though local endpoint edits are valid.
    WireChange toBus;
    toBus.kind=WireChange::Kind::routeOnly;
    toBus.from=backing;toBus.to=busId;toBus.gain=1.f;
    valid(logical.wire(logical.revision(),
                       std::span<const WireChange>{&toBus,1}).accepted());
    WireChange cycle;
    cycle.kind=WireChange::Kind::connect;
    cycle.from=busId;cycle.to=backing;cycle.gain=.5f;
    const auto revisionBeforeCycle=logical.revision();
    const auto rejected=logical.wire(logical.revision(),
                                    std::span<const WireChange>{&cycle,1});
    valid(!rejected.accepted() && logical.revision()==revisionBeforeCycle);
    valid(logical.roster().find(busId)->sends.front().destination==stableMain);

    // 6) Format, gain, pan, mute, solo and FX catalog references survive
    // an exact stable-ID document restore with no hardware in the loop.
    std::vector<ChannelEdit> settings;
    ChannelEdit format;format.kind=ChannelEdit::Kind::setFormat;
    format.id=backing;format.format=ProcessingFormat::mono;
    settings.push_back(format);
    ChannelEdit gain;gain.kind=ChannelEdit::Kind::setFader;
    gain.id=backing;gain.value=.5f;
    settings.push_back(gain);
    ChannelEdit pan;pan.kind=ChannelEdit::Kind::setPan;
    pan.id=backing;pan.value=-.2f;
    settings.push_back(pan);
    ChannelEdit mute;mute.kind=ChannelEdit::Kind::setMute;
    mute.id=backing;mute.enabled=true;
    settings.push_back(mute);
    ChannelEdit solo;solo.kind=ChannelEdit::Kind::setSolo;
    solo.id=backing;solo.enabled=true;
    settings.push_back(solo);
    ChannelEdit fx;fx.kind=ChannelEdit::Kind::addInsertReference;
    fx.id=backing;fx.text="armix.builtin.channel-compressor.v1";
    settings.push_back(fx);
    valid(logical.edit(logical.revision(),settings).accepted());
    DeviceIndependentMixerSession restored;
    valid(restored.restore(logical.roster()));
    valid(restored.revision()==logical.revision());
    valid(restored.mainId()==stableMain);
    const auto* row=restored.roster().find(backing);
    valid(row && row->format==ProcessingFormat::mono &&
          std::abs(row->faderLinear-.5f)<1.e-6f &&
          std::abs(row->pan+.2f)<1.e-6f && row->mute && row->solo &&
          row->insertCatalogIds.size()==1 &&
          row->insertCatalogIds.front()=="armix.builtin.channel-compressor.v1");

    // 7) Removing a BUS reconnects its exclusive routes to MAIN.
    valid(restored.removeBus(restored.revision(),busId).accepted());
    valid(!restored.roster().find(busId));
    const auto* rewired=restored.roster().find(backing);
    valid(rewired && rewired->sends.size()==1 &&
          rewired->sends.front().destination==stableMain &&
          rewired->sends.front().primaryOutput);

    // 8) Delete track removes ONLY that stable ChannelId and its wires.
    valid(restored.removeInput(restored.revision(),backing).accepted());
    valid(!restored.roster().find(backing));
    valid(restored.mainId()==stableMain);
    std::cout<<"Device-independent logical mixer: 8 scenarios PASS\n";
}
