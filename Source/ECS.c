
#include "Include/ECS.h"
#include "Include/Random.h"
#include "Math/Matrix.h"

void ECS_Init(v128f* EntityPositions, u32* EntityRotations)
{
    for (i32 i = 0; i < MAX_ENTITY; i++)
    {
        EntityPositions[i] = VecMulf(VecSetR((f1)(i & 63), 0.0f, (f1)(i >> 6), 0.0f), 1.5f);
        u32 rotation = WangHash(i + 123);
        EntityRotations[i * 2 + 0] = rotation & 0xFFFF0000;  // x=0, y=random
        EntityRotations[i * 2 + 1] = rotation << 16;         // z=0, w=random
    }
}

void ECS_Update(ECS* ecs, float delta_time)
{

}