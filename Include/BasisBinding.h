#ifndef BASIS_BINDING
#define BASIS_BINDING

#include <stdint.h>
#include <stdbool.h>
#include <SDL3/SDL_gpu.h>

#ifndef BASISU_MAX_TRANSCODED_MIPS
#define BASISU_MAX_TRANSCODED_MIPS 16
#endif

typedef struct BasisuTranscodedLevel_
{
    uint32_t width;
    uint32_t height;
    uint32_t blocks;
    uint32_t offset;
    uint32_t size;
} BasisuTranscodedLevel;

typedef struct BasisuTranscodedImage_
{
    void* data;
    uint32_t dataSize;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    uint32_t bytesPerBlock;
    SDL_GPUTextureFormat format;
    BasisuTranscodedLevel levels[BASISU_MAX_TRANSCODED_MIPS];
} BasisuTranscodedImage;

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
    uint32_t* mipLevels,
    bool isNormal, 
    bool isMetallicRoughness);

void* BasisuDecodeImageRGBA(
    const void* basisu_data,
    uint64_t size,
    int* width,
    int* height);

bool BasisuTranscodeImage(
    const void* basisu_data,
    uint64_t size,
    SDL_GPUTextureFormat targetFormat,
    BasisuTranscodedImage* outImage);

void BasisuFreeTranscodedImage(BasisuTranscodedImage* image);


#ifdef __cplusplus
}
#endif

#endif
