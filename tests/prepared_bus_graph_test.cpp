// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/core/PreparedBusGraph.h"
#include <array>
#include <cassert>
#include <cmath>
using namespace armix::mixer;
static bool eq(float a,float b){return std::abs(a-b)<1.e-4f;}
int main(){
    RouteSetup setup;
    constexpr std::array ports{5,2,7};
    setup.nodes[0]={RouteKind::mono,2,-1,0,.25f,1.f,true,false};
    setup.nodes[1]={RouteKind::stereo,5,7};
    setup.nodes[2]={RouteKind::strip,0};setup.nodes[2].main=false;
    PreparedRoutes plan;RouteFault fault;
    assert(PreparedRoutes::compile(setup,ports,plan,fault));
    std::array<float,4> h5{2,2,2,2},h2{1,1,1,1},h7{3,3,3,3},l{},r{};
    const float* physical[]{h5.data(),h2.data(),h7.data()};
    PreparedRouteRuntime engine;
    assert(engine.process(plan,physical,3,l.data(),r.data(),4));
    assert(eq(l[0],3.25f)&&eq(r[0],4.25f));
    assert(engine.process(plan,physical,3,l.data(),r.data(),4,
        [](int bus,float* a,float* b,int n) noexcept {
            if(bus==stripCount)for(int i=0;i<n;++i){a[i]*=2;b[i]*=2;}
        }));
    assert(eq(l[0],3.5f)&&eq(r[0],4.5f));
    auto saved=plan;
    setup.nodes[0].kind=RouteKind::aux;setup.nodes[0].a=0;
    assert(!PreparedRoutes::compile(setup,ports,plan,fault));
    assert(fault.code==RouteError::cycle);
    assert(plan.nodes[0].kind==saved.nodes[0].kind);
    setup.nodes[0].kind=RouteKind::mono;setup.nodes[0].a=2;
    setup.nodes[1].a=6;
    assert(!PreparedRoutes::compile(setup,ports,plan,fault));
    assert(fault.code==RouteError::badDevice);
    setup.nodes[1].a=5;
    setup.nodes[0].sendTo=2;
    assert(!PreparedRoutes::compile(setup,ports,plan,fault));
    assert(fault.code==RouteError::invalidSend);
    setup.nodes[0].sendTo=0;
    assert(PreparedRoutes::compile(setup,ports,plan,fault));
    assert(!engine.process(plan,physical,3,l.data(),r.data(),maxRouteFrames+1));
    assert(engine.process(plan,physical,3,l.data(),r.data(),4));
    assert(eq(l[0],3.25f));
}
