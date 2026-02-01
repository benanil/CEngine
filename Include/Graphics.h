#ifndef _H_GRAPHICS_
#define _H_GRAPHICS_

#include <SDL3/SDL_gpu.h>
#include "../Math/Half.h"
#include "../Math/Vector.h"
#include "GLTFParser.h"

#define CHECK_CREATE(var, thing) { if (!(var)) { SDL_Log("Failed to create %s: %s", thing, SDL_GetError()); Quit(2); } }
#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)

#if defined(__cplusplus)
extern "C" {
#endif

enum TexFlags_
{
    TexFlags_None        = 0,
    TexFlags_MipMap      = 1,
    TexFlags_Compressed  = 2,
    TexFlags_ClampToEdge = 4,
    TexFlags_Nearest     = 8,
    TexFlags_Linear      = 16, // default linear in desktop platforms
    TexFlags_DontDeleteCPUBuffer = 32,
    TexFlags_DynamicUpdate = 64, // the image content is updated infrequently by the CPU
    TexFlags_StreamUpdate = 128, // the image content is updated each frame by the CPU via
    TexFlags_RenderAttachment = 256,
    TexFlags_StorageAttachment = 512,
    // no filtering or wrapping
    TexFlags_RawData     = TexFlags_Nearest | TexFlags_ClampToEdge
};
typedef int TexFlags;

enum GraphicType_
{
    GraphicType_Byte, // -> 0x1400 in opengl 
    GraphicType_UnsignedByte, 
    GraphicType_Short,
    GraphicType_UnsignedShort, 
    GraphicType_Int,
    GraphicType_UnsignedInt,
    GraphicType_Float,
    GraphicType_TwoByte,
    GraphicType_ThreeByte,
    GraphicType_FourByte,
    GraphicType_Double,
    GraphicType_Half, // -> 0x140B in opengl
    GraphicType_XYZ10W2, // GL_INT_2_10_10_10_REV

    GraphicType_Vector2f,
    GraphicType_Vector3f,
    GraphicType_Vector4f,

    GraphicType_Vector2i,
    GraphicType_Vector3i,
    GraphicType_Vector4i,

    GraphicType_Matrix2,
    GraphicType_Matrix3,
    GraphicType_Matrix4,

    GraphicType_NormalizeBit = 1 << 31
};
typedef int GraphicType;

// https://www.yosoygames.com.ar/wp/2018/03/vertex-formats-part-1-compression/
typedef struct AVertex_
{
    float3   position;
    uint32_t normal;
    uint32_t tangent;
    uint32_t texCoord; // half2
} AVertex;

typedef struct ASkinedVertex_
{
    float3    position;
    uint32_t qtangentXYF16;
    uint32_t qtangentZWF16;
    uint32_t texCoord; // half2
    uint32_t joints;  // rgb8u
    uint32_t weights; // rgb8u
} ASkinedVertex;


typedef struct GPUMesh_
{
    int numVertex, numIndex;
    // unsigned because opengl accepts unsigned
    unsigned int vertexLayoutHandle;
    unsigned int indexHandle;
    unsigned int indexType;  // uint32, uint64. GL_BYTE + indexType
    unsigned int vertexHandle; // opengl handles for, POSITION, TexCoord...
    // usefull for knowing which attributes are there
    // POSITION, TexCoord... AAttribType_ bitmask
    int attributes;
    int stride; // size of an vertex of the mesh
    
    void* vertices;
    void* indices;
} GPUMesh;

typedef struct Texture_
{
    int width, height;
    SDL_GPUTexture* handle;
    SDL_GPUTextureFormat format;
    void* buffer;
} Texture;

typedef struct WindowState
{
	SDL_GPUTexture* tex_depth, *tex_msaa, *tex_resolve;
	Uint32 prev_drawablew, prev_drawableh;
} WindowState;

typedef struct RenderState
{
	SDL_GPUBuffer* buf_vertex;
	SDL_GPUBuffer* buf_index;
	SDL_GPUBuffer* buf_bones;
	SDL_GPUBuffer* buf_positions;
	SDL_GPUBuffer* buf_rotations;
    SDL_GPUSampler* sampler;
    Texture textures[8];
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUSampleCount sample_count;
} RenderState;


static inline int GetRootNodeIdx(SceneBundle* bundle)
{
    int node = 0;
    if (bundle->numScenes > 0) {
        AScene defaultScene = bundle->scenes[bundle->defaultSceneIndex];
        node = defaultScene.nodes[0];
    }
    return node;
}

// uint8_t rTextureTypeToBytesPerPixel(sg_pixel_format type);

void rInit(bool msaa);

void rDestroy();

// int rGetMipmapImageData(sg_image_data* img_data, void* data, int width, int height);

Texture rImportTexture(const char* path, TexFlags flags, const char* label);

Texture rCreateTexture(int width, int height, void* data, SDL_PixelFormat format, TexFlags flags, const char* label);

void rDeleteTexture(Texture texture);

void rUpdateTexture(Texture texture, void* data);

int GraphicsTypeToSize(GraphicType type);

SDL_GPUBuffer* CreateBuffer(void* buffer, size_t bufferSize, SDL_GPUBufferUsageFlags bufferUsage, const char* debugName);

void UpdateGPUBuffer(SDL_GPUBuffer* buffer, const void* data, size_t bufferSize);

SDL_GPUTexture* CreateDepthTexture(Uint32 drawablew, Uint32 drawableh);

SDL_GPUTexture* CreateMSAATexture(Uint32 drawablew, Uint32 drawableh);

SDL_GPUTexture* CreateResolveTexture(Uint32 drawablew, Uint32 drawableh);


// // w value is undefined, it could be anything or trash data
// static inline Vector4x32f GetPosition(GPUMesh* gpu, int index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index;
//     return VecLoad((const float*)bytePtr);
// }
// 
// // todo GPUMesh GetNormal
// static inline Vector4x32f GetNormal(GPUMesh* gpu, int index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index + sizeof(Vec3f); // skip position
//     uint32_t normalPacked = *(uint32_t *)bytePtr;
//     typedef union S_ { Vector4x32f v; Vec3f s; } S;
//     S s = (S){ .s = Unpack_INT_2_10_10_10_REV(normalPacked) };
//     return s.v; // VecLoad(bytePtr);
// }
// 
// // todo GPUMesh GetNormal
// static inline Vec2f GetUV(GPUMesh* gpu, int index)
// {
//     const char* bytePtr = (const char*)gpu->vertices;
//     bytePtr += gpu->stride * index + sizeof(Vec3f) + sizeof(int) + sizeof(int); // skip normal and tangent
//     uint32_t uvPacked = *(uint32_t *)bytePtr;
//     Vec2f result;
//     ConvertHalf2ToFloat2(&result.x, uvPacked); // VecLoad(bytePtr);
//     return result;
// }

#if defined(__cplusplus)
}
#endif

#endif