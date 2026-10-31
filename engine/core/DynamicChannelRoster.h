// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

// DYN0: control-thread session model, NOT a second renderer or a LIVE audio graph.
// One state type serves every future input, return, group, monitor and master.
// Audio graph publication must still use RealtimeRoutePublisher and native ACK.
namespace armix::mixer {

struct ChannelId final {
    std::uint64_t value=0;
    friend bool operator==(ChannelId,ChannelId)=default;
};
enum class ChannelRole : std::uint8_t { input, fxReturn, group, monitor, main };
enum class ProcessingFormat : std::uint8_t { mono, stereo };
struct ChannelSendRef final {
    ChannelId destination{};
    float gain=1.f;
    bool preFader=false;
    float pan=0.f; // send-only balance: PRE mono uses equal-power coefficients
    bool primaryOutput=false; // OUT selector edge, not a parallel SEND
};
struct ChannelStripDefinition final {
    ChannelId id{};
    ChannelRole role=ChannelRole::input;
    ProcessingFormat format=ProcessingFormat::mono;
    std::string name;
    float faderLinear=1.f, pan=0.f;
    bool mute=false, solo=false;
    // Catalog identifiers ONLY. No vendor processor object is copied or built.
    // The existing audio control thread owns independent VST instance creation.
    std::vector<std::string> insertCatalogIds;
    std::vector<ChannelSendRef> sends;
};

struct ChannelRosterBudget final {
    std::size_t definitionBytes=32U*1024U*1024U; // configurable memory admission, not a CH count
    std::size_t maximumInsertSlotsPerChannel=8; // same native rack capability on EVERY channel
    std::size_t maximumSendsPerChannel=32;       // DYN0 references only; live DAG validates separately
};

enum class ChannelEditFault : std::uint8_t {
    none, staleRevision, invalidAction, missingChannel, invalidPosition,
    invalidValue, budgetExceeded, linkedDestination, idExhausted
};
struct ChannelEdit final {
    enum class Kind : std::uint8_t {
        add, duplicate, remove, move, rename, setFormat, setFader, setPan,
        setMute, setSolo, addInsertReference, addSendReference, setPrimaryOutput
    } kind=Kind::add;
    ChannelId id{};       // existing channel (except add)
    ChannelId target{};   // send destination
    std::size_t position=0;
    ChannelRole role=ChannelRole::input;
    ProcessingFormat format=ProcessingFormat::mono;
    float value=0.f;
    bool enabled=false;      // mute/solo value (or future enable operation)
    bool preFader=false;     // send tap at construction, no hidden POST default
    bool primaryOutput=false; // exclusive OUT edge, independent of parallel SEND
    float sendPan=0.f;      // independent send PAN, never channel fader PAN
    std::string text;
};
struct ChannelEditResult final {
    ChannelEditFault fault=ChannelEditFault::none;
    std::size_t actionIndex=0;
    std::uint64_t revision=0;
    std::vector<ChannelId> createdIds;
    bool ok() const noexcept {return fault==ChannelEditFault::none;}
};

class DynamicChannelRoster final {
public:
    explicit DynamicChannelRoster(ChannelRosterBudget budget={}) : budget_(budget) {}
    std::uint64_t revision() const noexcept {return revision_;}
    std::size_t size() const noexcept {return channels_.size();}
    const std::vector<ChannelStripDefinition>& channels() const noexcept {return channels_;}
    const ChannelStripDefinition* find(ChannelId id) const noexcept {
        const auto it=std::find_if(channels_.begin(),channels_.end(),
            [id](const auto& c){return c.id==id;});
        return it==channels_.end()?nullptr:&*it;
    }

