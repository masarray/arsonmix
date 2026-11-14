// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

// Show transport decisions only; deterministic and allocation-free on
// inspection paths. Never owns JUCE, an audio device, or a DSP callback.
namespace armix::player {
// One outstanding Auto Next attempt per logical playback segment. A/B
// decoders may share numeric generations, so generation XOR is not a valid
// identity for queued repeats. Only a successful native ACK or explicit
// operator gesture rearms the next segment; errors do not busy-loop.
class PlayerAutoAdvanceGate final {
public:
    bool claim() noexcept {
        if(claimed_)return false;
        claimed_=true;
        return true;
    }
    void rearm() noexcept {claimed_=false;}
    bool claimed() const noexcept {return claimed_;}
private:
    bool claimed_=false; // CONTROL thread only; never touched by RT
};
// CONTROL-thread queue revision fence. Duplicates share a path but not a
// position. A stale WebView drag must never reorder a DIFFERENT entry after
// Auto Next ACK, setlist switch or another operator's queue mutation.
class PlayerQueueEditEpoch final {
public:
    int revision() const noexcept {return revision_;}
    void changed() noexcept {
        revision_=revision_==2147483647?1:revision_+1;
    }
    bool accepts(int expected,bool transitionPending) const noexcept {
        return expected==revision_ && !transitionPending;
    }
private:
    int revision_=1; // CONTROL only; never used on realtime callback
};
struct PlayerShowTransitionPolicy final {
    // Manual selection/explicit queue always wins over sequential Auto Next.
    // queuedIndex=-1 when there is no valid queued song.
    static int nextIndex(int current,int selected,int count,
                         int queuedIndex) noexcept {
        if(count<=0)return -1;
        if(queuedIndex>=0 && queuedIndex<count)return queuedIndex;
        if(selected>=0 && selected<count && selected!=current)
            return selected;
        if(current<0)return selected>=0 && selected<count?selected:0;
        return current+1<count?current+1:-1;
    }
    // The control timer checks at ~12 Hz. Start at fade duration + one
    // control tick before END, not only after EOF. Reserve enough frames
    // for the equal-power blend without forcing a second mixer callback.
    // A late timer check still accepts <=0 remaining for a safe next-song
    // fade-in, and a zero-duration media never auto-transitions.
    static bool armBeforeEnd(double seconds,double duration,
                             int fadeMilliseconds,double tickSeconds,
                             int tempoPercent=100) noexcept {
        if(!std::isfinite(seconds)||!std::isfinite(duration)||
           !std::isfinite(tickSeconds)||duration<=0. ||
           seconds<0. || fadeMilliseconds<0 ||
           fadeMilliseconds>3000 || tickSeconds<=0. ||
           tickSeconds>0.5 || tempoPercent<80 || tempoPercent>120)
            return false;
        const double remaining=duration-seconds;
        // The UI clock counts ORIGINAL MEDIA seconds. One real second
        // consumes tempo/100 source seconds. Ignore this and a 120% song
        // runs out before its requested fade can finish.
        const double speed=static_cast<double>(tempoPercent)/100.;
        const double early=(static_cast<double>(fadeMilliseconds)/1000.+
                            tickSeconds)*speed;
        return remaining<=early && remaining>=-tickSeconds*speed;
    }
    // Loop A-B is a rehearsal-only opt-in. Never attempt microscopic
    // segments or a fade longer than the segment: require a real period of
    // audible music and a bounded overlap. CUT uses a 40ms de-click loop.
    static bool validLoop(double cueIn,double cueOut,
                          int fadeMilliseconds) noexcept {
        if(!std::isfinite(cueIn)||!std::isfinite(cueOut)||
           cueIn<0. || cueOut<=cueIn || cueOut>86400. ||
           fadeMilliseconds<0 || fadeMilliseconds>3000)return false;
        const double overlap=std::max(40,fadeMilliseconds)/1000.;
        return cueOut-cueIn>=std::max(1.5,overlap+0.35);
    }
    // A queued entry is a logical song ID/path, not a whole-file deletion.
    // Preserve duplicates and current order; UI sends source/target indices.
    template<class T>
    static bool moveQueued(std::vector<T>& queue,int from,int to) {
        const int count=static_cast<int>(queue.size());
        if(from<0 || to<0 || from>=count || to>=count ||
           from==to)return false;
        auto item=std::move(queue[static_cast<std::size_t>(from)]);
        queue.erase(queue.begin()+from);
        queue.insert(queue.begin()+to,std::move(item));
        return true;
    }
    template<class T>
    static bool commitQueuedFront(std::vector<T>& queue,
                                  const T& successfullyStarted) {
        // The queue must not be consumed when decoder load or routing fails,
        // or when another operator has since changed the head.
        if(queue.empty() || queue.front()!=successfullyStarted)return false;
        queue.erase(queue.begin());
        return true;
    }
};
} // namespace armix::player
