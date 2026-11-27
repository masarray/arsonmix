// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace armix::windowchrome {
inline constexpr int resizeEdge = 8;      // JUCE logical units, DPI-scaled by peer
inline constexpr int resizeCorner = 16;   // extend along the reserved edge
enum class ResizeZone { client, left, right, top, bottom, topLeft, topRight, bottomLeft, bottomRight };
constexpr ResizeZone resizeZone(float x, float y, float w, float h, bool enabled) noexcept
{
    if (!enabled || w < 2 * resizeCorner || h < 2 * resizeCorner ||
        x < 0 || y < 0 || x >= w || y >= h)
        return ResizeZone::client;
    const bool l = x < resizeEdge, r = x >= w - resizeEdge;
    const bool t = y < resizeEdge, b = y >= h - resizeEdge;
    if (!(l || r || t || b)) return ResizeZone::client;
    const bool nearL = x < resizeCorner, nearR = x >= w - resizeCorner;
    const bool nearT = y < resizeCorner, nearB = y >= h - resizeCorner;
    if (nearL && nearT) return ResizeZone::topLeft;
    if (nearR && nearT) return ResizeZone::topRight;
    if (nearL && nearB) return ResizeZone::bottomLeft;
    if (nearR && nearB) return ResizeZone::bottomRight;
    if (l) return ResizeZone::left;
    if (r) return ResizeZone::right;
    if (t) return ResizeZone::top;
    return ResizeZone::bottom;
}
// Compile-time production contract, also runnable without JUCE or Windows.
static_assert(resizeZone(0, 100, 1100, 650, true) == ResizeZone::left);
static_assert(resizeZone(7.9f, 100, 1100, 650, true) == ResizeZone::left);
static_assert(resizeZone(8, 100, 1100, 650, true) == ResizeZone::client);
static_assert(resizeZone(1092, 100, 1100, 650, true) == ResizeZone::right);
static_assert(resizeZone(300, 0, 1100, 650, true) == ResizeZone::top);
static_assert(resizeZone(300, 649, 1100, 650, true) == ResizeZone::bottom);
static_assert(resizeZone(15, 2, 1100, 650, true) == ResizeZone::topLeft);
static_assert(resizeZone(1098, 15, 1100, 650, true) == ResizeZone::topRight);
static_assert(resizeZone(2, 635, 1100, 650, true) == ResizeZone::bottomLeft);
static_assert(resizeZone(1098, 649, 1100, 650, true) == ResizeZone::bottomRight);
static_assert(resizeZone(10, 10, 1100, 650, true) == ResizeZone::client);
static_assert(resizeZone(0, 100, 1100, 650, false) == ResizeZone::client);
static_assert(resizeZone(-1, 0, 1100, 650, true) == ResizeZone::client);
static_assert(resizeZone(1100, 0, 1100, 650, true) == ResizeZone::client);
}