    // A single structural transaction. All mutation and allocations occur on
    // control thread. Validation failures never change the accepted draft.
    // This does NOT publish audio; the existing muted native APPLY owns LIVE.
    ChannelEditResult edit(std::uint64_t expectedRevision,
                           std::span<const ChannelEdit> operations) {
        ChannelEditResult result;
        result.revision=revision_;
        if(expectedRevision!=revision_) {result.fault=ChannelEditFault::staleRevision;return result;}
        if(operations.empty() || revision_==std::numeric_limits<std::uint64_t>::max()) {
            result.fault=ChannelEditFault::invalidAction;return result;
        }
        auto candidate=channels_; // one copy per BATCH, never in audio callback
        auto next=nextId_;
        const auto seek=[&candidate](ChannelId id){
            return std::find_if(candidate.begin(),candidate.end(),
                [id](const auto& c){return c.id==id;});
        };
        for(std::size_t i=0;i<operations.size();++i) {
            const auto& op=operations[i];
            result.actionIndex=i;
            if(op.kind==ChannelEdit::Kind::add || op.kind==ChannelEdit::Kind::duplicate) {
                if(next==0 || next==std::numeric_limits<std::uint64_t>::max()) {
                    result.fault=ChannelEditFault::idExhausted;break;
                }
                if(op.position>candidate.size()) {
                    result.fault=ChannelEditFault::invalidPosition;break;
                }
                ChannelStripDefinition row;
                if(op.kind==ChannelEdit::Kind::duplicate) {
                    const auto from=seek(op.id);
                    if(from==candidate.end()) {result.fault=ChannelEditFault::missingChannel;break;}
                    row=*from; // copy settings/IDs only, never share a VST processor
                    row.sends.clear(); // never duplicate live sends / feedback topology implicitly
                } else {
                    if(!validRole(op.role)) {result.fault=ChannelEditFault::invalidValue;break;}
                    if(!validFormat(op.format)) {result.fault=ChannelEditFault::invalidValue;break;}
                    row.role=op.role;
                    row.format=op.format;
                }
                row.id={next++};
                if(!op.text.empty())row.name=op.text;
                else if(row.name.empty())row.name="Channel "+std::to_string(row.id.value);
                if(!validName(row.name)) {result.fault=ChannelEditFault::invalidValue;break;}
                candidate.insert(candidate.begin()+static_cast<std::ptrdiff_t>(op.position),std::move(row));
                result.createdIds.push_back(candidate[op.position].id);
            } else {
                auto it=seek(op.id);
                if(op.id.value==0 || it==candidate.end()) {
                    result.fault=ChannelEditFault::missingChannel;break;
                }
                switch(op.kind) {
                case ChannelEdit::Kind::remove: {
                    const auto referenced=std::any_of(candidate.begin(),candidate.end(),[&](const auto& c){
                        return c.id!=op.id && std::any_of(c.sends.begin(),c.sends.end(),[&](const auto& s){
                            return s.destination==op.id;
                        });
                    });
                    if(referenced) {result.fault=ChannelEditFault::linkedDestination;break;}
                    candidate.erase(it);break;
                }
                case ChannelEdit::Kind::move: {
                    if(op.position>=candidate.size()) {result.fault=ChannelEditFault::invalidPosition;break;}
                    const auto old=static_cast<std::size_t>(it-candidate.begin());
                    // Move a descriptor, NOT a plugin instance. IDs/edges stay stable.
                    if(old<op.position)
                        std::rotate(candidate.begin()+old,candidate.begin()+old+1,
                                    candidate.begin()+op.position+1);
                    else if(op.position<old)
                        std::rotate(candidate.begin()+op.position,candidate.begin()+old,
                                    candidate.begin()+old+1);
                    break;
                }
                case ChannelEdit::Kind::rename:
                    if(!validName(op.text))result.fault=ChannelEditFault::invalidValue;
                    else it->name=op.text;
                    break;
                case ChannelEdit::Kind::setFormat:
                    if(!validFormat(op.format))result.fault=ChannelEditFault::invalidValue;
                    else it->format=op.format;
                    break;
                case ChannelEdit::Kind::setFader:
                    if(!std::isfinite(op.value)||op.value<0.f||op.value>4.f)
                        result.fault=ChannelEditFault::invalidValue;
                    else it->faderLinear=op.value;
                    break;
                case ChannelEdit::Kind::setPan:
                    if(!std::isfinite(op.value)||op.value< -1.f||op.value>1.f)
                        result.fault=ChannelEditFault::invalidValue;
                    else it->pan=op.value;
                    break;
                case ChannelEdit::Kind::setMute:
                    it->mute=op.enabled;
                    break;
                case ChannelEdit::Kind::setSolo:
                    it->solo=op.enabled;
                    break;
                case ChannelEdit::Kind::addInsertReference:
                    if(op.text.empty() || op.text.size()>256 ||
                       it->insertCatalogIds.size()>=budget_.maximumInsertSlotsPerChannel)
                        result.fault=ChannelEditFault::budgetExceeded;
                    else it->insertCatalogIds.push_back(op.text);
                    break;
                case ChannelEdit::Kind::addSendReference:
                    if(op.primaryOutput && std::any_of(it->sends.begin(),it->sends.end(),
                        [](const auto& s){return s.primaryOutput;}))
                        result.fault=ChannelEditFault::invalidValue;
                    else if(op.target.value==0 || op.target==op.id || seek(op.target)==candidate.end())
                        result.fault=ChannelEditFault::invalidValue;
                    else if(it->sends.size()>=budget_.maximumSendsPerChannel)
                        result.fault=ChannelEditFault::budgetExceeded;
                    else if(!std::isfinite(op.value)||op.value<0.f||op.value>4.f||
                            !std::isfinite(op.sendPan)||op.sendPan< -1.f||op.sendPan>1.f)
                        result.fault=ChannelEditFault::invalidValue;
                    else it->sends.push_back({op.target,op.value,op.preFader,op.sendPan,op.primaryOutput});
                    break;
                case ChannelEdit::Kind::setPrimaryOutput: {
                    if(it->role==ChannelRole::main || it->role==ChannelRole::monitor ||
                       op.target==op.id || (op.target.value && seek(op.target)==candidate.end())) {
                        result.fault=ChannelEditFault::invalidValue;break;
                    }
                    auto& edges=it->sends;
                    edges.erase(std::remove_if(edges.begin(),edges.end(),
                        [](const auto& x){return x.primaryOutput;}),edges.end());
                    if(op.target.value) {
                        if(edges.size()>=budget_.maximumSendsPerChannel) {
                            result.fault=ChannelEditFault::budgetExceeded;break;
                        }
                        edges.push_back({op.target,1.f,false,0.f,true});
                    }
                    break;
                }
                default:result.fault=ChannelEditFault::invalidAction;break;
                }
                if(result.fault!=ChannelEditFault::none)break;
            }
            // Cheap cardinality preflight per action; full variable-size accounting
            // runs ONCE per batch, not O(N^2) for 1000-channel sessions.
            if(candidate.size()>budget_.definitionBytes/sizeof(ChannelStripDefinition)) {
                result.fault=ChannelEditFault::budgetExceeded;break;
            }
        }
        if(result.ok() && !withinBudget(candidate))result.fault=ChannelEditFault::budgetExceeded;
        if(!result.ok()) {result.createdIds.clear();return result;}
        channels_.swap(candidate);
        nextId_=next;
        result.revision=++revision_;
        return result;
    }

