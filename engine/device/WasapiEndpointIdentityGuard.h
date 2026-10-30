// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "EndpointIdentityPolicy.h"
#include <juce_core/juce_core.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

namespace armix::device
{
// Windows CONTROL THREAD only. Strict, read-only, two-phase endpoint ID
// guard around JUCE's name-based WASAPI open. Does not claim a native
// ID-bound IAudioClient or a shared hardware clock.
class WasapiEndpointIdentityGuard final
{
public:
    using Selection = EndpointIdentityPolicy::Selection;
    struct Result
    {
        Selection selection;
        juce::String error;
        bool ok() const noexcept { return error.isEmpty() && selection.valid(); }
    };
    static Result resolve (const juce::String& capture,
                           const juce::String& render)
    {
        const HRESULT hr = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED (hr) && hr != RPC_E_CHANGED_MODE)
            return {{}, "Cannot initialize Windows Core Audio identity check"};
        struct ComGuard final
        {
            bool owned;
            ~ComGuard() { if (owned) CoUninitialize(); }
        } com {hr == S_OK || hr == S_FALSE};

        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> devices;
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS (devices.GetAddressOf()))))
            return {{}, "Windows endpoint identity enumerator unavailable"};

        std::vector<EndpointIdentityPolicy::Endpoint> endpoints;
        endpoints.reserve (16);
        if (!scan (devices.Get(), eCapture, EndpointIdentityPolicy::Role::capture, endpoints)
            || !scan (devices.Get(), eRender, EndpointIdentityPolicy::Role::render, endpoints))
            return {{}, "Windows endpoint identity enumeration incomplete"};

        const auto d = EndpointIdentityPolicy::resolve (
            endpoints, capture.toWideCharPointer(), render.toWideCharPointer());
        if (!d.ok()) return {{}, errorText (d.error)};
        return {d.selection, {}};
    }

    static juce::String verify (const Selection& expected,
                                const juce::String& actualCapture,
                                const juce::String& actualRender)
    {
        // JUCE's active reported names must still be those requested.
        if (!expected.valid()
            || expected.captureName != std::wstring (actualCapture.toWideCharPointer())
            || expected.renderName != std::wstring (actualRender.toWideCharPointer()))
            return "Opened independent JUCE endpoint names differ from the selected route";
        const auto current = resolve (actualCapture, actualRender);
        if (!current.ok()) return current.error;
        if (!EndpointIdentityPolicy::sameIdentity (expected, current.selection))
            return "Windows capture/render endpoint identity changed during device opening";
        return {};
    }


    // ASIO MAIN -> independent WASAPI virtual-cable output requires render-
    // only identity verification. No invented/unused WASAPI capture endpoint.
    struct RenderSelection {
        std::wstring name, id;
        bool valid() const noexcept{return !name.empty() && !id.empty();}
    };
    struct RenderResult {
        RenderSelection selection;
        juce::String error;
        bool ok() const noexcept{return selection.valid() && error.isEmpty();}
    };
    static RenderResult resolveRender(const juce::String& label) {
        if(label.isEmpty())return {{}, "Choose an explicit stream output endpoint"};
        const HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE)
            return {{}, "Cannot inspect Windows render endpoint identities"};
        struct Guard {bool owns;~Guard(){if(owns)CoUninitialize();}}
            guard{hr==S_OK||hr==S_FALSE};
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> device;
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,
                 CLSCTX_INPROC_SERVER,IID_PPV_ARGS(device.GetAddressOf()))))
            return {{}, "Windows render endpoint enumerator unavailable"};
        std::vector<EndpointIdentityPolicy::Endpoint> endpoints;
        if(!scan(device.Get(),eRender,EndpointIdentityPolicy::Role::render,endpoints))
            return {{}, "Cannot enumerate active Windows playback endpoints"};
        RenderSelection found;
        int n=0;
        const std::wstring wanted(label.toWideCharPointer());
        for(const auto& e:endpoints)
            if(e.name==wanted){found={e.name,e.id};++n;}
        if(n!=1)return {{}, n==0 ? "Selected playback endpoint missing" :
                     "Duplicate endpoint names; select an unambiguous device"};
        return {std::move(found),{}};
    }
    static juce::String verifyRender(const RenderSelection& expected,
                                     const juce::String& openedName) {
        if(!expected.valid() ||
           expected.name!=std::wstring(openedName.toWideCharPointer()))
            return "WASAPI opened an unexpected playback endpoint";
        auto current=resolveRender(openedName);
        if(!current.ok())return current.error;
        return current.selection.id==expected.id ? juce::String() :
               juce::String("WASAPI playback endpoint ID changed during open");
    }
    static juce::String fingerprint (const std::wstring& id)
    {
        // Diagnostic correlation only, never a cryptographic identity.
        std::uint64_t hash = 14695981039346656037ull;
        for (const wchar_t ch : id)
        {
            const auto code = static_cast<std::uint16_t> (ch);
            hash = (hash ^ static_cast<std::uint8_t> (code)) * 1099511628211ull;
            hash = (hash ^ static_cast<std::uint8_t> (code >> 8)) * 1099511628211ull;
        }
        return juce::String::toHexString (static_cast<juce::int64> (hash));
    }

