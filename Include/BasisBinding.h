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

// decodes one mip level of one image of a basis file to rgba32, caller frees with
// DeAllocateTLSFGlobal. out: NULL on failure
void* BasisuDecodeLevelRGBA(
    const void* basisu_data,
    uint64_t size,
    uint32_t imageIndex,
    uint32_t level,
    int* width,
    int* height);

// number of images (texture array layers) stored in the basis file, 0 on failure
uint32_t BasisuGetImageCount(const void* basisu_data, uint64_t size);

bool BasisuTranscodeImage(
    const void* basisu_data,
    uint64_t size,
    SDL_GPUTextureFormat targetFormat,
    BasisuTranscodedImage* outImage);

// same as BasisuTranscodeImage for one image (layer) of a texture array basis file
bool BasisuTranscodeImageLayer(
    const void* basisu_data,
    uint64_t size,
    SDL_GPUTextureFormat targetFormat,
    uint32_t imageIndex,
    BasisuTranscodedImage* outImage);

void BasisuFreeTranscodedImage(BasisuTranscodedImage* image);


#ifdef __cplusplus
}
#endif

#endif
