// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// IO-2D: native callback selection is a pure rule, not another renderer.
// The existing qualified reader and publisher still check exact leases.
namespace armix::device {
enum class QualifiedCaptureHostMode {
    directPrimary, qualifiedDual, rejectPartial
};
constexpr QualifiedCaptureHostMode qualifiedCaptureHostMode(
    bool hasReader,bool hasClock) noexcept {
    if(!hasReader&&!hasClock)return QualifiedCaptureHostMode::directPrimary;
    if(hasReader&&hasClock)return QualifiedCaptureHostMode::qualifiedDual;
    return QualifiedCaptureHostMode::rejectPartial;
}
} // namespace armix::device