private:
    static bool scan (IMMDeviceEnumerator* devices, EDataFlow flow,
                      EndpointIdentityPolicy::Role role,
                      std::vector<EndpointIdentityPolicy::Endpoint>& out)
    {
        Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
        if (FAILED (devices->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE,
                                                  collection.GetAddressOf()))) return false;
        UINT count = 0;
        if (FAILED (collection->GetCount (&count)) || count > 128) return false;
        for (UINT i=0; i<count; ++i)
        {
            Microsoft::WRL::ComPtr<IMMDevice> endpoint;
            if (FAILED (collection->Item (i, endpoint.GetAddressOf()))) return false;
            LPWSTR id = nullptr;
            if (FAILED (endpoint->GetId (&id)) || id == nullptr) return false;
            const std::wstring stableId (id);
            CoTaskMemFree (id);

            Microsoft::WRL::ComPtr<IPropertyStore> store;
            if (FAILED (endpoint->OpenPropertyStore (STGM_READ, store.GetAddressOf())))
                return false;
            PROPVARIANT friendly;
            PropVariantInit (&friendly);
            const HRESULT hr = store->GetValue (PKEY_Device_FriendlyName, &friendly);
            const bool usable = SUCCEEDED (hr) && friendly.vt == VT_LPWSTR
                && friendly.pwszVal != nullptr;
            std::wstring name;
            if (usable) name = friendly.pwszVal;
            PropVariantClear (&friendly);
            if (!usable || name.empty()) return false;
            out.push_back ({role, std::move (name), stableId});
        }
        return true;
    }
    static juce::String errorText (EndpointIdentityPolicy::Error e)
    {
        using E = EndpointIdentityPolicy::Error;
        switch (e)
        {
            case E::emptyName: return "Select both named capture and render endpoints first";
            case E::captureMissing: return "Selected Windows capture endpoint is missing or has no stable ID";
            case E::renderMissing: return "Selected Windows render endpoint is missing or has no stable ID";
            case E::captureAmbiguous: return "Selected Windows capture name is ambiguous; choose a uniquely named endpoint";
            case E::renderAmbiguous: return "Selected Windows render name is ambiguous; choose a uniquely named endpoint";
            default: return {};
        }
    }
};
} // namespace armix::device
#else
namespace armix::device
{
// Explicitly unsupported on non-Windows. The independent adapter is Windows
// WASAPI-only; do not pretend macOS/Linux names are Windows endpoint IDs.
class WasapiEndpointIdentityGuard final
{
public:
    using Selection = EndpointIdentityPolicy::Selection;
    struct Result
    {
        Selection selection;
        juce::String error;
        bool ok() const noexcept { return false; }
    };
    static Result resolve (const juce::String&, const juce::String&)
    {
        return {{}, "Windows WASAPI endpoint identity guard unavailable"};
    }
    static juce::String verify (const Selection&, const juce::String&, const juce::String&)
    {
        return "Windows WASAPI endpoint identity guard unavailable";
    }
    struct RenderSelection {
        std::wstring name,id;
        bool valid() const noexcept{return false;}
    };
    struct RenderResult {
        RenderSelection selection;
        juce::String error;
        bool ok() const noexcept{return false;}
    };
    static RenderResult resolveRender(const juce::String&)
    {return {{}, "WASAPI virtual cable requires Windows"};}
    static juce::String verifyRender(const RenderSelection&,const juce::String&)
    {return "WASAPI virtual cable requires Windows";}
    static juce::String fingerprint (const std::wstring&) { return {}; }
};
} // namespace armix::device
#endif
