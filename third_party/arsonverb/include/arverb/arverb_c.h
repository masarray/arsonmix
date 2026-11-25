#ifndef ARVERB_C_H
#define ARVERB_C_H

#include <stdint.h>
#ifdef _WIN32
#  if defined(ARVERB_BUILD_SHARED)
#    define ARVERB_API __declspec(dllexport)
#  elif defined(ARVERB_USE_SHARED)
#    define ARVERB_API __declspec(dllimport)
#  else
#    define ARVERB_API
#  endif
#else
#  if defined(ARVERB_BUILD_SHARED)
#    define ARVERB_API __attribute__((visibility("default")))
#  else
#    define ARVERB_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arverb_handle arverb_handle;

/* ABI major 1: opaque handle; existing function signatures are immutable.
 * New functions may be added without bumping this version.
 * arverb_version() reports implementation version, not ABI compatibility.
 */
#define ARVERB_ABI_VERSION 1u
#define ARVERB_PARAMETER_COUNT 13u

enum arverb_parameter {
    ARVERB_MIX = 0, ARVERB_PREDELAY_MS, ARVERB_DECAY_SECONDS,
    ARVERB_SIZE, ARVERB_DIFFUSION, ARVERB_DAMPING_HZ,
    ARVERB_LOW_CUT_HZ, ARVERB_HIGH_CUT_HZ, ARVERB_MOD_DEPTH_MS,
    ARVERB_MOD_RATE_HZ, ARVERB_WIDTH, ARVERB_EARLY_LEVEL,
    ARVERB_DUCK_AMOUNT
};

enum arverb_preset {
    ARVERB_VOCAL_PLATE = 0, ARVERB_VOCAL_CHAMBER,
    ARVERB_MUSIC_HALL, ARVERB_AIRY_BALLAD
};

ARVERB_API arverb_handle* arverb_create(void);
ARVERB_API void arverb_destroy(arverb_handle* handle);
ARVERB_API int arverb_prepare(arverb_handle* handle, double sample_rate);
ARVERB_API void arverb_reset(arverb_handle* handle);
ARVERB_API int arverb_set_parameter(arverb_handle* handle, uint32_t parameter_id, float value);
ARVERB_API float arverb_get_parameter(const arverb_handle* handle, uint32_t parameter_id);
ARVERB_API int arverb_load_preset(arverb_handle* handle, uint32_t preset_id);
ARVERB_API void arverb_process(arverb_handle* handle,
                               const float* input_left, const float* input_right,
                               float* output_left, float* output_right,
                               uint32_t frames);
ARVERB_API const char* arverb_version(void);
ARVERB_API uint32_t arverb_abi_version(void);
ARVERB_API uint32_t arverb_parameter_count(void);

#ifdef __cplusplus
}
#endif
#endif