    // One Studio Console transaction: new independent BUS + multi-source
    // primary output reroute, or FX return + parallel sends. CONTROL THREAD.
    // Caller must compile one native graph and publish with device epoch/ACK.
    ChannelEditResult createBusForSelected(
        std::uint64_t expectedRevision,std::span<const ChannelId> selected,
        std::string name,ChannelRole role,float parallelGain=.25f) {
        ChannelEditResult result;result.revision=revision_;
        if(expectedRevision!=revision_) {
            result.fault=ChannelEditFault::staleRevision;return result;
        }
        if((role!=ChannelRole::group && role!=ChannelRole::fxReturn) ||
           selected.empty() || !validName(name) ||
           !std::isfinite(parallelGain)||parallelGain<0.f||parallelGain>4.f ||
           revision_==std::numeric_limits<std::uint64_t>::max()) {
            result.fault=ChannelEditFault::invalidAction;return result;
        }
        if(nextId_==0 || nextId_==std::numeric_limits<std::uint64_t>::max()) {
            result.fault=ChannelEditFault::idExhausted;return result;
        }
        auto candidate=channels_;
        auto mainIt=std::find_if(candidate.begin(),candidate.end(),
            [](const auto& row){return row.role==ChannelRole::main;});
        if(mainIt==candidate.end() || std::count_if(candidate.begin(),candidate.end(),
            [](const auto& row){return row.role==ChannelRole::main;})!=1) {
            result.fault=ChannelEditFault::invalidAction;return result;
        }
        const auto mainId=mainIt->id;
        const ChannelId busId{nextId_};
        std::unordered_set<std::uint64_t> seen;
        seen.reserve(selected.size());
        for(std::size_t i=0;i<selected.size();++i) {
            const auto id=selected[i];
            result.actionIndex=i;
            if(id.value==0 || !seen.insert(id.value).second) {
                result.fault=ChannelEditFault::invalidAction;return result;
            }
            auto it=std::find_if(candidate.begin(),candidate.end(),
                [id](const auto& row){return row.id==id;});
            if(it==candidate.end()) {
                result.fault=ChannelEditFault::missingChannel;return result;
            }
            if(it->role==ChannelRole::main || it->role==ChannelRole::monitor) {
                result.fault=ChannelEditFault::invalidValue;return result;
            }
            if(role==ChannelRole::group) {
                it->sends.erase(std::remove_if(it->sends.begin(),it->sends.end(),
                    [](const auto& edge){return edge.primaryOutput;}),
                    it->sends.end());
            }
            if(it->sends.size()>=budget_.maximumSendsPerChannel) {
                result.fault=ChannelEditFault::budgetExceeded;return result;
            }
            it->sends.push_back({busId,role==ChannelRole::group?1.f:parallelGain,
                                 false,0.f,role==ChannelRole::group});
        }
        ChannelStripDefinition bus;
        bus.id=busId;bus.role=role;bus.format=ProcessingFormat::stereo;
        bus.name=std::move(name);
        bus.sends.push_back({mainId,1.f,false,0.f,true});
        const auto position=static_cast<std::size_t>(mainIt-candidate.begin());
        candidate.insert(candidate.begin()+static_cast<std::ptrdiff_t>(position),
                         std::move(bus));
        if(!withinBudget(candidate)) {
            result.fault=ChannelEditFault::budgetExceeded;return result;
        }
        channels_.swap(candidate);
        ++nextId_;result.revision=++revision_;
        result.createdIds.push_back(busId);
        return result;
    }

private:
    static bool validFormat(ProcessingFormat f) noexcept {
        return f==ProcessingFormat::mono || f==ProcessingFormat::stereo;
    }
    static bool validRole(ChannelRole r) noexcept {
        return r==ChannelRole::input || r==ChannelRole::fxReturn ||
            r==ChannelRole::group || r==ChannelRole::monitor || r==ChannelRole::main;
    }
    static bool validName(const std::string& s) noexcept {
        return !s.empty() && s.size()<=96 &&
            std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32;});
    }
    bool withinBudget(const std::vector<ChannelStripDefinition>& rows) const noexcept {
        std::size_t remaining=budget_.definitionBytes;
        for(const auto& row:rows) {
            const auto debit=[&remaining](std::size_t bytes){
                if(bytes>remaining)return false;
                remaining-=bytes;return true;
            };
            if(!validRole(row.role)||!validFormat(row.format)||!validName(row.name)||
               row.insertCatalogIds.size()>budget_.maximumInsertSlotsPerChannel||
               row.sends.size()>budget_.maximumSendsPerChannel||
               !debit(sizeof(ChannelStripDefinition))||!debit(row.name.size())||
               !debit(row.insertCatalogIds.size()*sizeof(std::string))||
               !debit(row.sends.size()*sizeof(ChannelSendRef)))return false;
            for(const auto& name:row.insertCatalogIds)
                if(name.empty()||name.size()>256||!debit(name.size()))return false;
        }
        return true;
    }
    ChannelRosterBudget budget_;
    std::vector<ChannelStripDefinition> channels_;
    std::uint64_t revision_=0, nextId_=1;
};
} // namespace armix::mixer
