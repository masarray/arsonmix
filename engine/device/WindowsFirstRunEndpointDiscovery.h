// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "FirstRunEndpointPolicy.h"
#include <algorithm>
#include <cwctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <cstring>

namespace armix::device
{
// Windows control thread ONLY, one-shot on a pristine first launch.
// The native MMDevice API supplies stable endpoint IDs, form factor and mix
// format; JUCE supplies its own selectable names. Neither a display label nor
// an invented "Realtek" substring is permitted to establish a device binding.
// A form factor is *system-reported*, not a cryptographic physical guarantee:
// unknown or suspicious virtual interfaces must fail closed.
class WindowsFirstRunEndpointDiscovery final
{
public:
    using P = FirstRunEndpointPolicy;
    struct Result
    {
        P::Decision decision;
        std::wstring diagnostic;
        bool inspected = false;
    };
    static Result inspect (std::span<const std::wstring> juceCaptureNames,
                           std::span<const std::wstring> juceRenderNames)
    {
        const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED (init) && init != RPC_E_CHANGED_MODE)
            return { {}, L"COM_NOT_AVAILABLE", false };
        struct ScopedCom { bool owns; ~ScopedCom() { if (owns) CoUninitialize(); } }
            scope { init == S_OK || init == S_FALSE };
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr,
                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS (enumerator.GetAddressOf()))))
            return { {}, L"MMDEVICE_ENUMERATOR_UNAVAILABLE", false };

        std::wstring defaultRenderId;
        Microsoft::WRL::ComPtr<IMMDevice> defaultRender;
        if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (
                eRender, eMultimedia, defaultRender.GetAddressOf())))
            defaultRenderId = stableId (defaultRender.Get());

        std::vector<P::Endpoint> endpoints;
        endpoints.reserve (16);
        if (!scan (enumerator.Get(), eCapture, P::Direction::capture,
                   juceCaptureNames, defaultRenderId, endpoints) ||
            !scan (enumerator.Get(), eRender, P::Direction::render,
                   juceRenderNames, defaultRenderId, endpoints))
            return { {}, L"INCOMPLETE_NATIVE_INVENTORY", false };

        auto selected = P::choose (endpoints, false);
        return { std::move (selected), L"MMDEVICE_READ_ONLY_VERIFIED", true };
    }

