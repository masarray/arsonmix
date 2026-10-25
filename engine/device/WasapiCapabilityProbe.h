// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <wrl/client.h>
#include <cstdint>

namespace armix::device
{
// Explicit USER-CONTROL THREAD operation. Never call from an audio callback,
// from a device event handler, or during an active device graph transition.
// Observe native Windows endpoint capabilities without changing the driver,
// initializing an audio stream, or claiming an endpoint's clock is shared.
class WasapiCapabilityProbe final
{
public:
    static juce::String report() 
    {
        const HRESULT initialized = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        const bool ownsCom = initialized == S_OK || initialized == S_FALSE;
        // RPC_E_CHANGED_MODE means caller already initialized another COM
        // apartment model; COM is usable on this thread without uninitializing.
        if (FAILED (initialized) && initialized != RPC_E_CHANGED_MODE)
            return "WASAPI_CAPABILITY: COM initialization failed " + hex (initialized);
        struct ComScope final
        {
            bool owns;
            ~ComScope() { if (owns) CoUninitialize(); }
        } scope {ownsCom};

        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        const HRESULT created = CoCreateInstance (
            __uuidof (MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS (enumerator.GetAddressOf()));
        if (FAILED (created))
            return "WASAPI_CAPABILITY: endpoint enumerator unavailable " + hex (created);

        juce::String text;
        text << "WASAPI native (Windows Core Audio): read-only enumeration; "
                "reported engine periods are capabilities, not measured physical latency.\n";
        scan (enumerator.Get(), eCapture, "CAPTURE", text);
        scan (enumerator.Get(), eRender, "RENDER", text);
        return text;
    }

private:
    static juce::String hex (HRESULT result)
    {
        return "0x" + juce::String::toHexString (static_cast<juce::int64> (static_cast<std::uint32_t> (result))).paddedLeft ('0',8);
    }

    static std::uint64_t fingerprint (const wchar_t* value) noexcept
    {
        // Diagnostic correlation only; do not include the OS endpoint GUID.
        std::uint64_t hash = 14695981039346656037ull;
        if (value)
            while (*value)
            {
                const auto ch = static_cast<std::uint16_t> (*value++);
                hash = (hash ^ static_cast<unsigned char> (ch)) * 1099511628211ull;
                hash = (hash ^ static_cast<unsigned char> (ch >> 8)) * 1099511628211ull;
            }
        return hash;
    }

    static void scan (IMMDeviceEnumerator* enumerator, EDataFlow flow,
                      const char* kind, juce::String& text)
    {
        Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
        const auto hr = enumerator->EnumAudioEndpoints (flow, DEVICE_STATE_ACTIVE,
                                                         collection.GetAddressOf());
        if (FAILED (hr))
        {
            text << kind << ": enumerate failed " << hex (hr) << "\n";
            return;
        }
        UINT count = 0;
        if (FAILED (collection->GetCount (&count))) return;
        const auto limit = static_cast<UINT> (juce::jmin (32u, count));
        text << kind << " ACTIVE ENDPOINTS: " << count;
        if (count > limit) text << " (first " << limit << " only)";
        text << "\n";

        for (UINT index = 0; index < limit; ++index)
        {
            Microsoft::WRL::ComPtr<IMMDevice> endpoint;
            if (FAILED (collection->Item (index, endpoint.GetAddressOf()))) continue;
            LPWSTR endpointId = nullptr;
            juce::String id;
            if (SUCCEEDED (endpoint->GetId (&endpointId)) && endpointId != nullptr)
            {
                id = juce::String::toHexString (static_cast<juce::int64> (fingerprint (endpointId)));
                CoTaskMemFree (endpointId);
            }
            juce::String name = "(unnamed)";
            Microsoft::WRL::ComPtr<IPropertyStore> store;
            if (SUCCEEDED (endpoint->OpenPropertyStore (STGM_READ, store.GetAddressOf())))
            {
                PROPVARIANT prop;
                PropVariantInit (&prop);
                if (SUCCEEDED (store->GetValue (PKEY_Device_FriendlyName, &prop)) &&
                    prop.vt == VT_LPWSTR && prop.pwszVal)
                    name = juce::String (prop.pwszVal);
                PropVariantClear (&prop);
            }
            // Defensive report bounds; endpoint names can contain arbitrary
            // user-supplied text. Device IDs are never copied to clipboard.
            text << "  [" << index << "] " << name.substring (0,100)
                 << " | idHash=" << id << "\n";
            Microsoft::WRL::ComPtr<IAudioClient> client;
            const HRESULT activated = endpoint->Activate (
                __uuidof (IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
                reinterpret_cast<void**> (client.GetAddressOf()));
            if (FAILED (activated))
            {
                text << "    IAudioClient unavailable: " << hex (activated) << "\n";
                continue;
            }
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED (client->GetMixFormat (&mix)) && mix != nullptr)
            {
                text << "    MIX format: " << mix->nSamplesPerSec << " Hz, "
                     << mix->nChannels << " channels, "
                     << mix->wBitsPerSample << " bits, tag "
                     << mix->wFormatTag << "\n";
                Microsoft::WRL::ComPtr<IAudioClient3> client3;
                if (SUCCEEDED (client.As (&client3)))
                {
                    UINT32 defaultFrames=0,fundamentalFrames=0,minFrames=0,maxFrames=0;
                    const HRESULT period = client3->GetSharedModeEnginePeriod (
                        mix, &defaultFrames, &fundamentalFrames, &minFrames, &maxFrames);
                    if (SUCCEEDED (period))
                        text << "    IAudioClient3 engine periods (frames): default="
                             << defaultFrames << ", fundamental=" << fundamentalFrames
                             << ", min=" << minFrames << ", max=" << maxFrames << "\n";
                    else
                        text << "    IAudioClient3 period not available: " << hex (period) << "\n";
                }
                else text << "    IAudioClient3 unsupported (legacy Windows Audio mode)\n";
                CoTaskMemFree (mix);
            }
            REFERENCE_TIME default100ns=0,minimum100ns=0;
            if (SUCCEEDED (client->GetDevicePeriod (&default100ns,&minimum100ns)))
                text << "    IAudioClient device periods (100ns): default="
                     << static_cast<juce::int64> (default100ns) << ", minimum="
                     << static_cast<juce::int64> (minimum100ns) << "\n";
        }
    }
};
} // namespace armix::device
#endif
