// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// LMS M2 native PCM kernel. Not a second callback, mixer, or device manager.
// CONTROL: validate/compile/allocate. AUDIO: bounded process only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace armix::lms {
struct VerifiedOutputPort final {
    std::string identity; // exact verified device-native pin, not DEMO/GUI ordinal
    int callbackOrdinal=-1;
};
struct ParametricBand final {
    double frequencyHz=1000.,q=0.70710678118,gainDb=0.;
};
struct OutputPatch final {
    std::string outputId,portIdentity;
    int sourceSide=0,section=0;  // section [0,ways); side 0=L/mono, 1=R
    double delayMs=0.,gainDb=0.;
    std::vector<ParametricBand> peq; // max 8 fully prepared peaking bands
    bool mute=true,limiterEnabled=false;
    double ceilingDbfs=-6.;
    // A host-owned, explicit operator acknowledgement, NOT proof of physical protection.
    bool confirmedSpeakerProfile=false;
};
struct SpeakerSystemRequest final {
    std::uint64_t deviceEpoch=0,graphRevision=0;
    double sampleRate=0.;
    int maxFrames=0,activeOutputChannels=0,ways=2; // 1..16, budget constrained
    bool stereo=true;
    std::vector<double> crossoverHz; // ascending, ways-1
    std::vector<VerifiedOutputPort> verifiedPorts;
    std::vector<OutputPatch> outputs;
    std::size_t scratchBudgetBytes=16U*1024U*1024U;
};
enum class SpeakerPlanFault : std::uint8_t {
    none,invalidEpoch,invalidFormat,invalidCrossover,invalidPort,
    duplicatePort,invalidRoute,invalidOutputDsp,unconfirmedSpeaker,
    scratchBudget,preparationFailed
};
struct SpeakerPrepareResult final {
    SpeakerPlanFault fault=SpeakerPlanFault::none;
    std::size_t scratchBytes=0;
    [[nodiscard]] bool ok() const noexcept {return fault==SpeakerPlanFault::none;}
};

// 2 x 2nd-order Butterworth = LR4, 24 dB/oct, in-phase complementary pair.
struct Biquad final {
    double b0=1.,b1=0.,b2=0.,a1=0.,a2=0.,z1=0.,z2=0.;
    [[nodiscard]] float process(float in) noexcept {
        const double x=static_cast<double>(in),y=b0*x+z1;
        z1=b1*x-a1*y+z2;z2=b2*x-a2*y;
        if(std::abs(z1)<1.e-28)z1=0.;
        if(std::abs(z2)<1.e-28)z2=0.;
        return static_cast<float>(y);
    }
    static Biquad make(double sr,double hz,bool high) noexcept {
        constexpr double q=0.7071067811865475244;
        constexpr double pi=3.14159265358979323846;
        const double w=2.*pi*hz/sr,c=std::cos(w),a=std::sin(w)/(2.*q);
        const double scale=1./(1.+a);
        Biquad b;
        b.b0=((high?(1.+c):(1.-c))*.5)*scale;
        b.b1=(high?-(1.+c):(1.-c))*scale;
        b.b2=b.b0;b.a1=-2.*c*scale;b.a2=(1.-a)*scale;
        return b;
    }
    static Biquad peak(double sr,double hz,double q,double gainDb) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double w=2.*pi*hz/sr,c=std::cos(w),alpha=std::sin(w)/(2.*q);
        const double A=std::pow(10.,gainDb/40.),scale=1./(1.+alpha/A);
        Biquad b;
        b.b0=(1.+alpha*A)*scale;
        b.b1=-2.*c*scale;
        b.b2=(1.-alpha*A)*scale;
        b.a1=b.b1;
        b.a2=(1.-alpha/A)*scale;
        return b;
    }
};

