#include "arverb/Reverb.hpp"

namespace arverb {
namespace {
// 32 original factory programs for the current single-algorithm DSP.
constexpr std::array<PresetInfo, presetCount> catalog {{
    {"Vocal Plate", "Vocal", {{0.18f, 43.0f, 1.7f, 0.85f, 0.85f, 6900.0f, 260.0f, 9200.0f, 0.12f, 0.28f, 1.12f, 0.2f, 0.45f}}}, // VocalPlate
    {"Vocal Chamber", "Vocal", {{0.16f, 28.0f, 1.25f, 0.78f, 0.64f, 4800.0f, 250.0f, 7800.0f, 0.09f, 0.24f, 1.0f, 0.4f, 0.4f}}}, // VocalChamber
    {"Music Hall", "Music", {{0.19f, 34.0f, 2.5f, 1.3f, 0.72f, 6100.0f, 175.0f, 10600.0f, 0.22f, 0.34f, 1.18f, 0.28f, 0.22f}}}, // MusicHall
    {"Airy Ballad", "Vocal", {{0.23f, 64.0f, 3.7f, 1.64f, 0.8f, 5500.0f, 320.0f, 8000.0f, 0.32f, 0.41f, 1.26f, 0.14f, 0.52f}}}, // AiryBallad
    {"Vocal Plate Clean", "Vocal", {{0.17f, 38.0f, 2.05f, 0.89f, 0.86f, 8400.0f, 200.0f, 12400.0f, 0.12f, 0.24f, 1.14f, 0.18f, 0.42f}}}, // VocalPlateClean
    {"Vocal Plate Warm", "Vocal", {{0.2f, 45.0f, 2.35f, 0.92f, 0.88f, 5300.0f, 270.0f, 7600.0f, 0.16f, 0.28f, 1.12f, 0.18f, 0.4f}}}, // VocalPlateWarm
    {"Vocal Chamber Tight", "Vocal", {{0.14f, 25.0f, 0.92f, 0.72f, 0.65f, 6800.0f, 220.0f, 9900.0f, 0.07f, 0.2f, 1.0f, 0.36f, 0.45f}}}, // VocalChamberTight
    {"Vocal Hall Pop", "Vocal", {{0.18f, 48.0f, 2.55f, 1.35f, 0.77f, 7300.0f, 260.0f, 10600.0f, 0.18f, 0.31f, 1.22f, 0.22f, 0.48f}}}, // VocalHallPop
    {"Vocal Hall Ballad", "Vocal", {{0.22f, 70.0f, 4.1f, 1.7f, 0.83f, 5700.0f, 310.0f, 8600.0f, 0.29f, 0.25f, 1.3f, 0.17f, 0.52f}}}, // VocalHallBallad
    {"Vocal Wide Air", "Vocal", {{0.17f, 61.0f, 2.65f, 1.55f, 0.79f, 8500.0f, 350.0f, 13000.0f, 0.21f, 0.27f, 1.42f, 0.12f, 0.51f}}}, // VocalWideAir
    {"Vocal Space Short", "Vocal", {{0.12f, 16.0f, 0.66f, 0.73f, 0.58f, 8000.0f, 200.0f, 11800.0f, 0.05f, 0.19f, 1.05f, 0.47f, 0.46f}}}, // VocalSpaceShort
    {"Snare Plate Classic", "Music", {{0.18f, 13.0f, 1.42f, 0.93f, 0.91f, 8800.0f, 320.0f, 13200.0f, 0.06f, 0.18f, 1.08f, 0.3f, 0.15f}}}, // SnarePlateClassic
    {"Drum Room Tight", "Music", {{0.13f, 8.0f, 0.52f, 0.7f, 0.44f, 5900.0f, 180.0f, 9300.0f, 0.02f, 0.16f, 0.94f, 0.55f, 0.12f}}}, // DrumRoomTight
    {"Drum Room Warm", "Music", {{0.15f, 12.0f, 0.94f, 0.78f, 0.53f, 4800.0f, 130.0f, 7700.0f, 0.04f, 0.22f, 1.0f, 0.52f, 0.1f}}}, // DrumRoomWarm
    {"Guitar Short Space", "Music", {{0.16f, 23.0f, 1.35f, 0.83f, 0.66f, 6900.0f, 200.0f, 9600.0f, 0.08f, 0.23f, 1.12f, 0.32f, 0.16f}}}, // GuitarShortSpace
    {"Guitar Dream Hall", "Music", {{0.25f, 57.0f, 4.65f, 1.82f, 0.88f, 6400.0f, 240.0f, 10100.0f, 0.38f, 0.29f, 1.38f, 0.13f, 0.12f}}}, // GuitarDreamHall
    {"Piano Hall Warm", "Music", {{0.2f, 31.0f, 2.85f, 1.42f, 0.74f, 5600.0f, 135.0f, 8900.0f, 0.16f, 0.2f, 1.22f, 0.28f, 0.12f}}}, // PianoHallWarm
    {"Keys Stereo Wash", "Music", {{0.26f, 54.0f, 5.45f, 1.78f, 0.85f, 6300.0f, 210.0f, 11200.0f, 0.42f, 0.31f, 1.48f, 0.14f, 0.14f}}}, // KeysStereoWash
    {"Strings Lush Hall", "Music", {{0.22f, 43.0f, 3.95f, 1.66f, 0.88f, 5900.0f, 160.0f, 9500.0f, 0.27f, 0.18f, 1.29f, 0.18f, 0.11f}}}, // StringsLushHall
    {"Lead Synth Space", "Music", {{0.19f, 36.0f, 2.28f, 1.18f, 0.7f, 7900.0f, 290.0f, 12700.0f, 0.23f, 0.46f, 1.35f, 0.19f, 0.26f}}}, // LeadSynthSpace
    {"Vintage Plate 70s", "Vintage", {{0.22f, 38.0f, 2.18f, 0.92f, 0.87f, 3300.0f, 330.0f, 5900.0f, 0.09f, 0.17f, 1.05f, 0.16f, 0.34f}}}, // VintagePlate70s
    {"Vintage Hall 80s", "Vintage", {{0.24f, 44.0f, 4.05f, 1.54f, 0.83f, 5600.0f, 230.0f, 9200.0f, 0.37f, 0.33f, 1.26f, 0.19f, 0.28f}}}, // VintageHall80s
    {"Dark Vintage Chamber", "Vintage", {{0.2f, 27.0f, 1.72f, 0.81f, 0.67f, 2800.0f, 310.0f, 4800.0f, 0.12f, 0.25f, 1.03f, 0.35f, 0.26f}}}, // DarkVintageChamber
    {"Retro Broadcast", "Vintage", {{0.12f, 15.0f, 0.82f, 0.72f, 0.52f, 3900.0f, 360.0f, 6500.0f, 0.04f, 0.21f, 0.88f, 0.41f, 0.47f}}}, // RetroBroadcast
    {"Classic Studio Room", "Vintage", {{0.15f, 11.0f, 1.16f, 0.85f, 0.49f, 5400.0f, 120.0f, 8100.0f, 0.05f, 0.16f, 0.99f, 0.58f, 0.18f}}}, // ClassicStudioRoom
    {"Vintage Mod Hall", "Vintage", {{0.24f, 49.0f, 4.65f, 1.62f, 0.81f, 4500.0f, 250.0f, 7900.0f, 0.48f, 0.38f, 1.23f, 0.13f, 0.24f}}}, // VintageModHall
    {"Air Bloom", "Ambient", {{0.25f, 64.0f, 5.15f, 1.76f, 0.86f, 7700.0f, 310.0f, 12100.0f, 0.38f, 0.27f, 1.43f, 0.12f, 0.23f}}}, // AirBloom
    {"Long Dream Hall", "Ambient", {{0.31f, 82.0f, 8.3f, 1.93f, 0.91f, 5900.0f, 330.0f, 10200.0f, 0.5f, 0.24f, 1.5f, 0.11f, 0.1f}}}, // LongDreamHall
    {"Velvet Space", "Ambient", {{0.26f, 71.0f, 6.25f, 1.84f, 0.91f, 4300.0f, 370.0f, 6900.0f, 0.32f, 0.17f, 1.38f, 0.1f, 0.12f}}}, // VelvetSpace
    {"Wide Motion Cloud", "Ambient", {{0.29f, 69.0f, 7.5f, 1.95f, 0.87f, 7200.0f, 390.0f, 11500.0f, 0.72f, 0.4f, 1.53f, 0.12f, 0.16f}}}, // WideMotionCloud
    {"Deep Atmosphere", "Ambient", {{0.27f, 85.0f, 9.4f, 1.98f, 0.9f, 4000.0f, 270.0f, 6600.0f, 0.63f, 0.23f, 1.45f, 0.09f, 0.1f}}}, // DeepAtmosphere
    {"Slow Cinematic Wash", "Ambient", {{0.29f, 110.0f, 10.8f, 1.98f, 0.94f, 5100.0f, 420.0f, 9400.0f, 0.8f, 0.12f, 1.58f, 0.08f, 0.08f}}}, // SlowCinematicWash
}};
static_assert (catalog.size() == presetCount, "factory count mismatch");
} // namespace

Algorithm presetAlgorithm (Preset preset) noexcept {
    switch (preset) {
        case Preset::VocalPlate: case Preset::VocalPlateClean:
        case Preset::VocalPlateWarm: case Preset::SnarePlateClassic:
        case Preset::VintagePlate70s: return Algorithm::Plate;
        case Preset::VocalChamber: case Preset::VocalChamberTight:
        case Preset::DrumRoomTight: case Preset::DrumRoomWarm:
        case Preset::DarkVintageChamber: case Preset::RetroBroadcast:
        case Preset::ClassicStudioRoom: return Algorithm::Chamber;
        default: return Algorithm::Hall;
    }
}
const PresetInfo& presetInfo (Preset preset) noexcept {
    const auto index = static_cast<std::size_t> (preset);
    return catalog[index < catalog.size() ? index : 0];
}
std::array<float, parameterCount> presetValues (Preset preset) noexcept {
    return presetInfo (preset).values;
}
void Engine::loadPreset (Preset preset) noexcept {
    const auto& values = presetInfo (preset).values;
    for (std::size_t i = 0; i < parameterCount; ++i)
        setParameter (static_cast<Parameter> (i), values[i]);
}
} // namespace arverb
