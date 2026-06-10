#ifndef TEXTURE_SYSTEM_H
#define TEXTURE_SYSTEM_H

#include "Graphics.h"
#include "Extern/stb/stb_rect_pack.h"

enum { TextureClass_Albedo, TextureClass_Normal, TextureClass_MetallicRoughness, TextureClass_Count };

// compressed pages keep the last mips as cpu shadows because block alignment
// cannot be guaranteed for region uploads at those sizes
#define TEXTURE_PAGE_TAIL_MIPS 3

typedef struct TexturePageClass_
{
    Texture       pages;                            // 2d array texture with layerCount layers
    u32           layerCount;                       // gpu layers, grows 1 -> 2 -> 4 -> 8
    u32           openPages;                        // pages with live packer state
    stbrp_context packer[TEXTURE_PAGE_LAYERS];      // persistent, packs incrementally across appends
    stbrp_node*   packerNodes[TEXTURE_PAGE_LAYERS];
    u8*           tailMips[TEXTURE_PAGE_LAYERS][TEXTURE_PAGE_TAIL_MIPS]; // compressed path only
} TexturePageClass;

// per scene texture state: page atlases, descriptors and materials.
// material slots are stable: bundle->materialOffset never moves once assigned,
// removal leaves holes that only TextureSystem_ResetPacking + re-append reclaim.
typedef struct TextureSystem_
{
    TextureDescriptor* descriptors;       // MAX_TEXTURE_DESCRIPTORS
    MaterialGPU*       materials;         // MAX_GPU_MATERIALS
    u32                numDescriptors;
    u32                materialWatermark; // highest used material slot + 1
    SDL_GPUBuffer*     descriptorBuffer;
    SDL_GPUBuffer*     materialBuffer;
    TexturePageClass   classes[TextureClass_Count];
    SDL_GPUTextureFormat albedoFormat;
    SDL_GPUTextureFormat rgFormat;
    u32                compressed;        // block compressed page path for the lifetime of the instance
} TextureSystem;

// creates the shared page copy compute pipelines, call once after GraphicsInit
void TextureSystem_InitDevice(void);
void TextureSystem_DestroyDevice(void);

void TextureSystem_Init(TextureSystem* ts);
void TextureSystem_Destroy(TextureSystem* ts);

// packs the bundle's images into the pages, appends descriptors and writes the bundle's
// materials at [bundle->materialOffset, +numMaterials). stagingTextures is bundle local:
// stagingTextures[i] holds image i (basis buffer for compressed pages, gpu handle otherwise).
// caller keeps ownership of the staging textures. out: 0 on failure
s32 TextureSystem_AppendBundle(TextureSystem* ts, const SceneBundle* bundle, const Texture* stagingTextures);

// clears the bundle's material slots to defaults. descriptor and page space of the
// bundle leak until TextureSystem_ResetPacking
void TextureSystem_RemoveBundle(TextureSystem* ts, const SceneBundle* bundle);

// drops all pages and descriptors so the caller can re-append every live bundle.
// materials and their offsets are preserved, re-appends rewrite them in place
void TextureSystem_ResetPacking(TextureSystem* ts);

// releases the gpu handles and cpu basis buffers of a texture range
void TextureSystem_ReleaseTextures(Texture* textures, u32 count);

#endif // TEXTURE_SYSTEM_H
