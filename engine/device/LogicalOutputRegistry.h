// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace armix::device {
// CONTROL-only model, no PCM, device opening or output ARM permission.
// MAIN and MONITOR are exclusive. External channels can intentionally
// share ONE logical output, never two aliases claiming the same pair.
class LogicalOutputRegistry final {
public:
    using PortId=std::uint64_t;
    using ChannelId=std::uint64_t;
    using Pair=std::array<int,2>;
    enum class Role { main, monitor, external };
    enum class State { needsRemap, reserved, resolved };
    struct Device {std::string backend,playback;bool operator==(const Device&) const=default;};
    struct Pin {int physical=-1,callback=-1;std::string name;};
    struct Inventory {
        Device device;
        std::uint64_t epoch=0;
        std::span<const Pin> pins;
        // Direct routes NOT replaced by the candidate transaction, physical IDs.
        std::span<const int> reserved;
        bool authorized=false;
    };
    struct Binding {Device device;int left=-1,right=-1;std::string leftName,rightName;};
    struct Port {PortId id=0;std::string name;std::optional<Binding> binding;};
    struct Reference {ChannelId channel=0;PortId port=0;Role role=Role::external;};
    struct Destination {Reference reference;State state=State::needsRemap;Pair callback{-1,-1};};
    struct Plan {
        bool inventoryVerified=false;
        std::vector<Destination> destinations;
        bool complete() const noexcept {
            return inventoryVerified&&std::all_of(destinations.begin(),destinations.end(),
                [](const auto& d){return d.state==State::resolved;});
        }
    };
    static constexpr std::size_t maxPorts=128,maxReferences=512;
    const auto& ports() const noexcept{return ports_;}
    const auto& references() const noexcept{return refs_;}
    const Port* find(PortId id) const noexcept {
        const auto it=std::find_if(ports_.begin(),ports_.end(),[&](const auto& p){return p.id==id;});
        return it==ports_.end()?nullptr:&*it;
    }
    bool add(PortId id,std::string name){
        if(!id||find(id)||ports_.size()>=maxPorts||!validName(name)||nameUsed(name,0))return false;
        ports_.push_back({id,std::move(name),{}});return true;
    }
    bool rename(PortId id,std::string name){
        auto* p=edit(id);if(!p||!validName(name)||nameUsed(name,id))return false;
        p->name=std::move(name);return true;
    }
    bool erase(PortId id){
        if(!find(id)||std::any_of(refs_.begin(),refs_.end(),[&](const auto& ref){return ref.port==id;}))return false;
        std::erase_if(ports_,[&](const auto& p){return p.id==id;});return true;
    }
    // Real roster/role attestation remains the caller's responsibility.
    bool assign(ChannelId channel,PortId port,Role role){
        if(!channel||(role!=Role::main&&role!=Role::monitor&&role!=Role::external))return false;
        auto it=std::find_if(refs_.begin(),refs_.end(),[&](const auto& ref){return ref.channel==channel;});
        if(it!=refs_.end()&&it->role!=role)return false;
        if(!port){if(it!=refs_.end())refs_.erase(it);return true;}
        if(!find(port))return false;
        for(const auto& ref:refs_){
            if(ref.channel==channel)continue;
            if(role!=Role::external&&ref.role==role)return false;
            if(ref.port==port&&(role!=Role::external||ref.role!=Role::external))return false;
        }
        if(it!=refs_.end()){it->port=port;return true;}
        if(refs_.size()>=maxReferences)return false;
        refs_.push_back({channel,port,role});return true;
    }
    // Durable intent, not proof of live output identity.
    bool restoreIntent(PortId id,const Binding& b){
        auto* p=edit(id);if(!p||!validBinding(b))return false;
        for(const auto& other:ports_)
            if(other.id!=id&&other.binding&&other.binding->device==b.device&&
               other.binding->left==b.left)return false; // conventional disjoint pairs
        p->binding=b;return true;
    }
    bool bind(PortId id,const Binding& b,const Inventory& live,std::uint64_t expectedEpoch,bool confirmed){
        if(!confirmed||!expectedEpoch||expectedEpoch!=live.epoch||!validInventory(live)||
           !resolve(b,live)||blocked(b,live.reserved))return false;
        return restoreIntent(id,b);
    }
    bool unbind(PortId id){auto* p=edit(id);if(!p)return false;p->binding.reset();return true;}
    // A complete plan is necessary, not sufficient, for future publication.
    // Unavailable or conflicting ports never redirect to another amplifier.
    Plan prepare(const Inventory& live) const{
        Plan out;out.inventoryVerified=validInventory(live);
        out.destinations.reserve(refs_.size());
        for(const auto& ref:refs_){
            Destination d{ref};const auto* p=find(ref.port);
            if(out.inventoryVerified&&p&&p->binding){
                if(const auto pair=resolve(*p->binding,live)){
                    if(blocked(*p->binding,live.reserved))d.state=State::reserved;
                    else {d.state=State::resolved;d.callback=*pair;}
                }
            }
            out.destinations.push_back(d);
        }
        return out;
    }
private:
    std::vector<Port> ports_;
    std::vector<Reference> refs_;
    Port* edit(PortId id) noexcept{
        auto it=std::find_if(ports_.begin(),ports_.end(),[&](const auto& p){return p.id==id;});
        return it==ports_.end()?nullptr:&*it;
    }
    static bool validText(const std::string& s,std::size_t max) noexcept{
        return !s.empty()&&s.size()<=max&&std::none_of(s.begin(),s.end(),
            [](unsigned char c){return c<32||c==127;});
    }
    static bool validName(const std::string& s) noexcept{return validText(s,192)&&s.front()!=' '&&s.back()!=' ';}
    static unsigned char fold(unsigned char c) noexcept{return c>='A'&&c<='Z'?c+32:c;}
    bool nameUsed(const std::string& name,PortId except) const noexcept{
        return std::any_of(ports_.begin(),ports_.end(),[&](const auto& p){
            return p.id!=except&&p.name.size()==name.size()&&std::equal(p.name.begin(),p.name.end(),name.begin(),
                [](unsigned char a,unsigned char b){return fold(a)==fold(b);});
        });
    }
    static bool validBinding(const Binding& b) noexcept{
        // Match existing ASIO stereo output owner. No mono/odd pair yet.
        return b.device.backend=="ASIO"&&validText(b.device.playback,256)&&
            b.left>=0&&b.left<=254&&b.left%2==0&&b.right==b.left+1&&
            validText(b.leftName,256)&&validText(b.rightName,256)&&b.leftName!=b.rightName;
    }
    static bool blocked(const Binding& b,std::span<const int> reserved) noexcept{
        return std::find(reserved.begin(),reserved.end(),b.left)!=reserved.end()||
               std::find(reserved.begin(),reserved.end(),b.right)!=reserved.end();
    }
    static bool validInventory(const Inventory& live) noexcept{
        if(!live.authorized||!live.epoch||live.device.backend!="ASIO"||
           !validText(live.device.playback,256)||live.pins.size()<2||live.pins.size()>256)return false;
        std::bitset<256> seen;
        for(std::size_t i=0;i<live.pins.size();++i){
            const auto& p=live.pins[i];
            if(p.physical<0||p.physical>255||p.callback!=static_cast<int>(i)||
               seen[static_cast<std::size_t>(p.physical)])return false;
            seen.set(static_cast<std::size_t>(p.physical));
        }
        return std::all_of(live.reserved.begin(),live.reserved.end(),[](int p){return p>=0&&p<=255;});
    }
    static std::optional<Pair> resolve(const Binding& b,const Inventory& live) noexcept{
        if(!validBinding(b)||b.device!=live.device)return {};
        const auto pin=[&](int physical,const std::string& name){
            int found=-1,count=0;
            for(const auto& p:live.pins)if(p.name==name){++count;if(p.physical==physical)found=p.callback;}
            return count==1?found:-1;
        };
        const int l=pin(b.left,b.leftName),r=pin(b.right,b.rightName);
        if(l<0||r<0||l==r)return {};
        return Pair{l,r};
    }
};
} // namespace armix::device
