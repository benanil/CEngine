
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

i32 LoadFBX(const u8* path, SceneBundle* fbxScene, f1 scale);

i32 SaveGLTFBinary(const SceneBundle* gltf, const u8* path);

i32 LoadSceneBundleBinary(const u8* path, SceneBundle* gltf);

void CreateVerticesIndices(SceneBundle* gltf);

void CreateVerticesIndicesSkined(SceneBundle* gltf);

// ABM = AX binary mesh
u8 IsABMLastVersion(const u8* path);

u8 IsTextureLastVersion(const u8* path);

void SaveSceneImages(SceneBundle* scene, const u8* savePath);

// returns: 0 = noFile, 1 = success, 2 = missingImages, 3 = fileNumImage missmatch
i32 LoadSceneImages(const u8* texturePath, Texture* textures, i32 numImages, SDL_GPUDevice* gpuDevice);


#if defined(__cplusplus)
}
#endif

#endif // ASSET_MANAGER_H