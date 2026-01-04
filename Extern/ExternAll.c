// Add Windows.h at the VERY TOP
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#endif

// SOKOL
#if __GNUC__
    #define SOKOL_ASSERT(c) if (!(c)) { __builtin_trap(); }
#elif _MSC_VER
    #define SOKOL_ASSERT(c) if (!(c)) { __debugbreak(); }
#else
    #define SOKOL_ASSERT(c) if (!(c)) { *(volatile int *)0 = 0; }
#endif

#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_D3D11
#define SOKOL_APP_IMPL
#define SOKOL_TIME_IMPL

#include "sokol/sokol_time.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_glue.h"

#include "ufbx.c"

#define XXH_INLINE_ALL
#include "xxhash.h"
#include "hashmap.c"

uint64_t hashmap_xxhash3(const void *data, size_t len, uint64_t seed0, uint64_t seed1)
{
    (void)seed1;
    return XXH3_64bits_withSeed(data, len, seed0);
}