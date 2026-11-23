// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT
// directly copied from box3d's sample source codes.
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


// Dynamic character mover. The capsule is a rigid body with locked rotation, so it
// collides and is pushed like everything else in the world. A mover joint drives it to
// the target velocity and a pogo joint holds it above the ground so it can climb
// steps without touching the capsule.
//
// This system is inspired by Tom Waterson's presentation:
// https://www.youtube.com/watch?v=_jRLlTDqoGI
typedef struct DynamicMover
{
    // Used for the pogo to detect moving platforms.
    uint64_t  platformMaterialId;
	float     jumpSpeed;
	float     maxSpeed;
	float     minSpeed;
	float     stopSpeed;
	float     accelerate;
    // Fraction of the ground acceleration available in the air.
	float     airSteer;
	float     friction;
    // A character usually wants to fall faster than the rest of the world.
	float     gravityScale;
    // Force budget the mover joint has for reaching the target velocity. The air value
	// is small so the character keeps its momentum while jumping.
	float     maxGroundForce;
	float     maxAirForce;
	float     pogoRestLength;
	float     pogoHertz;
	float     pogoDampingRatio;
    // Pogo force limits as a multiple of the mover weight.
	float     pogoCompressionScale;
	float     pogoTensionScale;
    // Surfaces steeper than this are not ground.
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
    // Casts an octagon skirt of rays around the capsule when the central ray misses or
	// lands on a surface too steep to walk, so the mover can stand on a ledge or a thin
	// rail, or step off the foot of a steep slope.
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
// Advance the mover. Throttle is the input along the forward and right directions on [-1, 1].
void DynamicMover_Update( DynamicMover* mover, float timeStep, b3Vec3 forward, b3Vec3 right, b3Vec2 throttle,
						  float speedScale );
// Jump if grounded. Returns false when airborne.
bool DynamicMover_Jump( DynamicMover* mover );

// Release the ground so an external impulse can carry the mover into the air. Otherwise the
// pogo pulls it back down.
void DynamicMover_Launch( DynamicMover* mover );
// Gravity scale is used to size the pogo forces, so change it here.
void DynamicMover_SetGravityScale( DynamicMover* mover, float gravityScale );
void DynamicMover_Draw( const DynamicMover* mover );

static inline b3BodyId DynamicMover_GetBodyId( const DynamicMover* mover )
{
	return mover->moverId;
}

#ifdef __cplusplus
}
#endif
