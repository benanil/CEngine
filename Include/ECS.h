#ifndef ENTITY_COMPONENT_SYSTEM
#define ENTITY_COMPONENT_SYSTEM

#include "SIMD.h" 

#define MAX_ENTITY 4096

typedef struct ECS_
{
    v128f EntityPositions[MAX_ENTITY];
    u32 EntityRotations[MAX_ENTITY * 2];
    u32 EntityData[MAX_ENTITY];
    u32 NumEntities;
} ECS;


void ECS_Init(v128f* EntityPositions, uint32_t* EntityRotations);

void ECS_Update(ECS* ecs, f1 delta_time);





#endif