#ifndef SCENE_SERIALIZER_H
#define SCENE_SERIALIZER_H

#include "Scene.h"

#if defined(__cplusplus)
extern "C" {
#endif

// writes the scene as a line separated .scene text file describing its .abm bundles,
// spawns, lights, sun and texture tables, plus the texture pages baked next to it as
// .basis texture 2d arrays with mips: <name>_albedo.basis, <name>_normal.basis,
// <name>_mr.basis. out: 0 on failure
s32 SceneSerializer_Save(Scene* scene, const char* path);

// thread safe function can be called async. after this call SceneSerializer_Load
SceneFileData* SceneSerializer_LoadStage(const char* path);

// in case SceneSerializer_LoadStage fail, otherwise no need to destroy
void SceneFileData_Destroy(SceneFileData* data);

// loads a .scene file into a freshly initialized empty scene. when the baked atlases
// are usable the texture system restores from them directly (no per image packing),
// otherwise every bundle appends through the normal path. out: 0 on failure
// data: can be null, or retrive from SceneSerializer_LoadStage
s32 SceneSerializer_Load(Scene* scene, const char* path, SceneFileData* data);

#if defined(__cplusplus)
}
#endif

#endif // SCENE_SERIALIZER_H
