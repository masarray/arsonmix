// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AsioRegistrationPolicy.h"
#include <cstdint>
#include <iterator>
#include <string_view>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace armix::device
{
// Explicitly CONTROL-THREAD ONLY. Mirrors the registry sources used by pinned
// JUCE 8 Windows ASIO: HKLM/SOFTWARE/ASIO (process-native registry view),
// the optional driver description, its CLSID and HKCR/CLSID/InprocServer32.
// It does NOT open a COM class, a sound device or the audio callback.
class AsioRegistryIdentityProbe final
{
public:
    using Policy = AsioRegistrationPolicy;
    struct Result
    {
        Policy::Result registration;
        std::size_t scanned = 0;
    };
    static Result resolve(const std::wstring& selectedName)
    {
#if defined(_WIN32)
        if (selectedName.empty())
            return {{{}, Policy::Error::invalidName}, 0};
        HKEY raw = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0,
                          KEY_READ, &raw) != ERROR_SUCCESS)
            return {{{}, Policy::Error::missing}, 0};
        const Key root(raw);
        std::vector<Policy::Entry> entries;
        entries.reserve(16);
        constexpr DWORD maxEntries = 128;
        for (DWORD index = 0; index <= maxEntries; ++index)
        {
            wchar_t keyName[256] = {};
            DWORD length = static_cast<DWORD>(std::size(keyName));
            const auto enumerated = RegEnumKeyExW(root.handle,index,keyName,
                &length,nullptr,nullptr,nullptr,nullptr);
            if (enumerated == ERROR_NO_MORE_ITEMS)
                return {Policy::resolve(entries,selectedName),entries.size()};
            if (enumerated != ERROR_SUCCESS || index == maxEntries)
                return {{{},Policy::Error::registryScanFailed},entries.size()};
            HKEY item = nullptr;
            if (RegOpenKeyExW(root.handle,keyName,0,KEY_READ,&item) != ERROR_SUCCESS)
                return {{{},Policy::Error::registryScanFailed},entries.size()};
            const Key entry(item);
            std::wstring description;
            // This is JUCE's fallback rule: missing description => key name.
            if (!readString(entry.handle,L"description",description,true))
                description = keyName;
            // Do not treat duplicate NAME matches as safe even with same CLSID;
            // JUCE selects by its name-to-index mapping and might choose either.
            if (description != selectedName) continue;
            std::wstring guid;
            const bool hasGuid = readString(entry.handle,L"clsid",guid);
            const auto canonical = hasGuid ? Policy::canonicalGuid(guid) : std::wstring{};
            entries.push_back({description,keyName,guid,
                               !canonical.empty() && hasComServer(canonical)});
        }
        return {{{},Policy::Error::registryScanFailed},entries.size()};
#else
        (void)selectedName;
        return {{{},Policy::Error::registryScanFailed},0};
#endif
    }

    // A one-way correlation label only; security/physical identity is NOT
    // guaranteed by this short hash. Compare FULL CLSIDs for authorization.
    static std::uint64_t diagnosticFingerprint(const Policy::Identity& identity) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        if (!identity.valid()) return 0;
        appendFingerprint(hash,identity.registryKey);
        appendFingerprint(hash,identity.canonicalClsid);
        return hash;
    }
private:
    static void appendFingerprint(std::uint64_t& hash,
                                  std::wstring_view text) noexcept
    {
        const auto length=static_cast<std::uint64_t>(text.size());
        for (unsigned bit=0;bit<64;bit+=8)
            hash=(hash ^ ((length>>bit)&255u))*1099511628211ull;
        for (const auto ch:text)
        {
            const auto code=static_cast<std::uint16_t>(ch);
            hash=(hash ^ (code & 255u))*1099511628211ull;
            hash=(hash ^ (code >> 8))*1099511628211ull;
        }
    }
#if defined(_WIN32)
    struct Key
    {
        HKEY handle = nullptr;
        explicit Key(HKEY value) noexcept : handle(value) {}
        ~Key() { if(handle) RegCloseKey(handle); }
        Key(const Key&) = delete;
        Key& operator=(const Key&) = delete;
    };

    static bool readString(HKEY key,const wchar_t* value,std::wstring& result,
                           bool allowEmpty = false)
    {
        wchar_t buffer[512] = {};
        DWORD size=sizeof(buffer);
        DWORD type=0;
        if (RegQueryValueExW(key,value,nullptr,&type,
              reinterpret_cast<BYTE*>(buffer),&size) != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)
            || size < sizeof(wchar_t) || size > sizeof(buffer)
            || size % sizeof(wchar_t) != 0
            || buffer[size/sizeof(wchar_t)-1] != L'\0') return false;
        result.assign(buffer);
        return allowEmpty || !result.empty();
    }

    static bool hasComServer(const std::wstring& canonical)
    {
        HKEY raw=nullptr;
        const auto keyPath=L"CLSID\\"+canonical+L"\\InprocServer32";
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT,keyPath.c_str(),0,
                          KEY_READ,&raw)!=ERROR_SUCCESS) return false;
        const Key key(raw);
        std::wstring path;
        // The DLL path is never reported, opened, resolved or probed for
        // existence here: JUCE itself only checks nonempty registration.
        return readString(key.handle,nullptr,path);
    }
#endif
};
} // namespace armix::device
