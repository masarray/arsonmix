// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicPreparedMixerBlock.h"

namespace armix::mixer {
// One transient last-cue selection, bound to the exact installed render
// graph and physical MONITOR mapping. CONTROL publishes; AUDIO alone owns
// capture and transition state. No persistent WIRE or session mutation.
class NativePflMonitor final {
public:
    struct Key {
        std::uint64_t epoch=0,revision=0,route=0;
        ChannelId channel{};
    };
    struct State {ChannelId requested{},applied{};};
    bool select(Key key) noexcept {
        if(!key.epoch||!key.revision||!key.route||
           nextTicket_==std::numeric_limits<std::uint64_t>::max())return false;
        // Invalidate the old ticket BEFORE writing the payload. Sequentially
        // consistent atomics + a never-reused ticket give a bounded coherent
        // snapshot without waiting for a preempted CONTROL writer.
        active_.store(0);
        epoch_.store(key.epoch);revision_.store(key.revision);
        route_.store(key.route);channel_.store(key.channel.value);
        active_.store(++nextTicket_);
        return true;
    }
    void cancel() noexcept {active_.store(0);}
    void revokePublished() noexcept {revoke(active_.load());}
    State state(std::uint64_t epoch,std::uint64_t revision,std::uint64_t route) const noexcept {
        Request r;
        if(!read(r)||!matches(r.key,epoch,revision,route))return {};
        return {r.key.channel,applied_.load()==r.ticket?r.key.channel:ChannelId{}};
    }
    DynamicPflCapture* begin(std::uint64_t epoch,std::uint64_t revision,
                            std::uint64_t route,int frames) noexcept {
        block_={};capture_.reset();
        Request r;
        if(!read(r)){resetAudio();return nullptr;}
        if(!matches(r.key,epoch,revision,route)||frames<1||frames>DynamicPflCapture::capacity){
            revoke(r.ticket);resetAudio();return nullptr;
        }
        block_=r;
        if(!r.key.channel.value)return nullptr; // explicit OFF can fade back
        capture_.requested=r.key.channel;
        return &capture_;
    }
    // Exactly once, AFTER a successful native render of a distinct MONITOR
    // pair. A failed, superseded or aliased render must not touch any output.
    void finish(bool ok,float* l,float* r,int frames) noexcept {
        if(!block_.ticket)return;
        Request current;
        if(!ok||!l||!r||l==r||frames<1||
           !read(current)||current.ticket!=block_.ticket){
            revoke(block_.ticket);resetAudio();return;
        }
        const auto& key=block_.key;
        const bool listening=key.channel.value!=0;
        if(listening&&(capture_.captured()!=key.channel||
                       capture_.left().size()!=static_cast<std::size_t>(frames))){
            revoke(block_.ticket);resetAudio();return;
        }
        const bool sameRoute=matches(lastKey_,key.epoch,key.revision,key.route);
        if(!sameRoute)resetAudio(); // no stale samples across a device/graph boundary
        if(!listening&&!lastTicket_){applied_.store(block_.ticket);return;}
        if(lastTicket_!=block_.ticket){
            // Start from the last emitted sample only on the same verified
            // route. Selection changes and explicit OFF use a bounded ramp.
            fromL_=lastTicket_?lastL_:l[0];fromR_=lastTicket_?lastR_:r[0];
            remaining_=64;
            lastTicket_=block_.ticket;lastKey_=key;
        }
        const auto left=capture_.left(),right=capture_.right();
        for(int i=0;i<frames;++i){
            float wantedL=listening?safeCue(left[static_cast<std::size_t>(i)]):l[i];
            float wantedR=listening?safeCue(right[static_cast<std::size_t>(i)]):r[i];
            if(remaining_>0){
                const float blend=1.f-static_cast<float>(--remaining_)/64.f;
                wantedL=fromL_+(wantedL-fromL_)*blend;
                wantedR=fromR_+(wantedR-fromR_)*blend;
            }
            l[i]=lastL_=wantedL;r[i]=lastR_=wantedR;
        }
        applied_.store(block_.ticket);
        if(!listening&&!remaining_){revoke(block_.ticket);resetAudio();}
    }
private:
    struct Request {Key key{};std::uint64_t ticket=0;};
    static bool matches(const Key& k,std::uint64_t e,std::uint64_t v,std::uint64_t p) noexcept {
        return p&&k.epoch==e&&k.revision==v&&k.route==p;
    }
    static float safeCue(float v) noexcept {
        // Conservative fixed audition trim, independent of MAIN volume.
        return std::isfinite(v)?std::clamp(v*.25f,-.5f,.5f):0.f;
    }
    bool read(Request& r) const noexcept {
        const auto ticket=active_.load();if(!ticket)return false;
        const Key k{epoch_.load(),revision_.load(),route_.load(),ChannelId{channel_.load()}};
        if(active_.load()!=ticket)return false;
        r={k,ticket};return true;
    }
    void revoke(std::uint64_t ticket) noexcept {
        if(ticket)active_.compare_exchange_strong(ticket,0);
    }
    void resetAudio() noexcept {
        lastKey_={};lastTicket_=0;remaining_=0;
        lastL_=lastR_=fromL_=fromR_=0;applied_.store(0);
    }
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::uint64_t nextTicket_=0; // CONTROL only
    std::atomic<std::uint64_t> active_{0},epoch_{0},revision_{0},route_{0},channel_{0},applied_{0};
    Request block_{};Key lastKey_{};
    std::uint64_t lastTicket_=0;
    int remaining_=0;
    float lastL_=0,lastR_=0,fromL_=0,fromR_=0;
    DynamicPflCapture capture_{};
};
} // namespace armix::mixer
