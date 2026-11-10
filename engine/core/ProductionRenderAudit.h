// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>

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
