// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/arsonverb/include/arverb/Reverb.hpp"
#include <array>
namespace armix::builtin_fx {
inline constexpr int reverbSisPresetCount=19;
inline constexpr int reverbSisCustomItem=reverbSisPresetCount+1;
inline constexpr std::array<const char*,reverbSisPresetCount> vocalReverbPresetNames{{
    "Krenyess Vocal",
    "Sweet Karaoke",
    "Crisp Pop Plate",
    "Silky Ballad",
    "Tight Live Chamber",
    "Air Vocal Hall",
    "Warm Vintage Plate",
    "Large Stage Hall",
    "Acoustic Guitar Room",
    "Piano Concert Hall",
    "Drum Studio Room",
    "Keys Dream Hall",
    "Master Ceremony",
    "Album Ready Vocal",
    "Album Ready Ballad",
    "Album Ready Pop",
    "Live Vocal Focus",
    "Singer Comfort",
    "Live Ballad"
}};
struct ReverbSisFactoryProgram {int algorithm;std::array<float,arverb::parameterCount> values;int voicing;};
// Mix, predelay, decay, size, diffusion, damping, low cut, high cut,
// modulation depth/rate, width, early, duck. Stable IDs and names.
inline constexpr std::array<ReverbSisFactoryProgram,reverbSisPresetCount> reverbSisFactoryPrograms{{
    {1, {{0.24f,32.0f,2.2f,0.89f,0.94f,10400.0f,130.0f,15500.0f,0.12f,0.28f,1.08f,0.4f,0.28f}}, 5}, // Krenyess Vocal
    {1, {{0.22f,32.0f,1.85f,0.85f,0.9f,8200.0f,140.0f,12600.0f,0.09f,0.26f,1.06f,0.42f,0.28f}}, 5}, // Sweet Karaoke
    {1, {{0.2f,30.0f,1.5f,0.82f,0.92f,11000.0f,155.0f,14800.0f,0.07f,0.3f,1.06f,0.39f,0.3f}}, 5}, // Crisp Pop Plate
    {3, {{0.27f,48.0f,3.1f,1.4f,0.94f,7800.0f,145.0f,13500.0f,0.18f,0.28f,1.15f,0.31f,0.32f}}, 5}, // Silky Ballad
    {2, {{0.2f,12.0f,1.0f,0.75f,0.75f,7400.0f,135.0f,11500.0f,0.045f,0.22f,1.0f,0.52f,0.24f}}, 5}, // Tight Live Chamber
    {3, {{0.25f,44.0f,2.75f,1.32f,0.94f,9800.0f,135.0f,15500.0f,0.14f,0.27f,1.15f,0.31f,0.32f}}, 5}, // Air Vocal Hall
    {1, {{0.23f,34.0f,2.2f,0.92f,0.9f,5300.0f,130.0f,8500.0f,0.1f,0.25f,1.06f,0.38f,0.25f}}, 5}, // Warm Vintage Plate
    {3, {{0.28f,52.0f,3.65f,1.55f,0.93f,6900.0f,145.0f,11800.0f,0.18f,0.25f,1.17f,0.3f,0.32f}}, 5}, // Large Stage Hall
    {2, {{0.2f,14.0f,1.18f,0.83f,0.74f,6900.0f,110.0f,10500.0f,0.055f,0.23f,1.05f,0.48f,0.1f}}, 4}, // Acoustic Guitar Room
    {3, {{0.25f,25.0f,2.65f,1.35f,0.82f,5900.0f,95.0f,9800.0f,0.12f,0.2f,1.15f,0.38f,0.1f}}, 4}, // Piano Concert Hall
    {2, {{0.19f,8.0f,0.55f,0.7f,0.55f,6500.0f,100.0f,10300.0f,0.02f,0.16f,0.96f,0.66f,0.06f}}, 4}, // Drum Studio Room
    {3, {{0.27f,43.0f,4.4f,1.65f,0.9f,7000.0f,130.0f,12300.0f,0.28f,0.28f,1.2f,0.28f,0.12f}}, 4}, // Keys Dream Hall
    {2, {{0.14f,27.0f,0.78f,0.8f,0.78f,8300.0f,215.0f,12800.0f,0.045f,0.22f,1.02f,0.4f,0.42f}}, 4}, // Master Ceremony
    {1, {{0.22f,38.0f,2.35f,0.95f,0.94f,10000.0f,135.0f,15500.0f,0.11f,0.28f,1.1f,0.38f,0.32f}}, 5}, // Album Ready Vocal
    {3, {{0.25f,52.0f,3.1f,1.35f,0.94f,8800.0f,145.0f,14500.0f,0.16f,0.24f,1.16f,0.31f,0.32f}}, 5}, // Album Ready Ballad
    {1, {{0.2f,32.0f,1.75f,0.85f,0.92f,10800.0f,155.0f,15500.0f,0.08f,0.32f,1.07f,0.4f,0.3f}}, 5}, // Album Ready Pop
    {2, {{.18f,20.f,.95f,.75f,.80f,8200.f,135.f,13500.f,.045f,.22f,.95f,.48f,.65f}}, 5}, // Live Vocal Focus
    {1, {{.23f,30.f,1.65f,.87f,.92f,9800.f,135.f,15000.f,.08f,.26f,1.05f,.44f,.68f}}, 5}, // Singer Comfort
    {3, {{.26f,42.f,2.60f,1.30f,.94f,8500.f,145.f,14500.f,.15f,.24f,1.15f,.37f,.75f}}, 5}, // Live Ballad
}};
} // namespace armix::builtin_fx
