// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace armix::device {
// CONTROL-only session model. Logical IDs are independent from physical
// pin ordinals and machine-local named presets. No PCM state or arm rights.
// This first slice intentionally models input ports only; the live session
// owner must commit a fully resolved candidate through its existing graph.
class LogicalInputRegistry final {
public:
    using PortId=std::uint64_t;
    using ChannelId=std::uint64_t;
    enum class Format { mono, stereo };
    enum class Resolution { noSource, needsRemap, resolved };
    struct Pin final { int physical=-1,callback=-1; std::string name; };
    struct Device final {
        // Exact backend and registered endpoints, not display aliases.
        std::string backend,capture,playback;
        bool operator==(const Device&) const=default;
        bool valid() const noexcept {
            return !backend.empty()&&!capture.empty()&&!playback.empty();
        }
    };
    struct Inventory final {
        Device device;
        std::uint64_t epoch=0;
        // Caller supplies fresh, verified enabled pins, in callback order.
        std::span<const Pin> pins;
        bool authorized=false;
    };
    struct Binding final {
        Device device;
        int left=-1,right=-1;
        std::string leftName,rightName;
    };
    struct Port final {
        PortId id=0;
        std::string name;
        Format format=Format::mono;
        std::optional<Binding> binding;
    };
    struct Reference final { ChannelId channel=0; PortId port=0; };
    struct Resolved final {
        Resolution status=Resolution::noSource;
        PortId port=0;
        Format format=Format::mono;
        int left=-1,right=-1; // callback ordinals, NEVER serialized
    };
    static constexpr std::size_t maxPorts=128,maxReferences=512;

    const std::vector<Port>& ports() const noexcept {return ports_;}
    const std::vector<Reference>& references() const noexcept {return refs_;}
    const Port* find(PortId id) const noexcept {
        const auto it=std::find_if(ports_.begin(),ports_.end(),[&](const auto& p){return p.id==id;});
        return it==ports_.end()?nullptr:&*it;
    }
    bool add(PortId id,std::string name,Format format) {
        if(!id||find(id)||ports_.size()>=maxPorts||!validName(name)||nameUsed(name,0)||
           (format!=Format::mono&&format!=Format::stereo))return false;
        ports_.push_back({id,std::move(name),format,{}});return true;
    }
    bool rename(PortId id,std::string name) {
        auto* p=edit(id);
        if(!p||!validName(name)||nameUsed(name,id))return false;
        p->name=std::move(name);return true;
    }
    bool erase(PortId id) {
        if(!find(id)||std::any_of(refs_.begin(),refs_.end(),[&](const auto& r){return r.port==id;}))
            return false;
        std::erase_if(ports_,[&](const auto& p){return p.id==id;});return true;
    }
    // Roster validation remains the caller's responsibility. Zero port
    // explicitly clears the reference; no channel format or FX is changed.
    bool assign(ChannelId channel,PortId port) {
        if(!channel||(port&&!find(port)))return false;
        const auto it=std::find_if(refs_.begin(),refs_.end(),[&](const auto& r){return r.channel==channel;});
        if(it!=refs_.end()){
            if(port)it->port=port;
            else refs_.erase(it);
            return true;
        }
        if(!port)return true;
        if(refs_.size()>=maxReferences)return false;
        refs_.push_back({channel,port});return true;
    }
    bool unbind(PortId id) {
        auto* p=edit(id);if(!p)return false;
        p->binding.reset();return true;
    }
    // Explicit operator remap only: proposed physical labels AND ordinal
    // must match the current authorized inventory. A stale device epoch
    // rejects before mutation. Rebinding preserves all channel references.
    bool bind(PortId id,const Binding& proposed,const Inventory& live,std::uint64_t expectedEpoch) {
        auto* p=edit(id);
        if(!p||!expectedEpoch||expectedEpoch!=live.epoch||
           !resolveBinding(p->format,proposed,live))return false;
        p->binding=proposed;return true;
    }
    Resolved resolve(ChannelId channel,const Inventory& live) const noexcept {
        const auto it=std::find_if(refs_.begin(),refs_.end(),[&](const auto& r){return r.channel==channel;});
        if(it==refs_.end())return {};
        const auto* p=find(it->port);
        if(!p)return {Resolution::needsRemap,it->port};
        if(p->binding){
            if(const auto pair=resolveBinding(p->format,*p->binding,live))
                return {Resolution::resolved,p->id,p->format,pair->first,pair->second};
        }
        return {Resolution::needsRemap,p->id,p->format,-1,-1};
    }
private:
    std::vector<Port> ports_;
    std::vector<Reference> refs_;
    Port* edit(PortId id) noexcept {
        const auto it=std::find_if(ports_.begin(),ports_.end(),[&](const auto& p){return p.id==id;});
        return it==ports_.end()?nullptr:&*it;
    }
    static bool validName(const std::string& s) noexcept {
        if(s.empty()||s.size()>192||s.front()==' '||s.back()==' ')return false;
        return std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});
    }
    static unsigned char fold(unsigned char c) noexcept {return c>='A'&&c<='Z'?c+32:c;}
    bool nameUsed(const std::string& name,PortId except) const noexcept {
        return std::any_of(ports_.begin(),ports_.end(),[&](const auto& p){
            return p.id!=except&&p.name.size()==name.size()&&
                std::equal(p.name.begin(),p.name.end(),name.begin(),[](unsigned char a,unsigned char b){return fold(a)==fold(b);});
        });
    }
    static std::optional<std::pair<int,int>> resolveBinding(Format format,const Binding& b,
            const Inventory& live) noexcept {
        if(!live.authorized||!live.epoch||!live.device.valid()||b.device!=live.device||
           live.pins.empty()||live.pins.size()>256)return {};
        // Reject malformed/ambiguous enabled inventory even if the selected
        // pin itself looks valid. No fallback by ordinal or generic label.
        std::bitset<256> seen;
        for(std::size_t i=0;i<live.pins.size();++i){
            const auto& p=live.pins[i];
            if(p.physical<0||p.physical>255||p.callback!=static_cast<int>(i)||
               seen[static_cast<std::size_t>(p.physical)])return {};
            seen.set(static_cast<std::size_t>(p.physical));
        }
        const auto pin=[&](int physical,const std::string& name){
            if(physical<0||name.empty())return -1;
            int found=-1,count=0;
            for(const auto& p:live.pins)if(p.name==name){
                ++count;if(p.physical==physical)found=p.callback;
            }
            return count==1?found:-1;
        };
        const int left=pin(b.left,b.leftName);
        if(left<0)return {};
        if(format==Format::mono){
            if(b.right!=-1||!b.rightName.empty())return {};
            return std::pair<int,int>{left,-1};
        }
        const int right=pin(b.right,b.rightName);
        if(right<0||left==right)return {};
        return std::pair<int,int>{left,right};
    }
};
} // namespace armix::device
