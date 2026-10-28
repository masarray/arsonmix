// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

namespace armix::dsp
{
// Logical insert order is encoded as eight 4-bit physical slot identities.
// Callback reads exactly one 64-bit snapshot at an audio block boundary.
// Reorder is control-thread-only; processor ownership is NOT changed.
struct FxOrderPlan final
{
    static constexpr int capacity = 8;
    static constexpr std::uint64_t canonical = 0x76543210ULL;
    static constexpr int physicalAt (std::uint64_t word, int logical) noexcept
    {
        return static_cast<int> ((word >> (logical * 4)) & 0xfULL);
    }
    static constexpr std::uint64_t moved (std::uint64_t previous,
                                          int count, int from, int to) noexcept
    {
        if (count < 1 || count > capacity || from < 0 || to < 0
            || from >= count || to >= count || from == to)
            return previous;
        std::array<int, capacity> slots {};
        for (int i = 0; i < count; ++i) slots[i] = physicalAt (previous, i);
        const auto chosen = slots[from];
        if (from < to)
            for (int i = from; i < to; ++i) slots[i] = slots[i + 1];
        else
            for (int i = from; i > to; --i) slots[i] = slots[i - 1];
        slots[to] = chosen;
        auto next = canonical;
        for (int i = 0; i < count; ++i)
        {
            const auto shift = static_cast<unsigned> (i * 4);
            next = (next & ~(0xfULL << shift))
                 | (static_cast<std::uint64_t> (slots[i]) << shift);
        }
        return next;
    }
};
} // namespace armix::dsp
