// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DeEzzerCore.h"
#include "DeEzzerMonitor.h"
#include "../FxSpsc.h"
#include <limits>
#include <type_traits>
namespace armix::builtin_fx::de_ezzer {
enum class Audition { Normal, Detect, Delta };
enum class AckCode { Applied, WrongIdentity, StaleSequence, Invalid };
struct Identity { std::uint64_t instance=0,epoch=0; bool operator==(const Identity&) const noexcept=default; };
struct Command { Identity id; std::uint64_t sequence=0; Settings settings; Audition audition=Audition::Normal; };
struct Snapshot {
    Identity id; std::uint64_t appliedSequence=0,frames=0;
    Settings settings; Audition audition=Audition::Normal;
    float gainReductionDb=0,lowerWeight=.5f; std::uint32_t droppedTelemetry=0;
    Observation observation{};
};
struct Ack { Identity requested; std::uint64_t sequence=0; AckCode code=AckCode::Invalid; Snapshot actual; };
static_assert(std::is_trivially_copyable_v<Command>&&std::is_trivially_copyable_v<Ack>&&std::is_trivially_copyable_v<Snapshot>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
// One control producer/consumer, one audio owner. All lifecycle operations require
// BOTH owners quiescent. No public accessor reads mutable audio-owned state.
class Control final {
public:
    static constexpr int commandCapacity=32,ackCapacity=32,telemetryCapacity=8,maxCommandsPerBlock=8,maxFrames=4096;
    bool prepare(Identity id,double rate,int channels,Settings settings={}) noexcept {
        if(!id.instance||!id.epoch||id==id_)return false;
        Core candidate;if(!candidate.setSettings(settings)||!candidate.prepare(rate,channels))return false;
        Command cmd;Ack ack;Snapshot snap;
        while(commands_.pop(cmd)){}while(acks_.pop(ack)){}while(telemetry_.pop(snap)){}
        if(!observer_.prepare(rate,channels))return false;
        analysisRequested_.store(false,std::memory_order_relaxed);analysisRemaining_=0;analysisLease_=static_cast<int>(rate*.5);
        core_=candidate;id_=id;channels_=channels;lastSeen_=applied_=frames_=0;drops_=0;
        audition_=Audition::Normal;deltaMix_=detectMix_=0;lease_=0;
        leaseFrames_=static_cast<int>(rate*3);fadeFrames_=std::max(1,static_cast<int>(rate*.005));
        step_=1./fadeFrames_;telemetryPeriod_=static_cast<int>(rate/30);untilTelemetry_=0;
        return true;
    }
    // false means not enqueued; caller must not treat it as a successful edit.
    bool submit(const Command& cmd) noexcept {return commands_.push(cmd);}
    bool pollAck(Ack& ack) noexcept {return acks_.pop(ack);}
    bool pollTelemetry(Snapshot& snap) noexcept {return telemetry_.pop(snap);}
    bool process(float* l,float* r,int n) noexcept {
        if(!channels_||!l||n<=0||n>maxFrames||(channels_==2&&(!r||l==r)))return false;
        consumeRealtimeEdits();
        return processAudio(l,r,n);
    }
    // Existing mixer hook invokes this once before its rack bypass fast path.
    void consumeRealtimeEdits() noexcept {
        if(!channels_)return;
        // Reserve ACK capacity BEFORE consuming a command; never silently lose ACKs.
        for(int i=0;i<maxCommandsPerBlock&&acks_.canPush();++i){
            Command cmd;if(!commands_.pop(cmd))break;
            const auto code=apply(cmd);(void)acks_.push({cmd.id,cmd.sequence,code,snapshot()});
        }
    }
    // CONTROL renews while an exact-lease editor polls; no persistent toggle.
    void requestAnalysis() noexcept {analysisRequested_.store(true,std::memory_order_release);}
    bool processAudio(float* l,float* r,int n) noexcept {
        if(!channels_||!l||n<=0||n>maxFrames||(channels_==2&&(!r||l==r)))return false;
        renewAnalysis();
        if(audition_==Audition::Normal&&deltaMix_==0&&detectMix_==0){
            if(analysisRemaining_>0){if(!observer_.render(core_,l,channels_==2?r:nullptr,n))return false;}
            else core_.process(l,r,n);
        }else {analysisRemaining_=0;monitor(l,r,n);}
        finishBlock(n);return true;
    }
    // Future graph-authorized operator destination. Caller must supply separate
    // PCM buffers, and must not call processAudio a second time on this block.
    bool processAudioWithCue(float* l,float* r,int n,CueMode mode,float* cueL,float* cueR) noexcept {
        if(!channels_||!cueL||audition_!=Audition::Normal||deltaMix_!=0||detectMix_!=0)return false;
        if(analysisRemaining_<=0){observer_.reset();core_.resetEssTap();}
        if(!observer_.render(core_,l,channels_==2?r:nullptr,n,mode,cueL,cueR))return false;
        analysisRemaining_=analysisLease_;finishBlock(n);return true;
    }
    // Both owners stopped: only for host prepare/restart, never UI polling.
    Snapshot quiescentSnapshot() const noexcept {return snapshot();}
private:
    void renewAnalysis() noexcept {
        if(analysisRequested_.load(std::memory_order_acquire)&&analysisRequested_.exchange(false,std::memory_order_acq_rel)){
            if(analysisRemaining_<=0){observer_.reset();core_.resetEssTap();}
            analysisRemaining_=analysisLease_;
        }
    }
    void finishBlock(int n) noexcept {
        analysisRemaining_=std::max(0,analysisRemaining_-n);
        frames_+=static_cast<unsigned>(n);
        untilTelemetry_-=n;
        if(untilTelemetry_<=0){
            if(!telemetry_.push(snapshot())&&drops_!=std::numeric_limits<std::uint32_t>::max())++drops_;
            // One snapshot maximum per callback; no catch-up bursts.
            untilTelemetry_=telemetryPeriod_;
        }
    }
    AckCode apply(const Command& cmd) noexcept {
        if(cmd.id!=id_)return AckCode::WrongIdentity;
        if(cmd.sequence==0||cmd.sequence<=lastSeen_)return AckCode::StaleSequence;
        lastSeen_=cmd.sequence;
        if(cmd.audition!=Audition::Normal&&cmd.audition!=Audition::Detect&&cmd.audition!=Audition::Delta)return AckCode::Invalid;
        if(!cmd.settings.enabled&&cmd.audition!=Audition::Normal)return AckCode::Invalid;
        if(!core_.setSettings(cmd.settings))return AckCode::Invalid;
        applied_=cmd.sequence;audition_=cmd.audition;lease_=audition_==Audition::Normal?0:leaseFrames_;
        return AckCode::Applied;
    }
    Snapshot snapshot() const noexcept {return {id_,applied_,frames_,core_.settings(),audition_,core_.bandReductionDb(),core_.focusLowerWeight(),drops_,analysisRemaining_>0?observer_.read():Observation{}};}
    static double clean(float x) noexcept {return std::isfinite(x)?static_cast<double>(x):0.;}
    static float finiteFloat(double x) noexcept {constexpr double m=std::numeric_limits<float>::max();return static_cast<float>(std::clamp(x,-m,m));}
    void monitor(float* l,float* r,int n) noexcept {
        // Fixed scratch; no heap, no additional buffering delay. Normal fast path avoids copies.
        constexpr int chunk=128;
        for(int pos=0;pos<n;){
            const int count=std::min(chunk,n-pos);
            std::array<float,chunk> dryL{},dryR{},detectL{},detectR{};
            std::copy_n(l+pos,count,dryL.data());if(channels_==2)std::copy_n(r+pos,count,dryR.data());
            if(audition_==Audition::Detect||detectMix_>0)core_.processWithDetect(l+pos,channels_==2?r+pos:nullptr,count,detectL.data(),detectR.data());
            else core_.process(l+pos,channels_==2?r+pos:nullptr,count);
            for(int i=0;i<count;++i){
                if(lease_>0&&--lease_==0)audition_=Audition::Normal;
                auto advance=[this](double v,double target){return v<target?std::min(target,v+step_):std::max(target,v-step_);};
                deltaMix_=advance(deltaMix_,audition_==Audition::Delta?1.:0.);
                detectMix_=advance(detectMix_,audition_==Audition::Detect?1.:0.);
                // Normalize simultaneous fade weights; no additive monitor boost.
                const double norm=std::max(1.,deltaMix_+detectMix_);const double d=deltaMix_/norm,t=detectMix_/norm;
                auto mix=[d,t](float dry,float wet,float detect){return finiteFloat((1-d-t)*wet+d*(clean(dry)-wet)+t*detect);};
                if(d>0||t>0){l[pos+i]=mix(dryL[i],l[pos+i],detectL[i]);if(channels_==2)r[pos+i]=mix(dryR[i],r[pos+i],detectR[i]);}
            }
            pos+=count;
        }
    }
    Core core_;Monitor observer_;Identity id_{};int channels_=0;
    std::atomic<bool> analysisRequested_{false};int analysisRemaining_=0,analysisLease_=0;
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::uint64_t lastSeen_=0,applied_=0,frames_=0;std::uint32_t drops_=0;
    Audition audition_=Audition::Normal;int lease_=0,leaseFrames_=0,fadeFrames_=1,telemetryPeriod_=1,untilTelemetry_=0;
    double deltaMix_=0,detectMix_=0,step_=1;
    FxSpsc<Command,commandCapacity> commands_;
    FxSpsc<Ack,ackCapacity> acks_;
    FxSpsc<Snapshot,telemetryCapacity> telemetry_;
};
} // namespace armix::builtin_fx::de_ezzer
