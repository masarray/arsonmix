// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DmxOutputSinkPlan.h"
#include <cassert>
#include <cstdio>
#include <set>
using armix::mixer::DmxOutputSinkPlan;
int main(){
    using P=DmxOutputSinkPlan::Pair;
    std::size_t total=0,split=0,valid=0;
    for(int n=0;n<=8;++n)for(int l=-1;l<=n;++l)for(int r=-1;r<=n;++r)
      for(int ml=-1;ml<=n;++ml)for(int mr=-1;mr<=n;++mr)for(bool enabled:{false,true}){
        const DmxOutputSinkPlan p{n,{l,r},{ml,mr},enabled};++total;
        const bool expectedSplit=n==2&&l==-1&&r==-1&&enabled&&
            ((ml==0&&mr==1)||(ml==1&&mr==0));
        const auto legal=[&](int a,int b){return a>=0&&b>=0&&a<n&&b<n&&a!=b;};
        const bool ordinary=n>=2&&legal(l,r)&&
            ((!enabled&&ml==-1&&mr==-1)||(enabled&&legal(ml,mr)&&
            ml!=l&&ml!=r&&mr!=l&&mr!=r));
        assert(p.secondaryMain()==expectedSplit);
        assert(p.valid()==(ordinary||expectedSplit));
        if(!p.valid())continue;
        ++valid;if(p.secondaryMain())++split;
        const auto pins=p.reserved();std::set<int> unique;
        for(std::size_t i=0;i<p.reservedCount();++i){
            assert(pins[i]>=0&&pins[i]<n);assert(unique.insert(pins[i]).second);
        }
        if(expectedSplit){assert(p.main==P({-1,-1}));assert(p.reservedCount()==2);
            assert(pins[0]==ml&&pins[1]==mr);}
    }
    assert(split==2);
    std::printf("DMX_SINK_PLAN_PASS %zu cases, %zu valid, %zu exclusive split maps\n",total,valid,split);
}
