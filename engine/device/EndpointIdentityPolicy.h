// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace armix::device
{
// Control-plane only. A JUCE Windows Audio device is selected by *name*,
// while MMDevice exposes a per-endpoint stable identity. Never resolve
// a name shared by multiple ACTIVE native endpoints by picking the first.
class EndpointIdentityPolicy final
{
public:
    enum class Role { capture, render };
    enum class Error { none, emptyName, captureMissing, renderMissing,
                       captureAmbiguous, renderAmbiguous };
    struct Endpoint
    {
        Role role;
        std::wstring name, id;
    };
    struct Selection
    {
        std::wstring captureName, renderName, captureId, renderId;
        bool valid() const noexcept
        {
            return !captureName.empty() && !renderName.empty()
                && !captureId.empty() && !renderId.empty();
        }
    };
    struct Decision
    {
        Selection selection;
        Error error = Error::none;
        bool ok() const noexcept { return error == Error::none && selection.valid(); }
    };
    static Decision resolve (std::span<const Endpoint> endpoints,
                             std::wstring_view input,
                             std::wstring_view output)
    {
        if (input.empty() || output.empty()) return {{},Error::emptyName};
        Decision result;
        result.selection.captureName = input;
        result.selection.renderName = output;
        const auto findUnique = [&] (Role role, std::wstring_view name,
                                      std::wstring& id, Error missing,
                                      Error ambiguous) -> Error
        {
            unsigned matches = 0;
            for (const auto& ep : endpoints)
                if (ep.role == role && ep.name == name)
                {
                    ++matches;
                    if (matches > 1) return ambiguous;
                    id = ep.id;
                }
            return matches == 1 && !id.empty() ? Error::none : missing;
        };
        result.error = findUnique (Role::capture, input,
            result.selection.captureId, Error::captureMissing, Error::captureAmbiguous);
        if (result.error != Error::none) return result;
        result.error = findUnique (Role::render, output,
            result.selection.renderId, Error::renderMissing, Error::renderAmbiguous);
        return result;
    }
    static bool sameIdentity (const Selection& before,
                              const Selection& after) noexcept
    {
        return before.valid() && after.valid()
            && before.captureName == after.captureName
            && before.renderName == after.renderName
            && before.captureId == after.captureId
            && before.renderId == after.renderId;
    }
};
} // namespace armix::device
