// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PlayerShowTransitionPolicy.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
using P=armix::player::PlayerShowTransitionPolicy;
int main() {
    // Explicit queue wins; manual-selected NEXT wins over default row;
    // sequential only after the operator does not designate another song.
    assert(P::nextIndex(3,8,10,2)==2);
    assert(P::nextIndex(3,8,10,-1)==8);
    assert(P::nextIndex(3,3,10,-1)==4);
    assert(P::nextIndex(9,9,10,-1)==-1);
    assert(P::nextIndex(4,4,10,4)==4); // intentional repeat CURRENT
    assert(P::nextIndex(-1,-1,10,-1)==0);
    assert(P::nextIndex(-1,2,10,-1)==2);
    assert(P::nextIndex(0,0,0,-1)==-1);
    assert(P::nextIndex(0,0,10,999)==1);
    // A 500ms crossfade must start shortly before the end, never at
    // arbitrary mid-song positions and never when time is invalid.
    assert(!P::armBeforeEnd(60.,120.,500,.085));
    assert(!P::armBeforeEnd(119.3,120.,500,.085));
    assert(P::armBeforeEnd(119.5,120.,500,.085));
    assert(P::armBeforeEnd(120.,120.,500,.085));
    assert(!P::armBeforeEnd(121.,120.,500,.085));
    assert(P::armBeforeEnd(119.999,120.,0,.085));
    assert(!P::armBeforeEnd(-1.,120.,500,.085));
    assert(!P::armBeforeEnd(10.,0.,500,.085));
    assert(!P::armBeforeEnd(std::numeric_limits<double>::quiet_NaN(),120.,500,.085));
    assert(!P::armBeforeEnd(10.,std::numeric_limits<double>::infinity(),500,.085));
    assert(!P::armBeforeEnd(10.,120.,3001,.085));
    assert(!P::armBeforeEnd(10.,120.,500,0.));
    // All public transition settings, 44.1/48/96k, simulated 12 Hz
    // display cadence: never request a switch midway through a long song.
    for(int fade: {0,180,500,1500})for(int hz: {44100,48000,96000}) {
        const double duration=180.;
        const double step=static_cast<double>(512)/hz;
        bool hit=false;
        for(double sec=0.;sec<duration;sec+=step) {
            const bool should=P::armBeforeEnd(sec,duration,fade,.085);
            if(should) {
                assert(duration-sec<=fade/1000.0+.085+step);
                hit=true;
            }
        }
        assert(hit);
    }
    // Reordering preserves duplicates and avoids accidental queue loss.
    std::vector<std::string> queue={"song-A","song-B","song-A","song-C"};
    assert(!P::moveQueued(queue,-1,2));
    assert(!P::moveQueued(queue,0,4));
    assert(!P::moveQueued(queue,1,1));
    assert(P::moveQueued(queue,2,0));
    assert((queue==std::vector<std::string>{"song-A","song-A","song-B","song-C"}));
    // A failed decode must NOT consume the queue; commit occurs only on
    // actual native acknowledged track switch and the same queue head.
    assert(!P::commitQueuedFront(queue,std::string("song-C")));
    assert(queue.size()==4);
    assert(P::commitQueuedFront(queue,std::string("song-A")));
    assert(queue.size()==3 && queue.front()=="song-A");
    assert(P::commitQueuedFront(queue,std::string("song-A")));
    assert(queue.size()==2 && queue.front()=="song-B");
    assert(!P::commitQueuedFront(queue,std::string("song-A")));
    assert(P::commitQueuedFront(queue,std::string("song-B")));
    assert(P::commitQueuedFront(queue,std::string("song-C")));
    assert(queue.empty() && !P::commitQueuedFront(queue,std::string("song-C")));
    std::puts("M1/M3 PASS: queued identity, repeat, reorder, commit-on-ACK, pre-end crossfade deadlines");
}
