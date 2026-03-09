
#include "Include/ECS.h"
#include "Include/Random.h"
#include "Math/Half.h"
#include "Math/Matrix.h"

void ECS_Init(ECS* ecs)
{
    Entity* entities = ecs->entities;

    for (s32 i = 0; i < MAX_ENTITY; i++)
    {
        u64 hash = MurmurHash(i + 123);
        entities[i].position = VecMulf(VecSetR(f1_(i & 63), 0.0f, f1_(i >> 6), 0.0f), 1.5f);
        entities[i].rotation = hash & 0xFFFF0000FFFF0000ull;  // x=0, y=random, z=0, w=random
        entities[i].scale = Half4One;
    }
}

void ECS_Update(ECS* ecs, float delta_time)
{

}