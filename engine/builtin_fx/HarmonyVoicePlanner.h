// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>

namespace armix::harmony {
// Stable DSP slots for the five singers. Activation order (LOW, MID, HIGH,
// lower support, upper support) is separate from their musical/pan order.
enum class Singer { low, mid, high, lowerSupport, upperSupport };
struct SingerProfile {
    Singer role;
    int minInterval,maxInterval,preferred;
    float pan,presence;
};
inline constexpr std::array<SingerProfile,5> singers{{
    // Harder lateral anchors but still center-safe at Space=0. The HIGH
    // singer gets more relative presence; normalization remains 1/sqrt(N).
    {Singer::low,          -5,-3,-4,-.94f,1.20f},
    {Singer::mid,           3, 5, 4, .00f,.94f},
    {Singer::high,          6, 9, 7, .95f,1.37f},
    {Singer::lowerSupport, -9,-6,-7,-.66f,1.03f},
    {Singer::upperSupport, 10,12,12, .67f,1.18f}
}};
inline constexpr bool singerActive(Singer role,int voices) noexcept {
    return role==Singer::low||role==Singer::high||
           (role==Singer::mid&&voices>=3)||
           (role==Singer::lowerSupport&&voices>=4)||
           (role==Singer::upperSupport&&voices>=5);
}

// Diatonic live fallback. At most four candidates per singer. Disjoint
// interval bands prevent crossing or duplicating the lead or another role.
// Prefer consonant anchors, then retain common tones/limit movement when
// the scale allows it. Music Key supplies a scale, never a chord.
class VoicePlanner final {
public:
    void reset() noexcept { targets_.fill(-1); }

    void plan(int melody,int key,const std::array<int,7>& scale) noexcept {
        for(std::size_t i=0;i<singers.size();++i){
            const auto& profile=singers[i];
            int selected=-1,best=1000;
            for(int interval=profile.minInterval;interval<=profile.maxInterval;++interval){
                const int note=melody+interval;
                const int pitchClass=((note-key)%12+12)%12;
                if(std::find(scale.begin(),scale.end(),pitchClass)==scale.end())
                    continue;
                const int movement=targets_[i]<0?0:std::abs(note-targets_[i]);
                const int dissonance=std::abs(interval)==6?7:
                    (interval==10||interval==11?5:0);
                const int cost=2*std::abs(interval-profile.preferred)+
                               3*std::min(5,movement)+dissonance;
                if(cost<best){best=cost;selected=note;}
            }
            targets_[i]=selected;
        }
    }

    int target(std::size_t voice) const noexcept { return targets_[voice]; }
private:
    std::array<int,singers.size()> targets_{{-1,-1,-1,-1,-1}};
};
} // namespace armix::harmony
