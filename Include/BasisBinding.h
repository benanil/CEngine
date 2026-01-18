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
    SDL_GPUDevice* gpuDevice,
    bool isNormal, 
    bool isMetallicRoughness);


#ifdef __cplusplus
}
#endif

#endif
