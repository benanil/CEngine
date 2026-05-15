#ifndef _H_GRAPHICS_
#define _H_GRAPHICS_

#include <SDL3/SDL_gpu.h>
#include "Math/Half.h"
#include "Math/Vector.h"
#include "GLTFParser.h"

#define CHECK_CREATE(var, thing) { if (!(var)) { AX_ERROR("Failed to create %s: %s", thing, SDL_GetError()); Quit(2); } }

#define MAX_VERTEX 1000000
#define MAX_INDEX (MAX_VERTEX * 5)

#define TEXTURE_PAGE_SIZE 4096
#define TEXTURE_PAGE_LAYERS 4
#define MAX_SCENE_TEXTURES 512
#define MAX_TEXTURE_DESCRIPTORS 1024
#define MAX_GPU_MATERIALS 1024

#if defined(__cplusplus)
extern "C" {
#endif

enum TexFlags_
{
    TexFlags_None                = 0,
    TexFlags_MipMap              = 1,
    TexFlags_DontDeleteCPUBuffer = 2,

};
typedef s32 TexFlags;

enum GraphicType_
{
    GraphicType_Byte, // -> 0x1400 in opengl 
    GraphicType_UnsignedByte, 
    GraphicType_Short,
    GraphicType_UnsignedShort, 
    GraphicType_i32,
    GraphicType_Unsignedi32,
    GraphicType_Float,
    GraphicType_TwoByte,
    GraphicType_ThreeByte,
    GraphicType_FourByte,
    GraphicType_Double,
    GraphicType_Half, // -> 0x140B in opengl
    GraphicType_XYZ10W2, // GL_i32_2_10_10_10_REV

    GraphicType_Vector2f,
    GraphicType_Vector3f,
    GraphicType_Vector4f,

    GraphicType_Vector2i,
    GraphicType_Vector3i,
    GraphicType_Vector4i,
    // matrix types
    GraphicType_M22,
    GraphicType_M33,
    GraphicType_M44,

    GraphicType_NormalizeBit = 1 << 31
};
typedef s32 GraphicType;

// https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
typedef struct AVertex_
{
    float3 position;
    u32 octTbn;
    u32 texCoord; // half2
} AVertex;

typedef struct ASkinedVertex_
{
    u32 positionXY;
    u32 positionZW;
    u32 octTbn;   // XY11Z10
    u32 texCoord; // half2
    u32 joints;   // rgb8u
    u32 weights;  // rgb8u
} ASkinedVertex;

// for sizeof only
typedef struct ALineVertex_
{
    f32 x, y, z;
    u32 color;
} ALineVertex;

typedef struct GPUMesh_
{
    s32 numVertex, numIndex;
    // unsigned because opengl accepts unsigned
    u32 vertexLayoutHandle;
    u32 indexHandle;
    u32 indexType;  // ui3232, ui3264. GL_BYTE + indexType
    u32 vertexHandle; // opengl handles for, POSITION, TexCoord...
    // usefull for knowing which attributes are there
    // POSITION, TexCoord... AAttribType_ bitmask
    s32 attributes;
    s32 stride; // size of an vertex of the mesh
    
    void* vertices;
    void* indices;
} GPUMesh;

typedef struct Texture_
{
    s32 width, height;
    SDL_GPUTexture* handle;
    SDL_GPUTextureFormat format;
    void* buffer;
    u64 bufferSize;
    u32 channels;
    u32 type;
    u32 mipLevels;
} Texture;

typedef struct TextureDescriptor_
{
    u32 pageIndex;
    u32 flags;
    float2 uvScale;
    float2 uvBias;
} TextureDescriptor;

typedef struct MaterialGPU_
{
    u32 albedoDescriptor;
    u32 normalDescriptor;
    u32 metallicRoughnessDescriptor;
    u32 flags;
    u32 baseColorFactor;
    u32 metallicRoughnessFactor;
    u32 padding[2];
} MaterialGPU;

typedef struct WindowState
{
    SDL_GPUTexture* tex_depth, *tex_msaa, *tex_resolve, *tex_color, *tex_post;
    u32 prev_drawablew, prev_drawableh;
} WindowState;

typedef struct RenderSetBuffers_
{
    SDL_GPUBuffer* primitiveGroup;
    SDL_GPUBuffer* drawDenseIndices;
    SDL_GPUBuffer* drawArgs;
    SDL_GPUBuffer* denseToPrimitive;
    SDL_GPUBuffer* sparseToDense;
    SDL_GPUBuffer* entity;
    SDL_GPUBuffer* visibleDenseIndices;
    SDL_GPUBuffer* visibilityMask;
    SDL_GPUBuffer* visibleCount;
    SDL_GPUBuffer* dispatchArgs;
} RenderSetBuffers;

typedef struct RenderState
{
    SDL_GPUGraphicsPipeline* skinnedPipeline;
    SDL_GPUGraphicsPipeline* surfacePipeline;
    SDL_GPUGraphicsPipeline* linePipeline;
    SDL_GPUSampler*          sampler;
    SDL_GPUBuffer*           skinnedVertexBuffer;
    SDL_GPUBuffer*           surfaceVertexBuffer;
    SDL_GPUBuffer*           indexBuffer;
    SDL_GPUBuffer*           lineBuffer;
    SDL_GPUBuffer*           lineDrawArgsBuffer;
    RenderSetBuffers         skinnedBuffers;
    RenderSetBuffers         surfaceBuffers;
    
    SDL_GPUSampleCount       sample_count;
    // anim
    SDL_GPUBuffer*           boneBuffer;
    SDL_GPUBuffer*           animPoseBuffer;
    SDL_GPUBuffer*           animHierarchyBuffer;
    SDL_GPUBuffer*           animDataBuffer;
    SDL_GPUBuffer*           jointsBuffer;
    SDL_GPUBuffer*           invBindBuffer;
    SDL_GPUBuffer*           animInstanceBuffer;
    SDL_GPUBuffer*           textureDescriptorBuffer;
    SDL_GPUBuffer*           materialBuffer;
    u32                      numTextureDescriptors;
    u32                      numMaterials;
    Texture                  albedoPages;
    Texture                  normalPages;
    Texture                  metallicRoughnessPages;
    Texture                  textures[MAX_SCENE_TEXTURES];
} RenderState;


typedef struct Graphics_
{
    ASkinedVertex* SkinnedVertexBuffer;
    AVertex*       SurfaceVertexBuffer;
    u32*           IndexBuffer;
    u32            NumIndices;
    u32            NumSkinnedVertices;
    u32            NumSurfaceVertices;
} Graphics;

static inline s32 GetRootNodeIdx(SceneBundle* bundle)
{
    s32 node = 0;
    if (bundle->numScenes > 0) {
        AScene defaultScene = bundle->scenes[bundle->defaultSceneIndex];
        node = defaultScene.nodes[0];
    }
    return node;
}

void GraphicsInit(bool msaa);

void GraphicsDestroy();

Texture rImportTexture(const char* path, TexFlags flags, const char* label);

Texture rCreateTexture(int width, int height, void* data, SDL_GPUTextureFormat format,
                       TexFlags flags, SDL_GPUTextureUsageFlags usage, const char* label);

Texture rCreateTexture2DArray(int width, int height, int layers, void* data, SDL_GPUTextureFormat format, 
                              TexFlags flags, SDL_GPUTextureUsageFlags usage, const char* label);

void rDeleteTexture(Texture texture);

void TextureSystem_BuildPages(SceneBundle** bundles, const u32* imageOffsets, u32 numBundles, Texture* textures);

void UploadTextureRegion(Texture texture, u32 layer, u32 x, u32 y, u32 width, u32 height, u32 srcWidth, u32 srcHeight, const void* data);

void GenerateTextureMips(Texture texture);

s32 GraphicsTypeToSize(GraphicType type);

SDL_GPUBuffer* CreateBuffer(void* buffer, size_t bufferSize, SDL_GPUBufferUsageFlags bufferUsage, const char* debugName);

void UpdateGPUBuffer(SDL_GPUBuffer* buffer, const void* data, size_t bufferSize, size_t offset);

SDL_GPUTexture* CreateDepthTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateMSAATexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateResolveTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateSceneColorTexture(u32 drawablew, u32 drawableh, SDL_GPUSampleCount sampleCount);

SDL_GPUTexture* CreatePostProcessTexture(u32 drawablew, u32 drawableh);

#if defined(__cplusplus)
}
#endif

#endif
