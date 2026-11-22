// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include "box3d/types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
	DYNAMIC_MOVER_SKIRT_COUNT = 8,
	DYNAMIC_MOVER_PROBE_CAPACITY = DYNAMIC_MOVER_SKIRT_COUNT + 1,
};

typedef struct DynamicMoverDef
{
	b3Pos     position;
	b3Capsule capsule;
	b3Filter  filter;
	uint64_t  platformMaterialId;
	float     density;
	float     jumpSpeed;
	float     maxSpeed;
	float     minSpeed;
	float     stopSpeed;
	float     accelerate;
	float     airSteer;
	float     friction;
	float     gravityScale;
	float     maxGroundForce;
	float     maxAirForce;
	float     pogoRestLength;
	float     pogoHertz;
	float     pogoDampingRatio;
	float     pogoCompressionScale;
	float     pogoTensionScale;
	bool      enableSkirtPogo;
	bool      enablePreSolveEvents;
	bool      enableSensorEvents;
} DynamicMoverDef;

typedef struct DynamicMoverCastResult
{
	b3Pos    point;
	b3Vec3   normal;
	b3BodyId bodyId;
	uint64_t userMaterialId;
	float    fraction;
	bool     hit;
} DynamicMoverCastResult;

typedef struct DynamicMoverProbe
{
	b3Pos  origin;
	b3Vec3 translation;
	float  fraction;
	bool   hit;
} DynamicMoverProbe;

typedef struct DynamicMover
{
    uint64_t  platformMaterialId;
	float     jumpSpeed;
	float     maxSpeed;
	float     minSpeed;
	float     stopSpeed;
	float     accelerate;
	float     airSteer;
	float     friction;
	float     gravityScale;
	float     maxGroundForce;
	float     maxAirForce;
	float     pogoRestLength;
	float     pogoHertz;
	float     pogoDampingRatio;
	float     pogoCompressionScale;
	float     pogoTensionScale;
	float     minGroundNormalY;

	b3Capsule capsule;
	b3Filter  filter;

	b3WorldId worldId;
	b3BodyId  groundId;
	b3BodyId  moverId;
	b3BodyId  surfaceBodyId;
	b3JointId moverJointId;
	b3JointId pogoJointId;

	DynamicMoverCastResult castResult;

	b3Vec3 velocity;
	float  timeSliding;
	float  slideDuration;
	bool   onGround;
	bool   walkable;
	bool   jumping;
	bool   enableSkirtPogo;
	int    jumpTicks;

	float  pogoImpulse;
	float  pogoVelocity;
	float  pogoLength;

	DynamicMoverProbe probes[DYNAMIC_MOVER_PROBE_CAPACITY];
	int probeCount;
	int selectedProbe;
} DynamicMover;

DynamicMoverDef DefaultDynamicMoverDef( void );

void DynamicMover_Init( DynamicMover* mover );
void DynamicMover_Create( DynamicMover* mover, b3WorldId worldId, const DynamicMoverDef* def );
void DynamicMover_Destroy( DynamicMover* mover );
void DynamicMover_Update( DynamicMover* mover, float timeStep, b3Vec3 forward, b3Vec3 right, b3Vec2 throttle,
						  float speedScale );
bool DynamicMover_Jump( DynamicMover* mover );
void DynamicMover_Launch( DynamicMover* mover );
void DynamicMover_SetGravityScale( DynamicMover* mover, float gravityScale );
void DynamicMover_Draw( const DynamicMover* mover );

static inline b3BodyId DynamicMover_GetBodyId( const DynamicMover* mover )
{
	return mover->moverId;
}

#ifdef __cplusplus
}
#endif
