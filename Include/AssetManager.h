
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

int LoadFBX(const char* path, SceneBundle* fbxScene, float scale);

int SaveGLTFBinary(const SceneBundle* gltf, const char* path);

int LoadSceneBundleBinary(const char* path, SceneBundle* gltf);

void CreateVerticesIndices(SceneBundle* gltf);

void CreateVerticesIndicesSkined(SceneBundle* gltf);

// ABM = AX binary mesh
bool IsABMLastVersion(const char* path);

bool IsTextureLastVersion(const char* path);

void SaveSceneImages(SceneBundle* scene, const char* savePath);

// returns: 0 = noFile, 1 = success, 2 = missingImages, 3 = fileNumImage missmatch
int LoadSceneImages(const char* texturePath, Texture* textures, int numImages);


#if defined(__cplusplus)
}
#endif

#endif // ASSET_MANAGER_H