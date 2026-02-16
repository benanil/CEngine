#ifndef ENTITY_COMPONENT_SYSTEM
#define ENTITY_COMPONENT_SYSTEM

#include "SIMD.h" 

#define MAX_ENTITY 4096


typedef struct ECS_
{
    Vec4x32f EntityPositions[MAX_ENTITY];
    uint32_t EntityRotations[MAX_ENTITY * 2];
    uint32_t EntityData[MAX_ENTITY];
    uint32_t NumEntities;
} ECS;


void ECS_Init(Vec4x32f* EntityPositions, uint32_t* EntityRotations);

void ECS_Update(ECS* ecs, float delta_time);





#endif