class PreparedSpeakerSystem final {
    struct Section final {
        int side=0,way=0;
        std::array<Biquad,2> hp{},lp{};
        bool hasHigh=false,hasLow=false;
    };
    struct Destination final {
        int source=0,physical=0;
        float gain=1.f,ceiling=1.f;
        bool mute=true,limit=false;
        double delaySamples=0.;
        std::vector<Biquad> eq;
        std::vector<float> ring;
        std::size_t write=0;
        [[nodiscard]] float delay(float sample) noexcept {
            if(delaySamples<=0.)return sample;
            ring[write]=sample;
            double read=static_cast<double>(write)-delaySamples;
            if(read<0.)read+=static_cast<double>(ring.size());
            const auto index=static_cast<std::size_t>(read);
            const auto other=(index+1)%ring.size();
            const float frac=static_cast<float>(read-static_cast<double>(index));
            const float result=ring[index]+(ring[other]-ring[index])*frac;
            if(++write==ring.size())write=0;
            return result;
        }
    };
    std::uint64_t epoch_=0,revision_=0;
    int capacity_=0,physicalCount_=0,ways_=0;
    bool stereo_=false;
    std::vector<Section> sections_;
    std::vector<Destination> destinations_;
    std::vector<float> scratch_;
    std::size_t usedBytes_=0;
public:
    PreparedSpeakerSystem()=default;
    PreparedSpeakerSystem(const PreparedSpeakerSystem&)=delete;
    PreparedSpeakerSystem& operator=(const PreparedSpeakerSystem&)=delete;
    PreparedSpeakerSystem(PreparedSpeakerSystem&&)=default;
    PreparedSpeakerSystem& operator=(PreparedSpeakerSystem&&)=default;
    [[nodiscard]] std::size_t scratchBytes() const noexcept{return usedBytes_;}
    [[nodiscard]] std::size_t sectionCount() const noexcept{return sections_.size();}
    [[nodiscard]] std::size_t destinationCount() const noexcept{return destinations_.size();}
    [[nodiscard]] std::uint64_t epoch() const noexcept{return epoch_;}
    [[nodiscard]] std::uint64_t revision() const noexcept{return revision_;}

