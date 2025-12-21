
#include "Include/ECS.h"
#include "Include/Random.h"
#include "Math/Matrix.h"


void PackQuaternionS16Norm(Vector4x32f quat, uint32_t* result)
{
    quat = VecNorm(quat);
    Vector4x32f shortMax = VecSet1(INT16_MAX);
    Vector4x32u u32 = VecCvtF32U32(VecMul(quat, shortMax));
    Vector4x32u u16 = VecNarrowU32U16(u32);
    VecStoreI16(result, u16);
}

void ECS_Init(Vector4x32f* EntityPositions, uint32_t* EntityRotations)
{
    for (int i = 0; i < MAX_ENTITY; i++)
    {
        EntityPositions[i] = VecMulf(VecSetR((float)(i & 31), 0.0f, (float)(i >> 5), 0.0f), 4.0f);
        uint32_t rotation = WangHash(i + 123);
        EntityRotations[i * 2 + 0] = rotation & 0xFFFF0000;  // x=0, y=random
        EntityRotations[i * 2 + 1] = rotation << 16;         // z=0, w=random
    }
}

void ECS_Update(ECS* ecs, float delta_time)
{

}