#include "arverb/arverb_c.h"
#include "arverb/Reverb.hpp"
#include <new>
#include <cmath>

static_assert(arverb::parameterCount == ARVERB_PARAMETER_COUNT,
              "C ABI enumeration drift: add a versioned extension, do not renumber");
struct arverb_handle { arverb::Engine engine; };

extern "C" {
arverb_handle* arverb_create(void) {
    try { return new arverb_handle; } catch (...) { return nullptr; }
}
void arverb_destroy(arverb_handle* h) { delete h; }
int arverb_prepare(arverb_handle* h, double fs) {
    if (!h) return 0;
    try { h->engine.prepare(fs); return 1; } catch (...) { return 0; }
}
void arverb_reset(arverb_handle* h) { if (h) h->engine.reset(); }
int arverb_set_parameter(arverb_handle* h, uint32_t id, float value) {
    if (!h || id >= arverb::parameterCount || !std::isfinite(value)) return 0;
    h->engine.setParameter(static_cast<arverb::Parameter>(id), value);
    return 1;
}
float arverb_get_parameter(const arverb_handle* h, uint32_t id) {
    return h && id < arverb::parameterCount ?
        h->engine.getParameter(static_cast<arverb::Parameter>(id)) : 0.0f;
}
int arverb_load_preset(arverb_handle* h, uint32_t id) {
    if (!h || id > static_cast<uint32_t>(arverb::Preset::AiryBallad)) return 0;
    h->engine.loadPreset(static_cast<arverb::Preset>(id));
    return 1;
}
void arverb_process(arverb_handle* h, const float* l, const float* r,
                    float* outL, float* outR, uint32_t n) {
    if (h) h->engine.process(l, r, outL, outR, n);
}
#ifndef ARVERB_BUILD_VERSION
#error "Build the AR Reverb core using the CMake project so the ABI version is consistent"
#endif
const char* arverb_version(void) { return ARVERB_BUILD_VERSION; }
uint32_t arverb_abi_version(void) { return ARVERB_ABI_VERSION; }
uint32_t arverb_parameter_count(void) {
    return static_cast<uint32_t>(arverb::parameterCount);
}
}
