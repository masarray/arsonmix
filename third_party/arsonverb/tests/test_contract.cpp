#include <arverb/Reverb.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

static void require(bool okay, const char* why) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); }
}

int main() {
    std::set<std::string> unique;
    for (std::uint32_t i = 0; i < arverb::parameterCount; ++i) {
        const auto p = static_cast<arverb::Parameter>(i);
        const auto& info = arverb::parameterInfo(p);
        require(info.id && std::strlen(info.id) > 0, "missing ID");
        require(info.name && std::strlen(info.name) > 0, "missing name");
        require(unique.insert(info.id).second, "duplicate ID");
        require(info.minimum < info.maximum, "invalid range");
        require(info.defaultValue >= info.minimum && info.defaultValue <= info.maximum, "default bounds");
    }
    require(unique.find("mix") != unique.end(), "missing mix");
    require(unique.find("predelay") != unique.end(), "missing predelay");
    require(unique.find("decay") != unique.end(), "missing decay");
    // Automation ID stability: changing these strings breaks existing DAW sessions.
    constexpr std::array<const char*, 13> stableIds {
        "mix", "predelay", "decay", "size", "diffusion", "damping", "lowcut",
        "highcut", "moddepth", "modrate", "width", "early", "duck"
    };
    static_assert(stableIds.size() == arverb::parameterCount, "Append IDs; don't renumber");
    for (std::size_t i = 0; i < stableIds.size(); ++i)
        require(std::strcmp(stableIds[i], arverb::parameterInfo(static_cast<arverb::Parameter>(i)).id) == 0, "automation ID changed");
    static_assert (arverb::presetCount == 32, "32 original factory presets required");
    require (arverb::algorithmCount == 4, "P4 four algorithm choices");
    require (arverb::presetAlgorithm(arverb::Preset::VocalPlate) == arverb::Algorithm::Plate,
             "Plate program should have genuine Plate algorithm");
    require (arverb::presetAlgorithm(arverb::Preset::VocalChamber) == arverb::Algorithm::Chamber,
             "Chamber program should have genuine Chamber algorithm");
    require (arverb::presetAlgorithm(arverb::Preset::MusicHall) == arverb::Algorithm::Hall,
             "Hall program should have genuine Hall algorithm");
    std::set<std::string> names;
    arverb::Engine engine;
    engine.prepare (48000.0); // Single prep for all programs, economizes CI.
    for (std::size_t n = 0; n < arverb::presetCount; ++n) {
        const auto preset = static_cast<arverb::Preset> (n);
        const auto& record = arverb::presetInfo (preset);
        require (record.name && std::strlen (record.name) > 0, "missing preset name");
        require (names.insert (record.name).second, "duplicate factory preset");
        require (record.category && std::strlen (record.category) > 0,
                 "missing category");
        engine.reset();
        engine.loadPreset (preset);
        const auto values = arverb::presetValues (preset);
        for (std::size_t i = 0; i < arverb::parameterCount; ++i) {
            const auto param = static_cast<arverb::Parameter> (i);
            const auto& info = arverb::parameterInfo (param);
            const auto v = engine.getParameter (param);
            require (v == record.values[i] && v == values[i],
                     "engine/catalog mismatch");
            require (std::isfinite (v), "nonfinite factory value");
            require (v >= info.minimum && v <= info.maximum,
                     "factory value outside metadata limits");
        }
        std::array<float,128> left{}, right{};
        left[0] = right[0] = 0.5f;
        engine.process (left.data(),right.data(),left.data(),right.data(),left.size());
        for (float v : left) require (std::isfinite (v), "nonfinite left factory");
        for (float v : right) require (std::isfinite (v), "nonfinite right factory");
    }
    require (std::strcmp(arverb::presetInfo(arverb::Preset::VocalPlate).name,
                         "Vocal Plate") == 0, "old factory name changed");
    require (arverb::presetValues(static_cast<arverb::Preset>(10000)) ==
             arverb::presetValues(arverb::Preset::VocalPlate),
             "invalid index should return the first safe preset");
    std::cout << "Parameter ID and preset contract OK\n";
}
