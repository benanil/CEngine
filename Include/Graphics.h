#ifndef _H_GRAPHICS_
#define _H_GRAPHICS_

#include <SDL3/SDL_gpu.h>
#include "../Math/Half.h"
#include "../Math/Vector.h"
#include "GLTFParser.h"

#define CHECK_CREATE(var, thing) { if (!(var)) { SDL_Log("Failed to create %s: %s", thing, SDL_GetError()); Quit(2); } }
#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)

#define MAX_VERTEX 1000000
#define MAX_INDEX (MAX_VERTEX * 5)

#if defined(__cplusplus)
extern "C" {
#endif

enum TexFlags_
{
    TexFlags_None                = 0,
    TexFlags_MipMap              = 1,
    TexFlags_Compressed          = 2,
    TexFlags_ClampToEdge         = 4,
    TexFlags_Nearest             = 8,
    TexFlags_Linear              = 16, // default linear in desktop platforms
    TexFlags_DontDeleteCPUBuffer = 32,
    TexFlags_DynamicUpdate       = 64, // the image content is updated infrequently by the CPU
    TexFlags_StreamUpdate        = 128, // the image content is updated each frame by the CPU via
    TexFlags_RenderAttachment    = 256,
    TexFlags_StorageAttachment   = 512,
    // no filtering or wrapping
    TexFlags_RawData     = TexFlags_Nearest | TexFlags_ClampToEdge
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
    f3   position;
    u32  normal;
    u32  tangent;
    u32  texCoord; // half2
} AVertex;

typedef struct ASkinedVertex_
{
    u32 positionXY;
    u32 positionZW;
    u32 qtangentXYF16;
    u32 qtangentZWF16;
    u32 texCoord; // half2
    u32 joints;  // rgb8u
    u32 weights; // rgb8u
} ASkinedVertex;


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
} Texture;

typedef struct WindowState
{
	SDL_GPUTexture* tex_depth, *tex_msaa, *tex_resolve;
	u32 prev_drawablew, prev_drawableh;
} WindowState;

typedef struct RenderState
{
	SDL_GPUBuffer*  vertexBuffer;
	SDL_GPUBuffer*  indexBuffer;
	SDL_GPUBuffer*  boneBuffer;
	SDL_GPUBuffer*  entityBuffer;
    SDL_GPUSampler* sampler;
    Texture textures[8];
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUSampleCount sample_count;
} RenderState;


typedef struct Graphics_
{
    ASkinedVertex* VertexBuffer;
    u32*           IndexBuffer ;
    u32            NumIndices  ;
    u32            NumVertices ;
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

// ui328_t rTextureTypeToBytesPerPixel(sg_pixel_format type);

void rInit(bool msaa);

void rDestroy();

// i32 rGetMipmapImageData(sg_image_data* img_data, void* data, i32 width, i32 height);

Texture rImportTexture(const char* path, TexFlags flags, const char* label);

Texture rCreateTexture(s32 width, s32 height, void* data, SDL_PixelFormat format, TexFlags flags, const char* label);

void rDeleteTexture(Texture texture);

void rUpdateTexture(Texture texture, void* data);

s32 GraphicsTypeToSize(GraphicType type);

SDL_GPUBuffer* CreateBuffer(void* buffer, size_t bufferSize, SDL_GPUBufferUsageFlags bufferUsage, const char* debugName);

void UpdateGPUBuffer(SDL_GPUBuffer* buffer, const void* data, size_t bufferSize);

SDL_GPUTexture* CreateDepthTexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateMSAATexture(u32 drawablew, u32 drawableh);

SDL_GPUTexture* CreateResolveTexture(u32 drawablew, u32 drawableh);


// // w value is undefined, it could be anything or trash data
// static inline Vector4x32f GetPosition(GPUMesh* gpu, i32 index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index;
//     return VecLoad((const float*)bytePtr);
// }
// 
// // todo GPUMesh GetNormal
// static inline Vector4x32f GetNormal(GPUMesh* gpu, i32 index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index + sizeof(Vec3f); // skip position
//     ui3232_t normalPacked = *(ui3232_t *)bytePtr;
//     typedef union S_ { Vector4x32f v; Vec3f s; } S;
//     S s = (S){ .s = Unpack_i32_2_10_10_10_REV(normalPacked) };
//     return s.v; // VecLoad(bytePtr);
// }
// 
// // todo GPUMesh GetNormal
// static inline Vec2f GetUV(GPUMesh* gpu, i32 index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index + sizeof(Vec3f) + sizeof(i32) + sizeof(i32); // skip normal and tangent
//     ui3232_t uvPacked = *(ui3232_t *)bytePtr;
//     Vec2f result;
//     ConvertHalf2ToFloat2(&result.x, uvPacked); // VecLoad(bytePtr);
//     return result;
// }

#if defined(__cplusplus)
}
#endif

#endif