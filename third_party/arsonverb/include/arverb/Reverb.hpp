#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arverb {

static_assert(std::atomic<float>::is_always_lock_free,
              "This real-time engine requires lock-free float parameter atomics");

// Stable numeric IDs for host automation, presets and the C ABI.
enum class Parameter : std::uint32_t {
    Mix = 0,
    PreDelayMs,
    DecaySeconds,
    Size,
    Diffusion,
    DampingHz,
    LowCutHz,
    HighCutHz,
    ModDepthMs,
    ModRateHz,
    Width,
    EarlyLevel,
    DuckAmount,
    Count
};

struct ParameterInfo {
    const char* id;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float defaultValue;
};

constexpr std::size_t parameterCount = static_cast<std::size_t>(Parameter::Count);
// Independent from the 13 original scalar parameter IDs and C ABI.
enum class Algorithm : std::uint32_t { Classic=0, Plate=1, Chamber=2, Hall=3 };
constexpr unsigned algorithmCount = 4;
const char* algorithmName (Algorithm) noexcept;
const ParameterInfo& parameterInfo(Parameter param) noexcept;

// First four enum values preserved for backward-compatible core clients.
enum class Preset : std::uint32_t {
    VocalPlate,
    VocalChamber,
    MusicHall,
    AiryBallad,
    VocalPlateClean,
    VocalPlateWarm,
    VocalChamberTight,
    VocalHallPop,
    VocalHallBallad,
    VocalWideAir,
    VocalSpaceShort,
    SnarePlateClassic,
    DrumRoomTight,
    DrumRoomWarm,
    GuitarShortSpace,
    GuitarDreamHall,
    PianoHallWarm,
    KeysStereoWash,
    StringsLushHall,
    LeadSynthSpace,
    VintagePlate70s,
    VintageHall80s,
    DarkVintageChamber,
    RetroBroadcast,
    ClassicStudioRoom,
    VintageModHall,
    AirBloom,
    LongDreamHall,
    VelvetSpace,
    WideMotionCloud,
    DeepAtmosphere,
    SlowCinematicWash,
    Count
};
constexpr std::size_t presetCount = static_cast<std::size_t> (Preset::Count);
struct PresetInfo {
    const char* name;
    const char* category;
    std::array<float, parameterCount> values; // 13 native parameter units
};
const PresetInfo& presetInfo (Preset preset) noexcept;
std::array<float, parameterCount> presetValues (Preset preset) noexcept;
Algorithm presetAlgorithm (Preset preset) noexcept;

// The C++ DSP core has no dependency on VST, JUCE, audio I/O, UI or operating system.
// A single Engine instance belongs to exactly one audio thread.
// prepare/reset must be called with audio processing suspended. SetParameter is thread-safe.
class Engine final {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocation and configuration ONLY here, never inside process().
    // Supported sample rates: 8 kHz - 192 kHz. Throws std::invalid_argument for invalid rates.
    void prepare(double sampleRate);
    // Host-selected voicing, immutable during processing. Existing core/C ABI
    // keeps original behavior; Reverb-Sis selects the calibrated musical return.
    void prepare(double sampleRate, bool musicalReturn);
    // Explicit stopped-only send/return render mode. Dry gain is identically
    // zero from the first sample; saved/automated insert Mix cannot leak dry.
    // The host must own return gain, wet-only bypass, and mode persistence.
    // Existing prepare overloads restore normal insert behavior.
    void prepareWetReturn(double sampleRate, bool musicalReturn);
    void reset() noexcept;
    bool prepared() const noexcept;

    // Thread-safe, lock-free target updates; parameters become active at next process() call.
    void setParameter(Parameter parameter, float value) noexcept;
    float getParameter(Parameter parameter) const noexcept;
    void loadPreset(Preset preset) noexcept;
    void setAlgorithm(Algorithm algorithm) noexcept;
    Algorithm getAlgorithm() const noexcept;
    // Native opt-in voicing; independent of the stable 13 parameter IDs.
    void setAlbumReady(bool enabled) noexcept;
    // 0 original, 1 historical Album, 2 Signature, 3 Vocal Anchor, 4 Studio Finish, 5 Singer Feel.
    void setVoicing(unsigned profile) noexcept;

    // Planar stereo f32. In-place per channel supported. Avoid cross-channel buffer aliases.
    // All input/output pointers must be valid when frames > 0. No allocation, locks or I/O.
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight, std::size_t frames) noexcept;

private:
    void prepareMode(double sampleRate, bool musicalReturn, bool wetOnly);
    struct State;
    std::unique_ptr<State> state_;
    std::array<std::atomic<float>, parameterCount> parameters_;
    std::atomic<std::uint32_t> algorithm_ {0};
    std::atomic<unsigned> voicing_{0};
};

}  // namespace arverb
