
#include "Include/ECS.h"
#include "Include/Random.h"
#include "Math/Math.h"


void ECS_Init(Vector4x32f* EntityPositions, uint32_t* EntityRotations)
{
    for (int i = 0; i < MAX_ENTITY; i++)
    {
        EntityPositions[i] = VecMulf(VecSetR((float)(i & 31), 0.0f, (float)(i >> 5), 0.0f), 25.0f);
        uint32_t rotation = WangHash(i);
        EntityRotations[i * 2 + 0] = rotation & 0xFFFF0000;  // x=0, y=random
        EntityRotations[i * 2 + 1] = rotation << 16;         // z=0, w=random
    }
}

void ECS_Update(ECS* ecs, float delta_time)
{

}