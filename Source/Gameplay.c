

#include "Include/Scene.h"
#include "Include/Terrain.h"
#include "Include/Camera.h"
#include "Include/BVH.h"

extern Camera g_Camera;
extern void UpdateCharacter();

static void UpdatePicking()
{
    BVHHit hit = {};
    v128f origin = Vec3Load(&g_Camera.position.x);
    v128f direction = VecNorm(Vec3Load(&g_Camera.front));
    hit.hit.t = FLT_MAX;

    if (GetMousePressed(MouseButton_Left) && Physics_PhysicsRaycastPick(origin, direction, 10.0f, &hit, PhysBit_Foliage))
    {
        RenderSet* rs = &Foliage_GetScene()->surfaceSet;
        SparseData* sparse = &rs->sparseData[hit.entityID & 0x00FFFFFFu];
        if (sparse->parent) sparse -= sparse->parent;
        Entity* entity = &rs->entities[sparse->id];
        b3BodyId body = Physics_GetPhysicsBody(Foliage_GetScene(), entity);

        b3ShapeId shapeId;
        b3Body_GetShapes(body, &shapeId, 1);
        b3Body_SetType(body, b3_dynamicBody);
        Physics_ApplyDefaultDynamicMass(body, shapeId);
        b3Body_ApplyForceToCenter(body, (b3Vec3){0.0f, 100.0f, 1.0f}, true);
    }
}

void Gameplay_Update()
{
    UpdatePicking();
    UpdateCharacter();
}
