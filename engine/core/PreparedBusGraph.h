// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelRoutingContract.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <span>
namespace armix::mixer {
// P1C: offline-compiled, no-alloc two-AUX processing graph.
// The physical-port mask and VST lifecycles remain separate native gates.
constexpr int auxBuses=2, routingNodes=stripCount+auxBuses, maxRouteFrames=1024;
enum class RouteKind { none, mono, stereo, strip, aux };
struct RouteNode {
    RouteKind kind=RouteKind::none;
    int a=-1,b=-1,sendTo=-1;
    float sendLevel=0.f,level=1.f;
    bool main=true,mute=false;
};
struct RouteSetup {
    ChannelRoutingConfig channels{};
    std::array<RouteNode,routingNodes> nodes{};
};
enum class RouteError { none,invalidSource,invalidSend,invalidGain,cycle,badDevice };
struct RouteFault {RouteError code=RouteError::none;int node=-1;};
struct PreparedRoutes {
    ChannelRoutingPlan device{};
    std::array<RouteNode,routingNodes> nodes{};
    std::array<int,routingNodes> order{};
    static bool compile(const RouteSetup& src,std::span<const int> active,
                        PreparedRoutes& dst,RouteFault& fault) noexcept {
        fault={};
        RouteSetup cfg=src;
        for(int i=0;i<stripCount;++i){
            auto& c=cfg.channels.stripsAndMaster.strips[i];
            const auto& s=src.nodes[i];
            c.source=s.kind==RouteKind::mono?SourceMode::mono:
                     s.kind==RouteKind::stereo?SourceMode::stereo:SourceMode::disconnected;
            c.inputA=c.source==SourceMode::disconnected?-1:s.a;
            c.inputB=c.source==SourceMode::stereo?s.b:-1;
        }
        ChannelRoutingPlan device;LayoutError deviceError;
        if(!ChannelRoutingPlan::compile(cfg.channels,active,device,deviceError)){
            fault={RouteError::badDevice,deviceError.strip};return false;
        }
        std::array<std::array<bool,routingNodes>,routingNodes> edges{};
        std::array<int,routingNodes> incoming{};
        for(int n=0;n<routingNodes;++n){
            const auto& s=src.nodes[n];
            if(!std::isfinite(s.level)||s.level<0.f||s.level>4.f){
                fault={RouteError::invalidGain,n};return false;
            }
            int from=-1;
            if(s.kind==RouteKind::strip){if(s.a<0||s.a>=stripCount){fault={RouteError::invalidSource,n};return false;}from=s.a;}
            else if(s.kind==RouteKind::aux){if(s.a<0||s.a>=auxBuses){fault={RouteError::invalidSource,n};return false;}from=stripCount+s.a;}
            else if(s.kind==RouteKind::mono||s.kind==RouteKind::stereo){
                if(n>=stripCount){fault={RouteError::invalidSource,n};return false;}
            }else if(s.kind!=RouteKind::none){fault={RouteError::invalidSource,n};return false;}
            if(from>=0&&!edges[from][n]){edges[from][n]=true;incoming[n]++;}
            if(s.sendTo!=-1){
                if(n>=stripCount||s.sendTo<0||s.sendTo>=auxBuses||!std::isfinite(s.sendLevel)||s.sendLevel<0.f||s.sendLevel>4.f){
                    fault={RouteError::invalidSend,n};return false;
                }
                int to=stripCount+s.sendTo;
                if(!edges[n][to]){edges[n][to]=true;incoming[to]++;}
            }
        }
        PreparedRoutes p;p.device=device;p.nodes=src.nodes;
        std::array<bool,routingNodes> done{};
        for(int k=0;k<routingNodes;++k){
            int next=-1;
            for(int i=0;i<routingNodes;++i)if(!done[i]&&incoming[i]==0){next=i;break;}
            if(next<0){fault={RouteError::cycle,-1};return false;}
            p.order[k]=next;done[next]=true;
            for(int i=0;i<routingNodes;++i)if(edges[next][i])incoming[i]--;
        }
        dst=p;return true;
    }
};
class PreparedRouteRuntime {
    using Samples=std::array<float,maxRouteFrames>;
    std::array<Samples,routingNodes> left_{},right_{},addLeft_{},addRight_{};
    Samples masterL_{},masterR_{};
public:
    template<class Insert>
    bool process(const PreparedRoutes& p,const float* const* inputs,int inputCount,
                 float* outL,float* outR,int frames,Insert&& insert) noexcept {
        if(!outL||!outR||frames<=0||frames>maxRouteFrames)return false;
        for(int n=0;n<routingNodes;++n){
            std::fill_n(left_[n].data(),frames,0.f);std::fill_n(right_[n].data(),frames,0.f);
            std::fill_n(addLeft_[n].data(),frames,0.f);std::fill_n(addRight_[n].data(),frames,0.f);
        }
        std::fill_n(masterL_.data(),frames,0.f);std::fill_n(masterR_.data(),frames,0.f);
        for(int k=0;k<routingNodes;++k){
            int n=p.order[k];const auto& x=p.nodes[n];
            float* l=left_[n].data();float* r=right_[n].data();
            if(x.kind==RouteKind::mono||x.kind==RouteKind::stereo)
                p.device.extract(n,inputs,inputCount,l,r,frames);
            else if(x.kind==RouteKind::strip||x.kind==RouteKind::aux){
                int from=x.kind==RouteKind::strip?x.a:stripCount+x.a;
                std::copy_n(left_[from].data(),frames,l);std::copy_n(right_[from].data(),frames,r);
            }
            for(int i=0;i<frames;++i){l[i]+=addLeft_[n][i];r[i]+=addRight_[n][i];}
            insert(n,l,r,frames);
            if(x.mute)continue;
            for(int i=0;i<frames;++i){l[i]*=x.level;r[i]*=x.level;}
            if(x.sendTo>=0){
                int target=stripCount+x.sendTo;
                for(int i=0;i<frames;++i){
                    addLeft_[target][i]+=l[i]*x.sendLevel;
                    addRight_[target][i]+=r[i]*x.sendLevel;
                }
            }
            if(x.main)for(int i=0;i<frames;++i){masterL_[i]+=l[i];masterR_[i]+=r[i];}
        }
        p.device.base.finishMaster(masterL_.data(),masterR_.data(),frames);
        std::copy_n(masterL_.data(),frames,outL);std::copy_n(masterR_.data(),frames,outR);
        return true;
    }
    bool process(const PreparedRoutes& p,const float* const* in,int cnt,float* l,float* r,int n) noexcept {
        return process(p,in,cnt,l,r,n,[](int,float*,float*,int) noexcept {});
    }
};
}
