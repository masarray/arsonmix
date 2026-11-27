// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

namespace armix::session {
// CONTROL-only hint, separate from native graph identity and PCM admission.
struct CheckpointStamp {
    std::uint64_t epoch=0,graph=0,live=0,logicalIn=0,logicalOut=0;
    float masterGain=0;
    bool masterMute=false,masterMono=false,resumeMain=false;
    bool operator==(const CheckpointStamp&) const noexcept = default;
};
class SessionCheckpointCadence final {
public:
    static constexpr std::uint64_t quietMs=2000, retryMs=5000, refreshMs=60000;
    // Single existing control timer. No I/O, allocation, callback or vendor calls.
    bool observe(std::uint64_t now,bool disarmed,bool ready,CheckpointStamp stamp,
                 std::uint64_t maxDirtyAge=0) noexcept {
        if(!disarmed) {wasDisarmed_=false;return false;}
        if(!wasDisarmed_||stamp!=stamp_) {
            if(!wasDisarmed_||!dirty_)dirtySince_=now;
            wasDisarmed_=true;stamp_=stamp;dirty_=true;quietSince_=now;
        }
        if(!ready){quietSince_=now;return false;}
        const bool overdue=dirty_&&maxDirtyAge&&now>=dirtySince_&&now-dirtySince_>=maxDirtyAge;
        if(now<retryAt_||(!overdue&&(now<quietSince_||now-quietSince_<quietMs)))return false;
        if(!dirty_&&(now<lastAttempt_||now-lastAttempt_<refreshMs))return false;
        dirty_=false;lastAttempt_=now;
        return true;
    }
    // Separate instance: eligible while live, metadata only, even during a
    // continuous fader drag after 10 seconds. Full vendor capture uses observe.
    bool observeLive(std::uint64_t now,bool live,bool ready,CheckpointStamp stamp) noexcept {
        return observe(now,live,ready,stamp,10000);
    }
    void failed(std::uint64_t now) noexcept {
        dirty_=true;retryAt_=now>UINT64_MAX-retryMs?UINT64_MAX:now+retryMs;
    }
private:
    CheckpointStamp stamp_{};
    bool wasDisarmed_=false,dirty_=true;
    std::uint64_t quietSince_=0,lastAttempt_=0,retryAt_=0,dirtySince_=0;
};
} // namespace armix::session
