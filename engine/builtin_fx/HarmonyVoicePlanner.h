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

    void plan(int melody,int key,const std::array<int,7>& scale,
              int voices=2) noexcept {
        if(voices>=3){planEnsemble(melody,key,scale);return;}
        planDuo(melody,key,scale);
    }
    // Preserve the accepted two-voice arrangement. Wider ensembles need
    // joint chord membership, not independently chosen scale neighbours.
    void planDuo(int melody,int key,const std::array<int,7>& scale) noexcept {
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
    static int pc(int note,int key) noexcept {return ((note-key)%12+12)%12;}
    // A key is NOT an observed backing-track chord. This is a deterministic
    // diatonic triad fallback containing the melody, not chord recognition.
    // Evaluate at most 7 roots * 5 roles * 4 interval candidates per new
    // accepted note. No allocation, per-sample search, or additional DSP.
    void planEnsemble(int melody,int key,const std::array<int,7>& scale) noexcept {
        const auto previous=targets_;
        planDuo(melody,key,scale);
        const auto fallback=targets_;
        auto bestNotes=fallback;
        int bestCost=100000;
        for(int root=0;root<7;++root){
            const std::array<int,3> chord{{scale[root],scale[(root+2)%7],
                                          scale[(root+4)%7]}};
            const auto member=[&](int note) noexcept {
                return std::find(chord.begin(),chord.end(),pc(note,key))!=chord.end();
            };
            if(!member(melody))continue;
            // Major/minor triads only: a key-only fallback should not
            // invent a diminished chord under an ambiguous melody.
            if((chord[2]-chord[0]+12)%12!=7)continue;
            std::array<int,singers.size()> notes{};
            int score=0;
            bool complete=true;
            for(std::size_t i=0;i<singers.size();++i){
                const auto& role=singers[i];
                int chosen=-1,costBest=10000;
                for(int interval=role.minInterval;interval<=role.maxInterval;++interval){
                    const int note=melody+interval;
                    if(!member(note))continue;
                    const int move=previous[i]<0?0:std::min(7,std::abs(note-previous[i]));
                    const int cost=2*std::abs(interval-role.preferred)+move+
                        (i==0&&note!=fallback[0]?12:0);
                    if(cost<costBest){costBest=cost;chosen=note;}
                }
                if(chosen<0){complete=false;break;}
                notes[i]=chosen;score+=costBest;
            }
            if(!complete||notes[0]-notes[3]<3||notes[2]-notes[1]<3||
               notes[4]-notes[2]<3)continue;
            if(score<bestCost){bestCost=score;bestNotes=notes;}
        }
        targets_=bestNotes;
    }
    std::array<int,singers.size()> targets_{{-1,-1,-1,-1,-1}};
};
} // namespace armix::harmony
