
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


#if defined(__cplusplus)
extern "C" {
#endif

s32 LoadFBX(const u8* path, SceneBundle* fbxScene, f32 scale);

s32 SaveGLTFBinary(const SceneBundle* gltf, const u8* path);

s32 LoadSceneBundleBinary(const u8* path, SceneBundle* gltf);

// returns 0 on not enough memory
s32 CreateVerticesIndices(SceneBundle* gltf);

void CreateVerticesIndicesSkined(SceneBundle* gltf);

// ABM = AX binary mesh
u8 IsABMLastVersion(const u8* path);

u8 IsTextureLastVersion(const u8* path);

void SaveSceneImages(SceneBundle* scene, const u8* savePath, bool deleteRemaining);

// returns: 0 = noFile, 1 = success, 2 = missingImages, 3 = fileNumImage missmatch
s32 LoadSceneImages(const u8* texturePath, Texture* textures, s32 numImages);

s32 LoadGLTFCached(const char* path, SceneBundle* scene, Texture* textures);

void OptimizeMesh(const SceneBundle* gltf);

void GenerateLOD_75_GLTF(SceneBundle* sceneBundle);

void GenerateLOD_50_GLTF(SceneBundle* sceneBundle);

#if defined(__cplusplus)
}
#endif

#endif // ASSET_MANAGER_H