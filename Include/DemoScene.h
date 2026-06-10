#ifndef DEMO_SCENE_H
#define DEMO_SCENE_H

#include "Scene.h"

// loads the demo bundles and spawns the demo entities. out: 0 on failure
s32 DemoScene_Create(void);

void DemoScene_Update(f32 deltaTime);

Scene* DemoScene_Get(void);

#endif // DEMO_SCENE_H
