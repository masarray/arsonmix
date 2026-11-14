// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PlayerShowEditPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdio>
using P=armix::player::PlayerShowEditPolicy;
int main() {
    // Every surviving song ID must continue to point to the same
    // logical song after dragging a setlist row in either direction.
    for(int count=1;count<=32;++count)for(int from=0;from<count;++from)
    for(int to=0;to<count;++to) {
        std::vector<int> songs(count);
        std::iota(songs.begin(),songs.end(),0);
        const auto moved=songs[static_cast<std::size_t>(from)];
        songs.erase(songs.begin()+from);
        songs.insert(songs.begin()+to,moved);
        for(int old=0;old<count;++old) {
            const int remap=P::afterMove(old,from,to,count);
            assert(remap>=0 && remap<count);
            assert(songs[static_cast<std::size_t>(remap)]==old);
        }
    }
    for(int count=1;count<=64;++count)for(int index=0;index<count;++index) {
        for(int old=0;old<count;++old) {
            const int mapped=P::afterRemove(old,index);
            if(old==index)assert(mapped==-1);
            else {
                assert(mapped>=0 && mapped<count-1);
                assert(P::afterInsert(mapped,index)==old);
            }
        }
    }
    assert(P::afterMove(-1,0,1,5)==-1);
    assert(P::afterRemove(-1,0)==-1);
    assert(P::afterInsert(-1,0)==-1);
    assert(P::afterMove(5,-1,3,6)==5);
    std::puts("M1: all stable CURRENT/NEXT/pending reorder + remove/undo mappings pass");
}
