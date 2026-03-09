#ifndef ENTITY_COMPONENT_SYSTEM
#define ENTITY_COMPONENT_SYSTEM

#include "SIMD.h" 

#define MAX_ENTITY UINT16_MAX

typedef struct Entity_
{
    v128f position;
    u64   rotation;
    u64   scale;
} Entity;


typedef struct ECS_
{
    Entity entities[MAX_ENTITY];

    u64 numEntities;
} ECS;

void ECS_Init(ECS* ecs);


void ECS_Update(ECS* ecs, f1 delta_time);



#endif