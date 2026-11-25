// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeSession.h"
#include <array>
#include <algorithm>
#include <limits>
namespace armix::builtin_fx::vocal_delay {
struct SingerPreset {const char* name;float time,feedback,echo,duck,hp,lp;StereoMode mode;const char* description;};
inline constexpr std::array<SingerPreset,10> singerPresets{{
 {"Live Vocal",320,.30f,.22f,.20f,150,5500,StereoMode::stereo,"An easy everyday echo"},
 {"Classic Slap",100,0,.20f,.10f,120,6500,StereoMode::mono,"Short and thick"},
 {"Pop Echo",280,.35f,.22f,.25f,150,6000,StereoMode::stereo,"Bright rhythmic repeats"},
 {"Ballad",520,.45f,.25f,.30f,170,5000,StereoMode::stereo,"Soft, spacious repeats"},
 {"Concert",650,.50f,.25f,.35f,180,5000,StereoMode::stereo,"Longer stage-style echo"},
 {"Long Echo",850,.60f,.22f,.40f,200,4500,StereoMode::stereo,"Longer trailing phrases"},
 {"Ping Pong",380,.40f,.24f,.25f,150,6000,StereoMode::pingPong,"Alternating left and right"},
 {"Dreamy",1100,.65f,.28f,.45f,220,4200,StereoMode::stereo,"Soft and atmospheric"},
 {"Studio Clean",240,.24f,.18f,.16f,90,11000,StereoMode::stereo,"Clear, bright echoes"},
 {"Warm Vocal",360,.34f,.23f,.28f,180,4000,StereoMode::stereo,"Mellow, rounded repeats"}
}};
inline Parameters presetSettings(std::size_t index,bool enabled=true) noexcept{
    Parameters p{};p.enabled=enabled;if(index>=singerPresets.size())return p;
    const auto& v=singerPresets[index];p.timeMs=v.time;p.feedback=v.feedback;
    p.echo=v.echo;p.duck=v.duck;p.highPassHz=v.hp;p.lowPassHz=v.lp;p.mode=v.mode;return p;
}
inline bool sameParameters(const Parameters& a,const Parameters& b) noexcept{
    return a.timeMs==b.timeMs&&a.feedback==b.feedback&&a.echo==b.echo&&
       a.highPassHz==b.highPassHz&&a.lowPassHz==b.lowPassHz&&a.duck==b.duck&&
       a.duckAttackMs==b.duckAttackMs&&a.duckReleaseMs==b.duckReleaseMs&&
       a.mode==b.mode&&a.enabled==b.enabled;
}
// A style describes voicing, not the user's three performance knobs.
// Exact expert edits make the style Custom; editing Time/Repeats/Level
// keeps the selected style identifiable even after state restore.
inline bool sameVoicing(const Parameters& a,const Parameters& b) noexcept{
    return a.highPassHz==b.highPassHz&&a.lowPassHz==b.lowPassHz&&a.duck==b.duck&&
        a.duckAttackMs==b.duckAttackMs&&a.duckReleaseMs==b.duckReleaseMs&&a.mode==b.mode;
}
inline int styleIndex(const Parameters& p) noexcept{
    for(std::size_t i=0;i<singerPresets.size();++i)
        if(sameVoicing(p,presetSettings(i,p.enabled)))return int(i);
    return -1;
}
// Explicit one-touch CONTROL-only voicing. No microphone analysis claim,
// no gain boost to dry, no modification of user's rhythmic/level intent.
// Longer/more intense echoes get more low-end cleanup and ducking;
// short low-feedback echoes stay open and articulate. Expert can undo
// every mapped field. Deterministic and idempotent.
inline Parameters smartVoicing(const Parameters& p) noexcept{
    if(!valid(p))return p;
    auto q=p;
    const float load=std::clamp(.45f*(p.feedback/.9f)+.35f*p.echo+
        .20f*std::min(p.timeMs/1200.f,1.f),0.f,1.f);
    q.highPassHz=std::round(100.f+140.f*load);
    q.lowPassHz=std::round(8500.f-4500.f*load);
    q.duck=.12f+.55f*load;
    q.duckAttackMs=8.f+7.f*load;
    q.duckReleaseMs=100.f+180.f*load;
    return q;
}
inline constexpr std::array<double,7> beatMultipliers{{.25,.5,.75,1,1.5,2,4}};
inline bool beatTime(double bpm,std::size_t division,float& ms) noexcept{
    if(!std::isfinite(bpm)||bpm<40||bpm>240||division>=beatMultipliers.size())return false;
    const auto t=60000.*beatMultipliers[division]/bpm;
    if(t<20||t>4000)return false; // do not silently truncate musical value
    ms=float(t);return true;
}
class TapTempo final{
public:
    // CONTROL only, monotonic milliseconds. Reject irregular sequences.
    bool tap(double now,double& bpm) noexcept{
        if(!std::isfinite(now))return false;
        if(!count_){last_=now;count_=1;return false;}
        const auto dt=now-last_;last_=now;
        if(dt<250||dt>1500){count_=1;return false;}
        if(count_<=4){intervals_[count_-1]=dt;++count_;}
        else{for(std::size_t i=1;i<4;++i)intervals_[i-1]=intervals_[i];intervals_[3]=dt;}
        if(count_<3)return false;
        auto ordered=intervals_;const auto n=std::min<std::size_t>(count_-1,ordered.size());
        // Four taps maximum; explicit bounded insertion sort avoids generic
        // std::sort's GCC 15 threshold warning on a four-element array.
        for(std::size_t i=1;i<n;++i){
            const auto value=ordered[i];auto j=i;
            while(j>0&&ordered[j-1]>value){ordered[j]=ordered[j-1];--j;}
            ordered[j]=value;
        }
        const double median=n%2?ordered[n/2]:(ordered[n/2-1]+ordered[n/2])*.5;
        if(ordered[n-1]-ordered[0]>median*.20)return false;
        bpm=60000./median;return true;
    }
    void reset() noexcept{count_=0;last_=0;}
private:std::array<double,4> intervals_{};std::size_t count_=0;double last_=0;
};
// CONTROL-only native editor intent owner. No callback or browser processing.
// One outstanding edit; newer desired settings survive delayed DSP receipts.
class SingerControl final{
public:
    void reset() noexcept{lease_={};flight_=0;dirty_=false;fault_=false;desired_={};}
    bool attach(const Snapshot& s) noexcept{
        if(lease_==s.lease)return s.lease.valid();
        if(!s.lease.valid()||!s.quiescent)return false;
        reset();lease_=s.lease;desired_=s.accepted;return true;
    }
    bool offer(const Snapshot& s,const Parameters& p) noexcept{
        if(!attach(s)||!valid(p)||fault_)return false;
        desired_=p;dirty_=true;return true;
    }
    void service(NativeSession& session) noexcept{
        const auto s=session.acceptedSnapshot();
        if(!attach(s))return;
        if(flight_){
            Receipt r{};if(!session.poll(r))return;
            if(!(r.lease==lease_)||r.revision!=flight_||!r.applied){fault_=true;flight_=0;return;}
            flight_=0;
        }
        if(fault_||!dirty_)return;
        const auto latest=session.acceptedSnapshot();
        if(!latest.quiescent)return;
        if(sameParameters(latest.accepted,desired_)){dirty_=false;return;}
        if(latest.lastSubmitted==std::numeric_limits<std::uint32_t>::max()){fault_=true;return;}
        const auto revision=latest.lastSubmitted+1;
        const auto result=session.submit({lease_,revision,desired_});
        if(result==Submit::queued){flight_=revision;dirty_=false;}
        else if(result!=Submit::busy)fault_=true;
    }
    bool pending() const noexcept{return flight_||dirty_;}
    bool fault() const noexcept{return fault_;}
    Parameters display(const Snapshot& s) const noexcept{
        return lease_==s.lease&&(pending()||fault_)?desired_:s.accepted;
    }
private:Lease lease_{};Parameters desired_{};std::uint32_t flight_=0;bool dirty_=false,fault_=false;
};
} // namespace armix::builtin_fx::vocal_delay
