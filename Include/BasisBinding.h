#ifndef BASIS_BINDING
#define BASIS_BINDING

#include <stdint.h>
#include <stdbool.h>
#include "Extern/sokol/sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

void BasisuSetup(void);
void BasisuShutdown(void);
void BasisuMakeImage(void* basisu_data, uint64_t size, 
                     int* width, int* height, 
                     sg_pixel_format* format, void** buffer, sg_image* handle,
                     bool isNormal, bool isMetallicRoughness);

sg_image_desc BasisuTranscode(void* basisu_data, uint64_t size, bool isNormal, bool isMetallicRoughness);
void BasisuFree(const sg_image_desc* desc);

#ifdef __cplusplus
}
#endif

#endif
