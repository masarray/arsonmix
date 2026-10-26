// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace armix::device
{
// Control-plane only. Models JUCE Windows ASIO's registry name -> CLSID map.
// The CLSID identifies a registered DRIVER CLASS, never an audio interface,
// USB port, hardware clock, or physical endpoint. No allocation occurs in RT.
class AsioRegistrationPolicy final
{
public:
    struct Entry
    {
        std::wstring deviceName;
        std::wstring registryKey;
        std::wstring clsid;
        bool inprocServerRegistered = false;
    };
    struct Identity
    {
        std::wstring registryKey;
        std::wstring canonicalClsid;
        bool valid() const noexcept
        {
            return !registryKey.empty() && !canonicalClsid.empty();
        }
        bool sameDriverClass(const Identity& other) const noexcept
        {
            return valid() && registryKey == other.registryKey
                && canonicalClsid == other.canonicalClsid;
        }
    };
    enum class Error
    {
        none, invalidName, missing, ambiguousName, invalidClsid,
        missingComServer, registryScanFailed
    };
    struct Result
    {
        Identity identity;
        Error error = Error::missing;
        bool ok() const noexcept { return error == Error::none && identity.valid(); }
    };

    // Exact canonical {8-4-4-4-12}; reject ProgIDs, empty/nil GUIDs and
    // ambiguous strings even though Windows CLSIDFromString permits ProgIDs.
    static std::wstring canonicalGuid(std::wstring_view input)
    {
        if (input.size() != 38 || input.front() != L'{' || input.back() != L'}')
            return {};
        bool anyNonzero = false;
        std::wstring canonical(input);
        for (std::size_t i = 1; i < 37; ++i)
        {
            if (i == 9 || i == 14 || i == 19 || i == 24)
            {
                if (input[i] != L'-') return {};
                continue;
            }
            const wchar_t ch = input[i];
            const bool hex = (ch >= L'0' && ch <= L'9')
                          || (ch >= L'a' && ch <= L'f')
                          || (ch >= L'A' && ch <= L'F');
            if (!hex) return {};
            if (ch != L'0') anyNonzero = true;
            if (ch >= L'A' && ch <= L'F')
                canonical[i] = static_cast<wchar_t>(ch + (L'a' - L'A'));
        }
        return anyNonzero ? canonical : std::wstring{};
    }

    static Result resolve(std::span<const Entry> records,
                          std::wstring_view selectedName)
    {
        if (selectedName.empty()) return {{}, Error::invalidName};
        const Entry* found = nullptr;
        for (const auto& record : records)
        {
            if (record.deviceName != selectedName) continue;
            if (found) return {{}, Error::ambiguousName};
            found = &record;
        }
        if (!found) return {{}, Error::missing};
        auto guid = canonicalGuid(found->clsid);
        if (guid.empty() || found->registryKey.empty())
            return {{}, Error::invalidClsid};
        if (!found->inprocServerRegistered)
            return {{}, Error::missingComServer};
        return {{found->registryKey, std::move(guid)}, Error::none};
    }

    static const char* reason(Error error) noexcept
    {
        switch(error)
        {
            case Error::none: return "ASIO_REGISTRATION_MATCHED";
            case Error::invalidName: return "ASIO_EMPTY_SELECTED_DRIVER_NAME";
            case Error::missing: return "ASIO_REGISTRATION_MISSING";
            case Error::ambiguousName: return "ASIO_AMBIGUOUS_REGISTERED_DRIVER_NAME";
            case Error::invalidClsid: return "ASIO_INVALID_DRIVER_CLSID";
            case Error::missingComServer: return "ASIO_DRIVER_COM_SERVER_NOT_REGISTERED";
            case Error::registryScanFailed: return "ASIO_REGISTRY_ENUMERATION_INCOMPLETE";
        }
        return "ASIO_UNKNOWN_REGISTRATION_ERROR";
    }
};
} // namespace armix::device
