// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace armix::mixer {
// A private physical MONITOR override, never a programme insert or extra
// renderer. CONTROL is the sole request writer. AUDIO is the sole scratch
// owner. All atomic words are lock-free, reader attempts once (no spin).
enum class FxCueMode : std::uint64_t { Mix=0, Ess=1, Sidechain=2, Delta=3 };
struct FxCueKey {
    std::uint64_t epoch=0,revision=0,route=0,instance=0,instanceEpoch=0;
    bool operator==(const FxCueKey&) const noexcept=default;
    bool valid() const noexcept {return epoch&&revision&&route&&instance&&instanceEpoch;}
};
struct FxCueFrame {
    FxCueKey key{};
    FxCueMode mode=FxCueMode::Mix;
    float* left=nullptr;
    float* right=nullptr;
    int frames=0;
    bool claimed=false,written=false;
};
inline thread_local FxCueFrame* currentFxCueFrame=nullptr;
class FxCueScope final {
public:
    explicit FxCueScope(FxCueFrame* frame) noexcept:previous_(currentFxCueFrame){currentFxCueFrame=frame;}
    ~FxCueScope() noexcept {currentFxCueFrame=previous_;}
    FxCueScope(const FxCueScope&)=delete;
    FxCueScope& operator=(const FxCueScope&)=delete;
    static FxCueFrame* claim(std::uint64_t instance,std::uint64_t epoch,int frames) noexcept {
        auto* p=currentFxCueFrame;
        if(!p||p->claimed||p->key.instance!=instance||p->key.instanceEpoch!=epoch||
           frames!=p->frames)return nullptr;
        p->claimed=true;return p;
    }
private:
    FxCueFrame* previous_;
};
class NativeFxCueBus final {
public:
    static constexpr int capacity=4096;
    struct State {FxCueMode requested=FxCueMode::Mix,applied=FxCueMode::Mix;};
    bool start(FxCueKey key,FxCueMode mode,double sampleRate,std::uint32_t now) noexcept {
        if(!key.valid()||mode==FxCueMode::Mix||!validMode(mode)||
           !std::isfinite(sampleRate)||sampleRate<44100||sampleRate>96000||
           nextTicket_==std::numeric_limits<std::uint64_t>::max())return false;
        control_={key,mode,++nextTicket_,1,static_cast<int>(sampleRate*.5),
                  std::max(1,static_cast<int>(sampleRate*.005))};
        lastRenew_=now;publish();return true;
    }
    bool renew(FxCueKey key,std::uint32_t now) noexcept {
        if(control_.mode==FxCueMode::Mix||control_.key!=key)return false;
        if(expired(now)||revoked_.load()>=control_.ticket||
           control_.heartbeat==std::numeric_limits<std::uint64_t>::max()){
            cancel(false);return false;
        }
        ++control_.heartbeat;lastRenew_=now;publish();return true;
    }
    void expire(std::uint32_t now) noexcept {
        if(!control_.ticket)return;
        if(revoked_.load()>=control_.ticket||
           (control_.mode!=FxCueMode::Mix&&expired(now))||
           (control_.mode==FxCueMode::Mix&&finished_.load()==control_.ticket))cancel(false);
    }
    // A device/mute gate may invalidate from a non-CONTROL thread.
    // Atomic only; no scratch, request publication or ownership mutation.
    void revokePublished() noexcept {markRevoked(published_.load(std::memory_order_acquire));}
    void cancel(bool graceful) noexcept {
        if(control_.mode==FxCueMode::Mix){
            if(!graceful&&control_.ticket){control_={};publish();}
            return;
        }
        if(graceful)control_.mode=FxCueMode::Mix;
        else control_={};
        publish();
    }
    State state(FxCueKey key) const noexcept {
        if(control_.key!=key||control_.mode==FxCueMode::Mix||
           revoked_.load()>=control_.ticket)return {};
        return {control_.mode,applied_.load()==control_.ticket?control_.mode:FxCueMode::Mix};
    }
    // Called only under the publisher's existing exclusive render permit.
    // route==0 represents no physical output authority (metering is not cue).
    FxCueFrame* begin(std::uint64_t epoch,std::uint64_t revision,std::uint64_t route,
                      int frames) noexcept {
        frame_={};block_={};
        if(!published_.load(std::memory_order_acquire)){if(lastTicket_)hardReset();return nullptr;}
        Request req;
        if(!read(req)){hardReset();return nullptr;}
        block_=req;
        if(!route||!matches(req,epoch,revision,route)||frames<1||frames>capacity){
            revoke(req);return nullptr;
        }
        if(revoked_.load()>=req.ticket){hardReset();return nullptr;}
        if(req.mode==FxCueMode::Mix){
            // Explicit stop on the same verified route: finish() may fade
            // from the last bounded cue sample back to the real monitor.
            if(lastKey_!=req.key||lastTicket_!=req.ticket)hardReset();
            return nullptr;
        }
        if(lastKey_!=req.key){hardReset();lastKey_=req.key;}
        if(lastTicket_!=req.ticket||heartbeat_!=req.heartbeat){
            lastTicket_=req.ticket;heartbeat_=req.heartbeat;remaining_=req.leaseFrames;
        }
        if(remaining_<frames){revoke(req);return nullptr;}
        remaining_-=frames;
        frame_={req.key,req.mode,left_.data(),right_.data(),frames,false,false};
        return &frame_;
    }
    // Only the already-rendered MONITOR pair may be passed here. On a
    // rejected render, do not touch any physical samples.
    void finish(bool ok,float* monitorL,float* monitorR,int frames) noexcept {
        if(!block_.ticket)return;
        Request latest;
        if(!ok||!monitorL||!monitorR||monitorL==monitorR){revoke(block_);return;}
        if(!read(latest)||latest.ticket!=block_.ticket||latest.key!=block_.key||
           latest.mode!=block_.mode||revoked_.load()>=block_.ticket){
            hardReset();return;
        }
        if(block_.mode==FxCueMode::Mix){
            applied_.store(0);
            if(lastKey_!=block_.key||lastTicket_!=block_.ticket){finished_.store(block_.ticket);hardReset();return;}
            const float step=1.f/static_cast<float>(std::max(1,block_.fadeFrames));
            for(int i=0;i<frames&&mix_>0;++i){
                mix_=std::max(0.f,mix_-step);
                monitorL[i]=(1-mix_)*monitorL[i]+mix_*lastL_;
                monitorR[i]=(1-mix_)*monitorR[i]+mix_*lastR_;
            }
            if(mix_==0){finished_.store(block_.ticket);hardReset();}
            return;
        }
        if(!frame_.written||frame_.frames!=frames){revoke(block_);return;}
        const float step=1.f/static_cast<float>(std::max(1,block_.fadeFrames));
        for(int i=0;i<frames;++i){
            mix_=std::min(1.f,mix_+step);
            lastL_=finiteCue(left_[i]);lastR_=finiteCue(right_[i]);
            monitorL[i]=(1-mix_)*monitorL[i]+mix_*lastL_;
            monitorR[i]=(1-mix_)*monitorR[i]+mix_*lastR_;
        }
        applied_.store(block_.ticket);
    }
    // AUDIO only, with same exclusive render permit. Invalid route/mute is
    // intentionally fail-closed, not a residual audible cue tail.
    void invalidate() noexcept {Request r;if(read(r))revoke(r);else hardReset();}
private:
    struct Request {
        FxCueKey key{};FxCueMode mode=FxCueMode::Mix;
        std::uint64_t ticket=0,heartbeat=0;
        int leaseFrames=0,fadeFrames=1;
    };
    static bool validMode(FxCueMode m) noexcept {
        return m==FxCueMode::Mix||m==FxCueMode::Ess||m==FxCueMode::Sidechain||m==FxCueMode::Delta;
    }
    static bool matches(const Request& r,std::uint64_t e,std::uint64_t v,std::uint64_t p) noexcept {
        return r.ticket&&r.key.epoch==e&&r.key.revision==v&&r.key.route==p;
    }
    bool expired(std::uint32_t now) const noexcept {return now-lastRenew_>=500u;}
    static float finiteCue(float x) noexcept {return std::isfinite(x)?std::clamp(x,-.5f,.5f):0.f;}
    void publish() noexcept {
        // seq_cst on all fields gives a coherent one-attempt read, even when
        // a CONTROL renewal races the AUDIO snapshot. Never wait for writer.
        sequence_.fetch_add(1);
        const auto& k=control_.key;
        const std::array<std::uint64_t,10> v{k.epoch,k.revision,k.route,k.instance,k.instanceEpoch,
            static_cast<std::uint64_t>(control_.mode),control_.ticket,control_.heartbeat,
            static_cast<std::uint64_t>(control_.leaseFrames),static_cast<std::uint64_t>(control_.fadeFrames)};
        for(std::size_t i=0;i<v.size();++i)words_[i].store(v[i]);
        sequence_.fetch_add(1);
        published_.store(control_.ticket,std::memory_order_release);
    }
    bool read(Request& r) const noexcept {
        const auto seq=sequence_.load();if(seq&1u)return false;
        std::array<std::uint64_t,10> v{};
        for(std::size_t i=0;i<v.size();++i)v[i]=words_[i].load();
        if(sequence_.load()!=seq)return false;
        r={{v[0],v[1],v[2],v[3],v[4]},static_cast<FxCueMode>(v[5]),v[6],v[7],
           static_cast<int>(v[8]),static_cast<int>(v[9])};
        return true;
    }
    void hardReset() noexcept {
        mix_=lastL_=lastR_=0;lastKey_={};lastTicket_=heartbeat_=0;remaining_=0;applied_.store(0);
    }
    void markRevoked(std::uint64_t ticket) noexcept {
        // Usually one writer (audio), with a possible device gate race.
        // Never roll the invalidation floor backward. No unbounded CAS loop.
        auto old=revoked_.load();
        for(int attempt=0;attempt<2&&old<ticket;++attempt)
            if(revoked_.compare_exchange_strong(old,ticket))break;
    }
    void revoke(const Request& r) noexcept {markRevoked(r.ticket);hardReset();}
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    Request control_{};std::uint64_t nextTicket_=0;std::uint32_t lastRenew_=0;
    std::atomic<std::uint64_t> sequence_{0},applied_{0},revoked_{0},published_{0},finished_{0};
    std::array<std::atomic<std::uint64_t>,10> words_{};
    Request block_{};FxCueFrame frame_{};
    FxCueKey lastKey_{};std::uint64_t lastTicket_=0,heartbeat_=0;
    int remaining_=0;float mix_=0,lastL_=0,lastR_=0;
    std::array<float,capacity> left_{},right_{};
};
} // namespace armix::mixer
