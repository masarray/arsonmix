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
    // Distinct A/B decoder instances can both report generation 2.
    // The logical Auto Next gate must rearm on actual transition ACK,
    // not an XOR of generation/current/selected indices.
    armix::player::PlayerAutoAdvanceGate autoNext;
    assert(!autoNext.claimed());
    for(int pass=0;pass<7;++pass) {
        assert(autoNext.claim());  // enqueue next repeat
        assert(autoNext.claimed());
        assert(!autoNext.claim()); // pending/failure does not spin
        // Real native ACK: NEXT is now CURRENT and may use the very
        // same file, index and per-decoder generation as the last pass.
        autoNext.rearm();
    }
    assert(autoNext.claim());
    assert(!autoNext.claim()); // error remains latched
    autoNext.rearm();          // explicit operator retry
    assert(autoNext.claim());
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
    // A source timeline is NOT the same as output time when tempo differs.
    // At 120% the 500ms fade needs 600ms of source media; at 80% it
    // needs only 400ms. Both retain an 85ms timer cushion.
    assert(P::armBeforeEnd(119.45,120.,500,.085,120));
    assert(!P::armBeforeEnd(119.45,120.,500,.085,80));
    assert(P::armBeforeEnd(119.55,120.,500,.085,80));
    // 120% threshold is (0.500+0.085)*1.2=0.702 media seconds.
    assert(!P::armBeforeEnd(119.2,120.,500,.085,120));
    assert(!P::armBeforeEnd(119.,120.,500,.085,75));
    assert(!P::armBeforeEnd(119.,120.,500,.085,125));
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
    // Loop A-B is only allowed for real segments, with enough source
    // material on both sides of the selected equal-power overlap.
    assert(P::validLoop(12.,16.,180));
    assert(P::validLoop(0.,2.,1500));
    assert(P::validLoop(0.,1.5,0));
    assert(!P::validLoop(2.,2.,180));
    assert(!P::validLoop(3.,2.,180));
    assert(!P::validLoop(0.,1.,180));
    assert(!P::validLoop(0.,1.5,1500));
    assert(!P::validLoop(0.,4.,3001));
    assert(!P::validLoop(-1.,4.,500));
    assert(!P::validLoop(0.,std::numeric_limits<double>::quiet_NaN(),180));
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
