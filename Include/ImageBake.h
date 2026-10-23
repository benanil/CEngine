#ifndef IMAGE_BAKE_H
#define IMAGE_BAKE_H
#include <stdbool.h>

void SaveSceneImages(struct SceneBundle_* scene, const char* savePath, bool deleteRemaining);

// unusded for now 
// void SaveSceneImagesAsync(struct SceneBundle_* scene, const char* path, bool deleteRemaining, AsyncCallback callback);

// result: 1 fine, 2 some file is not exist, 3 not enough images for scene
int LoadSceneImages(const char* texturePath, struct Texture_* textures, int numImages);

// loads the cached basis images of a gltf into a bundle local staging array
int LoadBundleImagesFromCache(const char* gltfPath, struct SceneBundle_* bundle, struct Texture_* staging);

#endif IMAGE_BAKE_H