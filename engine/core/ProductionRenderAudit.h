// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <span>

// Read-only evidence for the single JUCE production renderer. Neither this
// type nor its counters authorize PCM/output. No heap, lock, wait or log on RT.
namespace armix::rt {
class ProductionRenderAudit final {
public:
    enum class Path : std::uint8_t { channelId, compatibilityM4 };
    struct Snapshot {
        std::uint64_t nativeStarted=0,nativeCompleted=0,nativeFailed=0;
        std::uint64_t m4Started=0,m4Completed=0,m4Failed=0;
        std::uint64_t overlapDenied=0,unpatchedSilent=0,mutedNoRender=0;
    };
    // CONTROL/field diagnostics only. The caller must supply two snapshots
    // from the same process and same *physical* device epoch, both with an
    // independently verified installed ChannelId admission. This never grants
    // permission to render, arm an output, or retire legacy code.
    enum class WindowStatus : std::uint8_t {
        invalidContext, countersRegressed, boundaryInFlight,
        legacyRendered, nativeFailed, noNativeRender,
        silentOrMuted, exclusiveNativeObserved
    };
    struct RenderWindow {
        WindowStatus status=WindowStatus::invalidContext;
        Snapshot delta{};
        [[nodiscard]] constexpr bool exclusiveNativeObserved() const noexcept {
            return status==WindowStatus::exclusiveNativeObserved;
        }
    };
    [[nodiscard]] static constexpr RenderWindow assessWindow(
            Snapshot before,Snapshot after,bool sameProcess,
            bool sameDeviceEpoch,bool channelIdAdmittedAtBothEnds) noexcept {
        if(!sameProcess || !sameDeviceEpoch || !channelIdAdmittedAtBothEnds)
            return {WindowStatus::invalidContext,{}};
        const auto monotonic=[](Snapshot x,Snapshot y) constexpr noexcept {
            return y.nativeStarted>=x.nativeStarted &&
                y.nativeCompleted>=x.nativeCompleted &&
                y.nativeFailed>=x.nativeFailed &&
                y.m4Started>=x.m4Started &&
                y.m4Completed>=x.m4Completed &&
                y.m4Failed>=x.m4Failed &&
                y.overlapDenied>=x.overlapDenied &&
                y.unpatchedSilent>=x.unpatchedSilent &&
                y.mutedNoRender>=x.mutedNoRender;
        };
        if(!monotonic(before,after))
            return {WindowStatus::countersRegressed,{}};
        // Relaxed atomic CONTROL snapshots can catch one callback in flight.
        // Neither partial endpoint is valid proof of a completed DSP window.
        const auto settled=[](Snapshot s) constexpr noexcept {
            return s.nativeStarted>=s.nativeCompleted &&
                s.nativeStarted-s.nativeCompleted==s.nativeFailed &&
                s.m4Started>=s.m4Completed &&
                s.m4Started-s.m4Completed==s.m4Failed;
        };
        if(!settled(before) || !settled(after))
            return {WindowStatus::boundaryInFlight,{}};
        const Snapshot delta{
            after.nativeStarted-before.nativeStarted,
            after.nativeCompleted-before.nativeCompleted,
            after.nativeFailed-before.nativeFailed,
            after.m4Started-before.m4Started,
            after.m4Completed-before.m4Completed,
            after.m4Failed-before.m4Failed,
            after.overlapDenied-before.overlapDenied,
            after.unpatchedSilent-before.unpatchedSilent,
            after.mutedNoRender-before.mutedNoRender};
        if(delta.m4Started || delta.m4Completed || delta.m4Failed)
            return {WindowStatus::legacyRendered,delta};
        if(delta.nativeFailed || delta.overlapDenied)
            return {WindowStatus::nativeFailed,delta};
        if(delta.nativeCompleted==0)
            return {WindowStatus::noNativeRender,delta};
        if(delta.unpatchedSilent || delta.mutedNoRender)
            return {WindowStatus::silentOrMuted,delta};
        return {WindowStatus::exclusiveNativeObserved,delta};
    }
    // CONTROL-only, conservative evidence over MULTIPLE nonempty bounded
    // windows. This prevents one short transient native window from being
    // mistaken for proof that the M4 compatibility path can be retired.
    // processNonce/epoch/admission must come from independently checked
    // device/session evidence, not from a browser badge or raw counter.
    // A continuous span crosses NO device restarts; repeat on a new epoch.
    struct Boundary {
        Snapshot counters{};
        std::uint64_t processNonce=0,deviceEpoch=0;
        bool verifiedInstalledChannelId=false;
    };
    enum class SeriesStatus : std::uint8_t {
        tooFewWindows,invalidContext,badRenderWindow,
        insufficientNativeBlocks,continuousNativeObserved
    };
    struct RenderSeries {
        SeriesStatus status=SeriesStatus::tooFewWindows;
        WindowStatus firstBadWindow=WindowStatus::invalidContext;
        std::uint64_t nativeCompleted=0;
        [[nodiscard]] constexpr bool continuousNativeObserved() const noexcept {
            return status==SeriesStatus::continuousNativeObserved;
        }
    };
    [[nodiscard]] static constexpr RenderSeries assessSeries(
            std::span<const Boundary> boundaries,
            std::uint64_t minimumNativeCompleted=128) noexcept {
        // Require >=3 independent bounded windows, not a single callback.
        // A zero caller-selected threshold cannot bypass the evidence gate.
        if(boundaries.size()<4 || minimumNativeCompleted==0)
            return {SeriesStatus::tooFewWindows,WindowStatus::invalidContext,0};
        const auto& first=boundaries.front();
        if(!first.processNonce || !first.deviceEpoch ||
           !first.verifiedInstalledChannelId)
            return {SeriesStatus::invalidContext,WindowStatus::invalidContext,0};
        std::uint64_t total=0;
        for(std::size_t i=1;i<boundaries.size();++i) {
            const auto& prior=boundaries[i-1];
            const auto& current=boundaries[i];
            if(prior.processNonce!=first.processNonce ||
               current.processNonce!=first.processNonce ||
               prior.deviceEpoch!=first.deviceEpoch ||
               current.deviceEpoch!=first.deviceEpoch ||
               !prior.verifiedInstalledChannelId ||
               !current.verifiedInstalledChannelId)
                return {SeriesStatus::invalidContext,WindowStatus::invalidContext,total};
            const auto window=assessWindow(prior.counters,current.counters,
                true,true,true);
            if(!window.exclusiveNativeObserved())
                return {SeriesStatus::badRenderWindow,window.status,total};
            if(total>std::numeric_limits<std::uint64_t>::max()-
                       window.delta.nativeCompleted)
                return {SeriesStatus::badRenderWindow,
                        WindowStatus::countersRegressed,total};
            total+=window.delta.nativeCompleted;
        }
        return total>=minimumNativeCompleted?
            RenderSeries{SeriesStatus::continuousNativeObserved,
                         WindowStatus::exclusiveNativeObserved,total}:
            RenderSeries{SeriesStatus::insufficientNativeBlocks,
                         WindowStatus::noNativeRender,total};
    }
    ProductionRenderAudit()=default;
    ProductionRenderAudit(const ProductionRenderAudit&)=delete;
    ProductionRenderAudit& operator=(const ProductionRenderAudit&)=delete;
    void started(Path path) noexcept {select(path,nativeStarted_,m4Started_).fetch_add(1,std::memory_order_relaxed);}
    void completed(Path path) noexcept {select(path,nativeCompleted_,m4Completed_).fetch_add(1,std::memory_order_relaxed);}
    void failed(Path path) noexcept {select(path,nativeFailed_,m4Failed_).fetch_add(1,std::memory_order_relaxed);}
    void deniedOverlap() noexcept {overlapDenied_.fetch_add(1,std::memory_order_relaxed);}
    void unpatched() noexcept {unpatchedSilent_.fetch_add(1,std::memory_order_relaxed);}
    void muted() noexcept {mutedNoRender_.fetch_add(1,std::memory_order_relaxed);}
    // CONTROL only: independent relaxed loads can briefly show an in-flight
    // attempt. Counters are monotonic since process start, not per device.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        return {nativeStarted_.load(std::memory_order_relaxed),
                nativeCompleted_.load(std::memory_order_relaxed),
                nativeFailed_.load(std::memory_order_relaxed),
                m4Started_.load(std::memory_order_relaxed),
                m4Completed_.load(std::memory_order_relaxed),
                m4Failed_.load(std::memory_order_relaxed),
                overlapDenied_.load(std::memory_order_relaxed),
                unpatchedSilent_.load(std::memory_order_relaxed),
                mutedNoRender_.load(std::memory_order_relaxed)};
    }
private:
    using Counter=std::atomic<std::uint64_t>;
    static Counter& select(Path path,Counter& native,Counter& m4) noexcept {
        return path==Path::channelId?native:m4;
    }
    Counter nativeStarted_{0},nativeCompleted_{0},nativeFailed_{0};
    Counter m4Started_{0},m4Completed_{0},m4Failed_{0};
    Counter overlapDenied_{0},unpatchedSilent_{0},mutedNoRender_{0};
};
} // namespace armix::rt