private:
    static std::wstring stableId (IMMDevice* device)
    {
        if (device == nullptr) return {};
        LPWSTR id = nullptr;
        if (FAILED (device->GetId (&id)) || id == nullptr) return {};
        std::wstring copy (id);
        CoTaskMemFree (id);
        return copy;
    }
    static std::wstring propertyString (IPropertyStore* store, REFPROPERTYKEY key)
    {
        PROPVARIANT property;
        PropVariantInit (&property);
        const HRESULT status = store->GetValue (key, &property);
        std::wstring value;
        if (SUCCEEDED (status) && property.vt == VT_LPWSTR &&
            property.pwszVal != nullptr)
            value = property.pwszVal;
        PropVariantClear (&property);
        return value;
    }
    static unsigned formFactor (IPropertyStore* store)
    {
        PROPVARIANT property;
        PropVariantInit (&property);
        const HRESULT status = store->GetValue (PKEY_AudioEndpoint_FormFactor, &property);
        const unsigned value = SUCCEEDED (status) && property.vt == VT_UI4
                             ? property.uintVal : static_cast<unsigned> (UnknownFormFactor);
        PropVariantClear (&property);
        return value;
    }
    static bool stereoMixFormat (IPropertyStore* store)
    {
        PROPVARIANT property;
        PropVariantInit (&property);
        const HRESULT status = store->GetValue (PKEY_AudioEngine_DeviceFormat, &property);
        bool stereo = false;
        if (SUCCEEDED (status) && property.vt == VT_BLOB &&
            property.blob.pBlobData != nullptr &&
            property.blob.cbSize >= sizeof (WAVEFORMATEX))
        {
            WAVEFORMATEX format {};
            std::memcpy (&format, property.blob.pBlobData, sizeof (format));
            stereo = format.nChannels >= 2 && format.nChannels <= 64;
        }
        PropVariantClear (&property);
        return stereo;
    }
    static std::wstring lowercase (std::wstring value)
    {
        std::transform (value.begin(), value.end(), value.begin(),
            [] (wchar_t c) { return static_cast<wchar_t> (std::towlower (c)); });
        return value;
    }
    static bool contains (const std::wstring& text, std::wstring_view part)
    {
        return text.find (part) != std::wstring::npos;
    }
    static P::Kind classify (P::Direction role,
                             const std::wstring& title,
                             const std::wstring& adapter,
                             const std::wstring& manufacturer,
                             unsigned factor)
    {
        const auto name = lowercase (title);
        const auto driver = lowercase (adapter + L" " + manufacturer);
        const bool vbAudio = contains (driver, L"vb-audio") ||
                             contains (driver, L"vb audio");
        if (role == P::Direction::capture && vbAudio &&
            contains (name, L"cable output"))
            return P::Kind::standardVbCable;
        // Vendor-specific virtual interfaces can masquerade as "Speakers".
        // Never classify an obvious software endpoint as physical.
        const auto all = driver + L" " + name;
        for (auto token : { L"virtual", L"voicemeeter", L"vb-audio",
                            L"vb audio", L"cable", L"loopback",
                            L"nvidia broadcast", L"steam streaming", L"obs virtual" })
            if (contains (all, token)) return P::Kind::otherVirtual;
        if (role != P::Direction::render || adapter.empty() ||
            manufacturer.empty()) return P::Kind::unknown;
        // Windows-reported form factor is supporting evidence. Require a real
        // adapter and manufacturer too; unknown digital/network or line form
        // factors must be explicitly selected by the user.
        if (factor == static_cast<unsigned> (Speakers) ||
            factor == static_cast<unsigned> (Headphones) ||
            factor == static_cast<unsigned> (Headset) ||
            factor == static_cast<unsigned> (DigitalAudioDisplayDevice))
            return P::Kind::physical;
        return P::Kind::unknown;
    }
    static bool scan (IMMDeviceEnumerator* enumerator, EDataFlow flow,
                      P::Direction role, std::span<const std::wstring> juceNames,
                      const std::wstring& defaultRenderId,
                      std::vector<P::Endpoint>& dest)
    {
        Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
        if (FAILED (enumerator->EnumAudioEndpoints (
                flow, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) return false;
        UINT count = 0;
        if (FAILED (collection->GetCount (&count)) || count > 128) return false;
        for (UINT i = 0; i < count; ++i)
        {
            Microsoft::WRL::ComPtr<IMMDevice> device;
            if (FAILED (collection->Item (i, device.GetAddressOf()))) return false;
            const std::wstring id = stableId (device.Get());
            if (id.empty()) return false;
            Microsoft::WRL::ComPtr<IPropertyStore> store;
            if (FAILED (device->OpenPropertyStore (STGM_READ,
                                                   store.GetAddressOf()))) return false;
            const auto name = propertyString (store.Get(), PKEY_Device_FriendlyName);
            const auto adapter = propertyString (store.Get(), PKEY_DeviceInterface_FriendlyName);
            const auto manufacturer = propertyString (store.Get(), PKEY_Device_Manufacturer);
            if (name.empty()) return false;
            P::Endpoint row;
            row.id = id;
            row.name = name;
            row.direction = role;
            row.kind = classify (role, name, adapter, manufacturer, formFactor (store.Get()));
            row.active = true;
            row.verifiedIdentity = true;
            // JUCE uses names to open a device; count exactly ONE in JUCE's
            // backend inventory. Do not trust the MMDevice ID alone to bind it.
            row.uniqueNativeName = std::count (juceNames.begin(), juceNames.end(), name) == 1;
            row.stereo = stereoMixFormat (store.Get());
            row.currentDefaultRender = role == P::Direction::render &&
                                       id == defaultRenderId;
            dest.push_back (std::move (row));
        }
        return true;
    }
};
} // namespace armix::device
#else
namespace armix::device
{
class WindowsFirstRunEndpointDiscovery final
{
public:
    struct Result
    {
        FirstRunEndpointPolicy::Decision decision;
        std::wstring diagnostic;
        bool inspected = false;
    };
    static Result inspect (std::span<const std::wstring>, std::span<const std::wstring>)
    {
        return { {}, L"WINDOWS_ONLY", false };
    }
};
} // namespace armix::device
#endif
