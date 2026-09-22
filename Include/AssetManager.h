
#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

// game build doesn't have astc encoder, ufbx, dxt encoder. 
// because we are only decoding when we release the game
// if true, reduces exe size and you will have faster compile times.
// also it uses zstddeclib instead of entire zstd. (only decompression in game builds) go to CMakeLists.txt for more details
#if defined(__ANDROID__)
    #define AX_GAME_BUILD 1
#else
    #define AX_GAME_BUILD 0 /* make zero for editor build */
#endif


#include "Graphics.h"
#include "Async.h"
#include "Include/Memory.h"

#if defined(__cplusplus)
extern "C" {
#endif

s32  LoadFBX(const char* path, SceneBundle* fbxScene, f32 scale);
s32  LoadOBJ(const char* path, SceneBundle* objScene, f32 scale);

// Parse a source mesh file (.gltf/.glb/.fbx/.obj) into an intermediate SceneBundle by extension.
s32 ImportBundle(const char* path, SceneBundle* scene, f32 scale);

/* Binary asset cache */
s32 SaveGLTFBinary(const SceneBundle* gltf, const char* path);

s32 LoadSceneBundleBinary(const char* path, SceneBundle* gltf, void** outVertexHeapPtr, void** outIndexHeapPtr);

s32 LoadBundleMeshCached(const char* path, SceneBundle* bundle, void** outVertexHeapPtr, void** outIndexHeapPtr, bool* outBaked);
// Fans the per-image transcode+GPU upload out across worker threads via ParallelFor (each
// image is an independent create/submit/fence-wait round trip through SDL_GPU - the slow part).
s32 LoadBundleImagesFromCache(const char* gltfPath, SceneBundle* bundle, Texture* staging);

// ABM = AX binary mesh
u8 IsABMLastVersion(const char* path);
u8 IsMeshPath(const char* path);

/* Mesh baking */
// returns 0 on not enough memory
s32 BakeSceneMeshesAndAnimations(SceneBundle* gltf, void** outVertexHeapPtr, void** outIndexHeapPtr);


/* Animation baking */
void BakeGLTFAnimations(SceneBundle* gltf);

void CreateVerticesIndicesSkined(SceneBundle* gltf);

/* Texture/image cache */
u8 IsTextureLastVersion(const char* path);

void SaveSceneImages(SceneBundle* scene, const char* savePath, bool deleteRemaining);

void SaveSceneImagesAsync(SceneBundle* scene, const char* savePath, bool deleteRemaining, AsyncCallback callback);

// returns: 0 = noFile, 1 = success, 2 = missingImages, 3 = fileNumImage missmatch
s32 LoadSceneImages(const char* texturePath, Texture* textures, s32 numImages);


// MeshBuilder.c
typedef struct MeshBuilder_
{
    float3* positions;
    float2* texCoords;
    float3* normals;
    v128f*  tangents;
    u32*    indices;
    
    SceneBundle*   bundle;
    APrimitive*    primitive;
    Pow2Allocator* alloc;
} MeshBuilder;

typedef enum MeshType_
{
    MeshType_Default   , // imported from fbx, gltf, obj and saved with abm
    MeshType_Runtime   , // generated runtime
    // unit meshes
    MeshType_Sphere    ,
    MeshType_Cylinder  ,
    MeshType_Cone      ,
    MeshType_Grid      ,
    MeshType_Cube      ,
    MeshType_Capsule
} MeshType;

// MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
// set vertices, set indices -> Scene_AddBundle -> Scene_Spawn
// cache the b.result and use it later
MeshBuilder MeshBuilder_Create(s32 numVertex, s32 numIndex);
SceneBundle* GenerateSphere(float radius, u32 ringCount, u32 sliceCount);
SceneBundle* GenerateCylinder(float radius, float height, u32 sliceCount);
SceneBundle* GenerateCone(float height, float radius, u32 sliceCount);
SceneBundle* GenerateGrid(float segmentSize, u32 verticalCount, u32 horizontalCount);
SceneBundle* GenerateCube(float size);
SceneBundle* GenerateCapsule(float radius, float height, u32 sliceCount);

SceneBundle* GetUnitSphere(); // cached 1mt 16 ring sphere
SceneBundle* GetUnitCylinder();
SceneBundle* GetUnitCone(); // cached 1mt 16 ring cone
SceneBundle* GetUnitGrid();
SceneBundle* GetUnitCube();
SceneBundle* GetUnitCapsule();

MeshType IsBundlePrimitive(SceneBundle* bundle);
SceneBundle* GetUnitPrimitive(MeshType type);
const char* GetPrimitiveName(MeshType type);


#if defined(__cplusplus)
}
#endif

#endif // ASSET_MANAGER_H