    // A rejected candidate NEVER changes the prior accepted snapshot.
    static SpeakerPrepareResult prepare(const SpeakerSystemRequest& req,
        std::unique_ptr<PreparedSpeakerSystem>& accepted) {
        SpeakerPrepareResult result;
        try {
        if(!req.deviceEpoch||!req.graphRevision){
            result.fault=SpeakerPlanFault::invalidEpoch;return result;
        }
        if(!(req.sampleRate>=8000.&&req.sampleRate<=384000.)||
           !std::isfinite(req.sampleRate)||req.maxFrames<1||req.maxFrames>8192||
           req.ways<1||req.ways>16||req.activeOutputChannels<1||
           req.activeOutputChannels>256){
            result.fault=SpeakerPlanFault::invalidFormat;return result;
        }
        if(req.crossoverHz.size()!=static_cast<std::size_t>(req.ways-1)){
            result.fault=SpeakerPlanFault::invalidCrossover;return result;
        }
        double prev=15.;
        for(const auto hz:req.crossoverHz){
            if(!std::isfinite(hz)||hz<20.||hz<=prev*1.01||
               hz>=req.sampleRate*.45){
                result.fault=SpeakerPlanFault::invalidCrossover;return result;
            }
            prev=hz;
        }
        std::unordered_set<std::string> names;
        std::unordered_set<int> ordinals;
        if(req.verifiedPorts.empty()||req.verifiedPorts.size()>256){
            result.fault=SpeakerPlanFault::invalidPort;return result;
        }
        for(const auto& p:req.verifiedPorts) {
            if(p.identity.empty()||p.identity.starts_with("DEMO-")||
               p.callbackOrdinal<0||p.callbackOrdinal>=req.activeOutputChannels||
               !names.insert(p.identity).second||
               !ordinals.insert(p.callbackOrdinal).second){
                result.fault=SpeakerPlanFault::invalidPort;return result;
            }
        }
        if(req.outputs.size()>req.verifiedPorts.size()){
            result.fault=SpeakerPlanFault::invalidPort;return result;
        }
        std::unordered_set<std::string> logical;
        std::unordered_set<std::string> physical;
        std::size_t totalRing=0,totalEq=0;
        for(const auto& p:req.outputs){
            if(p.outputId.empty()||p.portIdentity.empty()||
               !logical.insert(p.outputId).second||
               !physical.insert(p.portIdentity).second){
                result.fault=SpeakerPlanFault::duplicatePort;return result;
            }
            if((p.sourceSide!=0&&p.sourceSide!=1)||
               (!req.stereo&&p.sourceSide!=0)||
               p.section<0||p.section>=req.ways){
                result.fault=SpeakerPlanFault::invalidRoute;return result;
            }
            if(!std::any_of(req.verifiedPorts.begin(),req.verifiedPorts.end(),
                [&](const auto& x){return x.identity==p.portIdentity;})){
                result.fault=SpeakerPlanFault::invalidPort;return result;
            }
            if(!std::isfinite(p.gainDb)||p.gainDb< -60.||p.gainDb>6.||
               !std::isfinite(p.delayMs)||p.delayMs<0.||p.delayMs>500.||
               !std::isfinite(p.ceilingDbfs)||p.ceilingDbfs< -60.||p.ceilingDbfs>0.){
                result.fault=SpeakerPlanFault::invalidOutputDsp;return result;
            }
            if(p.peq.size()>8){
                result.fault=SpeakerPlanFault::invalidOutputDsp;return result;
            }
            for(const auto& band:p.peq)
                if(!std::isfinite(band.frequencyHz)||band.frequencyHz<20.||
                   band.frequencyHz>=req.sampleRate*.45||
                   !std::isfinite(band.q)||band.q<.1||band.q>18.||
                   !std::isfinite(band.gainDb)||band.gainDb< -18.||
                   band.gainDb>18.){
                    result.fault=SpeakerPlanFault::invalidOutputDsp;return result;
                }
            totalEq+=p.peq.size();
            if(!p.mute&&(!p.confirmedSpeakerProfile||!p.limiterEnabled)){
                result.fault=SpeakerPlanFault::unconfirmedSpeaker;return result;
            }
            const double samples=std::ceil(p.delayMs*req.sampleRate/1000.);
            if(samples>=static_cast<double>(
                std::numeric_limits<std::size_t>::max()/sizeof(float)-4)){
                result.fault=SpeakerPlanFault::scratchBudget;return result;
            }
            totalRing+=static_cast<std::size_t>(samples)+3;
        }
        const auto sections=static_cast<std::size_t>(req.ways)*(req.stereo?2U:1U);
        const auto maxFloats=req.scratchBudgetBytes/sizeof(float);
        if(sections>maxFloats/static_cast<std::size_t>(req.maxFrames)||
           totalRing>maxFloats-sections*static_cast<std::size_t>(req.maxFrames)||
           totalEq*sizeof(Biquad)>req.scratchBudgetBytes-
                (totalRing+sections*static_cast<std::size_t>(req.maxFrames))*sizeof(float)){
            result.fault=SpeakerPlanFault::scratchBudget;return result;
        }
            auto next=std::make_unique<PreparedSpeakerSystem>();
            next->epoch_=req.deviceEpoch;next->revision_=req.graphRevision;
            next->capacity_=req.maxFrames;next->physicalCount_=req.activeOutputChannels;
            next->ways_=req.ways;next->stereo_=req.stereo;
            next->scratch_.resize(sections*static_cast<std::size_t>(req.maxFrames));
            next->sections_.reserve(sections);
            for(int side=0;side<(req.stereo?2:1);++side)
              for(int way=0;way<req.ways;++way){
                Section s;s.side=side;s.way=way;
                if(way>0){
                    s.hasHigh=true;
                    s.hp[0]=s.hp[1]=Biquad::make(req.sampleRate,req.crossoverHz[way-1],true);
                }
                if(way<req.ways-1){
                    s.hasLow=true;
                    s.lp[0]=s.lp[1]=Biquad::make(req.sampleRate,req.crossoverHz[way],false);
                }
                next->sections_.push_back(s);
              }
            next->destinations_.reserve(req.outputs.size());
            for(const auto& p:req.outputs){
                Destination d;
                const auto pin=std::find_if(req.verifiedPorts.begin(),req.verifiedPorts.end(),
                    [&](const auto& x){return x.identity==p.portIdentity;});
                d.physical=pin->callbackOrdinal;
                d.source=p.sourceSide*req.ways+p.section;
                d.gain=static_cast<float>(std::pow(10.,p.gainDb/20.));
                d.mute=p.mute;d.limit=p.limiterEnabled;
                d.ceiling=static_cast<float>(std::pow(10.,p.ceilingDbfs/20.));
                d.delaySamples=p.delayMs*req.sampleRate/1000.;
                d.eq.reserve(p.peq.size());
                for(const auto& band:p.peq)
                    d.eq.push_back(Biquad::peak(req.sampleRate,band.frequencyHz,
                                                band.q,band.gainDb));
                if(d.delaySamples>0.)
                    d.ring.resize(static_cast<std::size_t>(std::ceil(d.delaySamples))+3);
                next->destinations_.push_back(std::move(d));
            }
            next->usedBytes_=next->scratch_.size()*sizeof(float);
            for(const auto& d:next->destinations_)
                next->usedBytes_+=d.ring.size()*sizeof(float)+d.eq.size()*sizeof(Biquad);
            result.scratchBytes=next->usedBytes_;
            accepted=std::move(next);
            return result;
        }catch(...){
            result.fault=SpeakerPlanFault::preparationFailed;return result;
        }
    }
    // AUDIO only. All source planes are copied into prepared section buffers
    // BEFORE output writes, so input aliasing physical MAIN is supported.
    // On stale epoch/revision, bad frame count, or absent pin: zero writes.
    [[nodiscard]] bool process(std::uint64_t currentDeviceEpoch,
        std::uint64_t acceptedGraphRevision,const float* mainL,
        const float* mainR,float* const* output,int physicalCount,
        int frames,bool nativeOutputAuthorized) noexcept {
        if(!nativeOutputAuthorized||currentDeviceEpoch!=epoch_||
           acceptedGraphRevision!=revision_||physicalCount!=physicalCount_||
           frames<1||frames>capacity_||!mainL||(stereo_&&!mainR)||!output)
            return false;
        for(const auto& d:destinations_)
            if(d.physical<0||d.physical>=physicalCount||!output[d.physical])
                return false;
        if(destinations_.empty())return true; // no active physical pins
        for(std::size_t idx=0;idx<sections_.size();++idx){
            auto& s=sections_[idx];
            const float* in=s.side?mainR:mainL;
            float* band=scratch_.data()+idx*static_cast<std::size_t>(capacity_);
            for(int i=0;i<frames;++i){
                float x=in[i];
                if(!std::isfinite(x))x=0.f;
                if(s.hasHigh)for(auto& b:s.hp)x=b.process(x);
                if(s.hasLow)for(auto& b:s.lp)x=b.process(x);
                band[i]=std::isfinite(x)?x:0.f;
            }
        }
        for(auto& d:destinations_){
            float* out=output[d.physical];
            const float* band=scratch_.data()+static_cast<std::size_t>(d.source)*capacity_;
            if(d.mute){std::fill_n(out,frames,0.f);continue;}
            for(int i=0;i<frames;++i){
                float x=band[i];
                for(auto& eq:d.eq)x=eq.process(x);
                x=d.delay(x)*d.gain;
                if(!std::isfinite(x))x=0.f;
                // Hard digital peak ceiling is NOT calibrated loudspeaker protection.
                if(d.limit)x=std::clamp(x,-d.ceiling,d.ceiling);
                out[i]=x;
            }
        }
        return true;
    }
};
} // namespace armix::lms
