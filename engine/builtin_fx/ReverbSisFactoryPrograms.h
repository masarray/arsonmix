// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/arsonverb/include/arverb/Reverb.hpp"
#include <array>
namespace armix::builtin_fx {
inline constexpr int reverbSisPresetCount=16;
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
    "Album Ready Pop"
}};
struct ReverbSisFactoryProgram {int algorithm;std::array<float,arverb::parameterCount> values;};
// Mix, predelay, decay, size, diffusion, damping, low cut, high cut,
// modulation depth/rate, width, early, duck. Stable IDs and names.
inline constexpr std::array<ReverbSisFactoryProgram,reverbSisPresetCount> reverbSisFactoryPrograms{{
    {1, {{0.24f,28.0f,2.2f,0.89f,0.94f,10400.0f,145.0f,15500.0f,0.14f,0.28f,1.08f,0.44f,0.24f}}}, // Krenyess Vocal
    {1, {{0.22f,26.0f,1.85f,0.85f,0.9f,7600.0f,150.0f,12400.0f,0.1f,0.26f,1.06f,0.46f,0.26f}}}, // Sweet Karaoke
    {1, {{0.2f,26.0f,1.55f,0.85f,0.92f,10500.0f,165.0f,14800.0f,0.08f,0.3f,1.06f,0.42f,0.28f}}}, // Crisp Pop Plate
    {3, {{0.27f,42.0f,3.05f,1.4f,0.94f,7400.0f,155.0f,13500.0f,0.2f,0.28f,1.15f,0.34f,0.3f}}}, // Silky Ballad
    {2, {{0.2f,12.0f,1.05f,0.75f,0.76f,7400.0f,135.0f,11500.0f,0.05f,0.22f,1.0f,0.54f,0.24f}}}, // Tight Live Chamber
    {3, {{0.25f,38.0f,2.75f,1.32f,0.94f,9500.0f,145.0f,15500.0f,0.16f,0.27f,1.15f,0.34f,0.3f}}}, // Air Vocal Hall
    {1, {{0.23f,30.0f,2.2f,0.92f,0.9f,5300.0f,135.0f,8500.0f,0.12f,0.25f,1.06f,0.4f,0.23f}}}, // Warm Vintage Plate
    {3, {{0.28f,47.0f,3.65f,1.55f,0.93f,6600.0f,155.0f,11500.0f,0.2f,0.25f,1.17f,0.32f,0.3f}}}, // Large Stage Hall
    {3, {{0.2f,14.0f,1.25f,0.83f,0.74f,6900.0f,110.0f,10500.0f,0.06f,0.23f,1.05f,0.48f,0.1f}}}, // Acoustic Guitar Room
    {3, {{0.25f,25.0f,2.65f,1.35f,0.82f,5900.0f,95.0f,9800.0f,0.12f,0.2f,1.15f,0.38f,0.1f}}}, // Piano Concert Hall
    {2, {{0.19f,8.0f,0.55f,0.7f,0.55f,6500.0f,100.0f,10300.0f,0.02f,0.16f,0.96f,0.66f,0.06f}}}, // Drum Studio Room
    {3, {{0.27f,43.0f,4.4f,1.65f,0.9f,7000.0f,130.0f,12300.0f,0.28f,0.28f,1.2f,0.28f,0.12f}}}, // Keys Dream Hall
    {2, {{0.14f,27.0f,0.78f,0.8f,0.78f,8300.0f,215.0f,12800.0f,0.045f,0.22f,1.02f,0.4f,0.42f}}}, // Master Ceremony
    {1, {{0.21f,34.0f,2.35f,0.95f,0.94f,9800.0f,145.0f,15500.0f,0.12f,0.28f,1.1f,0.42f,0.28f}}}, // Album Ready Vocal
    {3, {{0.25f,48.0f,3.1f,1.35f,0.94f,8500.0f,155.0f,14500.0f,0.18f,0.24f,1.16f,0.34f,0.3f}}}, // Album Ready Ballad
    {1, {{0.19f,30.0f,1.75f,0.85f,0.92f,10500.0f,165.0f,15500.0f,0.09f,0.32f,1.07f,0.44f,0.26f}}}, // Album Ready Pop
}};
} // namespace armix::builtin_fx
