// SPDX-License-Identifier: AGPL-3.0-only
// The Windows SDK declares these AudioEndpoint PROPERTYKEYs as extern unless
// INITGUID is set in exactly one translation unit per executable.
// Both the JUCE application and the read-only MMDevice CTest link this unit.
// Never put INITGUID in WindowsFirstRunEndpointDiscovery.h: its multiple
// include sites could emit duplicate definitions or alter unrelated GUIDs.
// See Microsoft's DEFINE_PROPERTYKEY documentation.
#if defined(_WIN32)
#include <initguid.h>
#include <mmdeviceapi.h>
#endif
