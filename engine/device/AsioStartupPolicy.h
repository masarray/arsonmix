// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <span>
#include <string_view>

namespace armix::device
{
// Explicit, bounded control-plane-only initial backend ordering.
// A single ASIO device must advertise the SAME name for capture/render.
// Never combine two unrelated ASIO drivers into a fake same-clock device.
// No device opened, no rate or buffer guessed, no audio callback work.
class AsioStartupPolicy final
{
public:
    static bool preferAsio (std::span<const std::string_view> captures,
                            std::span<const std::string_view> renders) noexcept
    {
        return captures.size() == 1 && renders.size() == 1
            && !captures.front().empty()
            && captures.front() == renders.front();
    }
};
} // namespace armix::device
