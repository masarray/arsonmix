// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
namespace armix::builtin_fx::vocal_delay {
// Control-only consumer of the existing host tempo estimator. No PCM
// analysis, new clock, timer, memory allocation or session identity here.
struct MusicObservation final{
    std::uint64_t source=0,epoch=0,revision=0,frame=0;
    float bpm=0,confidence=0;
    bool locked=false;
};
class MusicFollow final{
public:
    enum class Status{manual,listening,holding,locked,range};
    void enable(bool enabled) noexcept{enabled_=enabled;hadLock_=false;reset();status_=enabled?Status::listening:Status::manual;}
    bool enabled() const noexcept{return enabled_;}
    Status status() const noexcept{return status_;}
    // A valid lock must persist on advancing observations for 500ms.
    // Hold last accepted timing during silence, device/revision change,
    // low confidence or invalid rhythmic duration. Never switch to a
    // guessed tempo or allow the insert's own channel as the clock.
    bool update(const MusicObservation& o,std::uint64_t own,double now,
                const Parameters& base,Parameters& next) noexcept{
        if(!enabled_)return false;
        const bool good=o.locked&&o.source&&own&&o.source!=own&&o.epoch&&o.revision&&o.frame&&
            std::isfinite(now)&&std::isfinite(o.confidence)&&o.confidence>=.35f&&o.confidence<=1.f&&
            std::isfinite(o.bpm)&&o.bpm>=40&&o.bpm<=240;
        if(!good){reset();status_=hadLock_?Status::holding:Status::listening;return false;}
        const auto q=static_cast<std::uint16_t>(std::lround(o.bpm*4.f));
        if(o.source!=source_||o.epoch!=epoch_||o.revision!=revision_||
           o.frame<frame_||now<lastAdvance_||now-lastAdvance_>1500.||q!=candidate_){
            source_=o.source;epoch_=o.epoch;revision_=o.revision;frame_=o.frame;
            candidate_=q;since_=lastAdvance_=now;status_=hadLock_?Status::holding:Status::listening;return false;
        }
        if(o.frame==frame_)return false;
        frame_=o.frame;lastAdvance_=now;
        if(now-since_<500.)return false;
        auto p=base;
        if(!setTempo(p,double(q)*.25,base.division)){status_=Status::range;return false;}
        hadLock_=true;status_=Status::locked;
        if(p.bpmQuarter==base.bpmQuarter&&p.timeMs==base.timeMs)return false;
        // Native smoothing retimes wet taps only, leaving all other edits intact.
        next=p;return true;
    }
private:
    void reset() noexcept{source_=epoch_=revision_=frame_=0;candidate_=0;since_=lastAdvance_=0;}
    bool enabled_=false,hadLock_=false;
    Status status_=Status::manual;
    std::uint64_t source_=0,epoch_=0,revision_=0,frame_=0;
    std::uint16_t candidate_=0;
    double since_=0,lastAdvance_=0;
};
}
