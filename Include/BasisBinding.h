#ifndef BASIS_BINDING
#define BASIS_BINDING

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void BasisuSetup(void);
void BasisuShutdown(void);

SDL_GPUTexture* BasisuMakeImage(
    const void* basisu_data, 
    uint64_t size, 
    int* width,
    int* height, 
    SDL_GPUTextureFormat* format,
    bool isNormal, 
    bool isMetallicRoughness);

void* BasisuDecodeImageRGBA(
    const void* basisu_data,
    uint64_t size,
    int* width,
    int* height);


#ifdef __cplusplus
}
#endif

#endif
