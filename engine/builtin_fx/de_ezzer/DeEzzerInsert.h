// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DeEzzerControl.h"
#include <bit>
#include <cstddef>
namespace armix::builtin_fx::de_ezzer {
// Explicit endian/versioned state, no transient audition, counters or histories.
struct StateCodec final {
    using Bytes=std::array<std::uint8_t,24>;
    static bool valid(Settings s) noexcept {
        Core validator;return validator.setSettings(s);
    }
    static Bytes encode(Settings s) noexcept {
        Bytes b{};const std::array<std::uint32_t,6> words{0x5a454441u,1u,
            std::bit_cast<std::uint32_t>(s.amount),std::bit_cast<std::uint32_t>(s.sensitivity),
            std::bit_cast<std::uint32_t>(s.rangeDb),static_cast<std::uint32_t>((s.enabled?1:0)|(s.mode==Mode::Voice?2:0)|(s.shape==Shape::Focused?4:0))};
        for(std::size_t i=0;i<words.size();++i)for(unsigned j=0;j<4;++j)b[4*i+j]=static_cast<std::uint8_t>(words[i]>>(8*j));
        return b;
    }
    static bool decode(const void* raw,std::size_t bytes,Settings& out) noexcept {
        if(!raw||bytes!=Bytes{}.size())return false;
        const auto* b=static_cast<const std::uint8_t*>(raw);std::array<std::uint32_t,6> w{};
        for(std::size_t i=0;i<w.size();++i)for(unsigned j=0;j<4;++j)w[i]|=static_cast<std::uint32_t>(b[4*i+j])<<(8*j);
        if(w[0]!=0x5a454441u||w[1]!=1u||w[5]>7u)return false;
        Settings s{std::bit_cast<float>(w[2]),std::bit_cast<float>(w[3]),std::bit_cast<float>(w[4]),(w[5]&1)!=0,(w[5]&2)?Mode::Voice:Mode::Auto,(w[5]&4)?Shape::Focused:Shape::Shelf};
        if(!valid(s))return false;
        out=s;return true;
    }
};
// Native insert lifecycle facade. One Control, one Core. All lifecycle calls
// require both owners stopped; enqueue/poll/readback only on CONTROL.
class Insert final {
public:
    bool prepare(Identity id,double rate,int channels,int maximumBlock) noexcept {
        if(!id.instance||!id.epoch||maximumBlock<1||maximumBlock>Control::maxFrames||
           (prepared_&&(id.instance!=actual_.id.instance||id.epoch<=actual_.id.epoch)))return false;
        const auto settings=prepared_?control_.quiescentSnapshot().settings:actual_.settings;
        if(!control_.prepare(id,rate,channels,settings))return false;
        prepared_=true;rate_=rate;channels_=channels;maximumBlock_=maximumBlock;
        actual_=control_.quiescentSnapshot();outstanding_=0;return true;
    }
    bool restoreUnpublished(const void* data,std::size_t bytes) noexcept {
        Settings s;
        if(sealed_||outstanding_||!StateCodec::decode(data,bytes,s))return false;
        if(prepared_){
            if(actual_.id.epoch==std::numeric_limits<std::uint64_t>::max())return false;
            Identity id{actual_.id.instance,actual_.id.epoch+1};
            if(!control_.prepare(id,rate_,channels_,s))return false;
            actual_=control_.quiescentSnapshot();
        }else actual_.settings=s;
        return true;
    }
    void seal() noexcept {sealed_=true;}
    bool stateSnapshotQuiescent() const noexcept {return outstanding_==0;}
    StateCodec::Bytes acceptedState() const noexcept {return StateCodec::encode(actual_.settings);}
    bool submit(const Command& cmd) noexcept {
        // Audience PCM must never be replaced by Detect/Delta.
        if(!prepared_||cmd.id!=actual_.id||cmd.audition!=Audition::Normal||
           !cmd.sequence||!StateCodec::valid(cmd.settings)||!control_.submit(cmd))return false;
        ++outstanding_;return true;
    }
    bool pollAck(Ack& ack) noexcept {
        if(!control_.pollAck(ack))return false;
        --outstanding_;actual_=ack.actual;return true;
    }
    Snapshot readback() noexcept {
        // Do not let telemetry impersonate ACK or roll accepted state backward.
        Snapshot s;
        for(int i=0;i<Control::telemetryCapacity&&control_.pollTelemetry(s);++i)
            if(s.id==actual_.id&&s.appliedSequence==actual_.appliedSequence&&s.frames>=actual_.frames)actual_=s;
        return actual_;
    }
    Identity identity() const noexcept {return actual_.id;}
    void requestAnalysis() noexcept {if(prepared_)control_.requestAnalysis();}
    void consumeRealtimeEdits() noexcept {control_.consumeRealtimeEdits();}
    bool processAudioWithCue(float* l,float* r,int n,CueMode mode,float* cueL,float* cueR) noexcept {return prepared_&&n<=maximumBlock_&&control_.processAudioWithCue(l,r,n,mode,cueL,cueR);}
    bool processAudio(float* l,float* r,int n) noexcept {return prepared_&&n<=maximumBlock_&&control_.processAudio(l,r,n);}
private:
    Control control_;Snapshot actual_{};unsigned outstanding_=0;
    double rate_=0;int channels_=0,maximumBlock_=0;bool prepared_=false,sealed_=false;
};
} // namespace armix::builtin_fx::de_ezzer
