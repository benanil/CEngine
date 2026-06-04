#ifndef _H_GRAPHICS_
#define _H_GRAPHICS_

#include <SDL3/SDL_gpu.h>
#include "Math/Half.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "GLTFParser.h"
#include "RenderLimits.h"

#define CHECK_CREATE(var, thing) { if (!(var)) { AX_ERROR("Failed to create %s: %s", thing, SDL_GetError()); Quit(2); } }

#define TEXTURE_PAGE_SIZE       4096
#define TEXTURE_PAGE_LAYERS     8
#define MAX_SCENE_TEXTURES      1024
#define MAX_TEXTURE_DESCRIPTORS 2048
#define MAX_GPU_MATERIALS       2048

#define BReadRasterBit   SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ
#define BWriteComputeBit SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE
#define BReadCompute     SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
#define BIndirectBit     SDL_GPU_BUFFERUSAGE_INDIRECT
#define BVertexBit       SDL_GPU_BUFFERUSAGE_VERTEX

#define VFORMAT_F32    SDL_GPU_VERTEXELEMENTFORMAT_FLOAT
#define VFORMAT_FLOAT3 SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3
#define VFORMAT_FLOAT4 SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
#define VFORMAT_UINT   SDL_GPU_VERTEXELEMENTFORMAT_UINT
#define VFORMAT_HALF2  SDL_GPU_VERTEXELEMENTFORMAT_HALF2
#define VFORMAT_HALF4  SDL_GPU_VERTEXELEMENTFORMAT_HALF4
#define VFORMAT_UBYTE4 SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4
#define VFORMAT_NBYTE4 SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM

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
    float2 uvScale;
    float2 uvBias;
    u32 pageIndex;
    u32 flags;
    u32 padding[2];
} TextureDescriptor;

#define MATERIAL_FLAG_ALPHA_MASK       (1u << 0)
#define MATERIAL_ALPHA_CUTOFF_SHIFT    8u
#define MATERIAL_ALPHA_CUTOFF_MASK     (0xffu << MATERIAL_ALPHA_CUTOFF_SHIFT)

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
    SDL_GPUTexture* tex_depth, *tex_hiz_depth, *tex_color, *tex_post, *tex_hiz, *tex_sdsm_bounds;
    SDL_GPUTexture* tex_gbuffer_tangent, *tex_gbuffer_albedo_metallic, *tex_gbuffer_shadow_roughness;
    SDL_GPUTexture* tex_hbao, *tex_hbao_blur, *tex_hbao_normal;
    SDL_GPUTexture* tex_mlaa_edge_mask, *tex_mlaa_edge_count, *tex_mlaa_output;
    SDL_GPUTexture* tex_shadow_depth, *tex_shadow_color;
    u32 prev_width, prev_height;
    u32 hiz_width, hiz_height, hiz_mip_count, sdsm_mip_count;
    mat4x4 hiz_view_proj;
    bool hiz_valid;
    bool sdsm_valid;
} WindowState;

typedef struct RenderSetBuffers_
{
    SDL_GPUBuffer* primitiveGroup;
    SDL_GPUBuffer* drawSparseIndices;
    SDL_GPUBuffer* drawArgs;
    SDL_GPUBuffer* denseToPrimitive;
    SDL_GPUBuffer* sparseToDense;
    SDL_GPUBuffer* entity;
    SDL_GPUBuffer* visibleSparseIndices;
    SDL_GPUBuffer* visibilityMask;
    SDL_GPUBuffer* visibleCount;
    SDL_GPUBuffer* dispatchArgs;
} RenderSetBuffers;

typedef struct RenderState
{
    SDL_GPUGraphicsPipeline* skinnedPipeline;
    SDL_GPUGraphicsPipeline* surfacePipeline;
    SDL_GPUGraphicsPipeline* skinnedDepthPipeline;
    SDL_GPUGraphicsPipeline* surfaceDepthPipeline;
    SDL_GPUGraphicsPipeline* skinnedShadowPipeline;
    SDL_GPUGraphicsPipeline* surfaceShadowPipeline;
    SDL_GPUGraphicsPipeline* linePipeline;
    SDL_GPUGraphicsPipeline* slugPipeline;
    SDL_GPUGraphicsPipeline* slug2DPipeline;
    SDL_GPUGraphicsPipeline* slugDepthPipeline;
    SDL_GPUGraphicsPipeline* uiShapePipeline;
    SDL_GPUGraphicsPipeline* uiImagePipeline;
    SDL_GPUSampler*          sampler;
    SDL_GPUSampler*          hiZSampler;
    SDL_GPUSampler*          shadowSampler;
    SDL_GPUBuffer*           skinnedVertexBuffer;
    SDL_GPUBuffer*           surfaceVertexBuffer;
    SDL_GPUBuffer*           indexBuffer;
    SDL_GPUBuffer*           lineBuffer;
    SDL_GPUBuffer*           lineDrawArgsBuffer;
    SDL_GPUBuffer*           uiShapeBuffer;
    SDL_GPUBuffer*           uiShapeDrawArgsBuffer;
    SDL_GPUBuffer*           skinnedAnimatedVertices;
    SDL_GPUBuffer*           shadowCascadeBuffer;
    RenderSetBuffers         skinnedBuffers;
    RenderSetBuffers         surfaceBuffers;
    
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
    SDL_GPUTexture*          skyNoise3D;
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

void InitTextureSystem(void);

void TextureSystem_BuildPages(SceneBundle** bundles, const u32* imageOffsets, u32 numBundles, Texture* textures);

void UploadTextureRegion(Texture texture, u32 layer, u32 x, u32 y, u32 width, u32 height, u32 srcWidth, u32 srcHeight, const void* data);

void GenerateTextureMips(Texture texture);

void ReleaseTexture(Texture* texture);

s32 GraphicsTypeToSize(GraphicType type);

SDL_GPUBuffer* CreateBuffer(void* buffer, size_t bufferSize, SDL_GPUBufferUsageFlags bufferUsage, const char* debugName);

void UpdateGPUBuffer(SDL_GPUBuffer* buffer, const void* data, size_t bufferSize, size_t offset);

SDL_GPUTexture* CreateDepthTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateHiZDepthTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateHiZTexture(u32 drawablew, u32 drawableh, u32* mipCount);
SDL_GPUTexture* CreateSDSMDepthBoundsTexture(u32 drawablew, u32 drawableh, u32* mipCount);

SDL_GPUTexture* CreateSceneColorTexture(u32 drawablew, u32 drawableh, SDL_GPUSampleCount sampleCount);

SDL_GPUTexture* CreateGBufferTangentTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateGBufferAlbedoMetallicTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateGBufferShadowRoughnessTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreatePostProcessTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateHBAOTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateHBAONormalTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateMLAAEdgeMaskTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateMLAAEdgeCountTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateMLAAOutputTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* Create3DNoise3DTexture(u32 size);

SDL_GPUTexture* CreateShadowDepthTexture(u32 size);

SDL_GPUTexture* CreateShadowColorTexture(u32 size, u32 layers);

#if defined(__cplusplus)
}
#endif

#endif
