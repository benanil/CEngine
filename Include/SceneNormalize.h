
#ifndef SCENE_NORMALIZE_H
#define SCENE_NORMALIZE_H

#include "GLTFParser.h"

#if defined(__cplusplus)
extern "C" {
#endif

void SceneBundle_BuildParentIndices(SceneBundle* scene);
void SceneBundle_FlattenNodes(SceneBundle* scene);
void SceneBundle_ValidateNodeHierarchy(const SceneBundle* scene);
void SceneBundle_Normalize(SceneBundle* scene);

#if defined(__cplusplus)
}
#endif

#endif // SCENE_NORMALIZE_H
