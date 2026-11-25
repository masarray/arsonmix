// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeSession.h"
#include <array>
#include <algorithm>
#include <limits>
namespace armix::builtin_fx::vocal_delay {
struct SingerPreset {const char* name;float time,feedback,echo,duck,hp,lp;StereoMode mode;};
inline constexpr std::array<SingerPreset,8> singerPresets{{
 {"Live Vocal",320,.30f,.22f,.20f,150,5500,StereoMode::stereo},
 {"Classic Slap",100,0,.20f,.10f,120,6500,StereoMode::mono},
 {"Pop Echo",280,.35f,.22f,.25f,150,6000,StereoMode::stereo},
 {"Ballad",520,.45f,.25f,.30f,170,5000,StereoMode::stereo},
 {"Concert",650,.50f,.25f,.35f,180,5000,StereoMode::stereo},
 {"Long Echo",850,.60f,.22f,.40f,200,4500,StereoMode::stereo},
 {"Ping Pong",380,.40f,.24f,.25f,150,6000,StereoMode::pingPong},
 {"Dreamy",1100,.65f,.28f,.45f,220,4200,StereoMode::stereo}
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
        auto ordered=intervals_;const auto n=count_-1;std::sort(ordered.begin(),ordered.begin()+n);
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
