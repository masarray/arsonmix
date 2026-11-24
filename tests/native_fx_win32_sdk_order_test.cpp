// SPDX-License-Identifier: AGPL-3.0-only
// Compile the exact problematic ordering from Main.cpp: Windows SDK already
// loaded, FX owner adapter next, multimedia API later. No fake Windows stubs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef near
#error Windows SDK near macro missing before adapter
#endif
#ifndef far
#error Windows SDK far macro missing before adapter
#endif
#include "../src/NativeFxWin32Owner.h"
#if !defined(near) || !defined(far)
#error Native FX interface corrupted existing Windows SDK macros
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <type_traits>
static_assert(std::is_pointer_v<LPWAVEFORMATEX>);
static_assert(std::is_pointer_v<decltype(&armix::ui::win32::attachOwnedPopup)>);
