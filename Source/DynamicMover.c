// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#include "DynamicMover.h"
#include "Include/Editor.h"
#include "Include/Scene.h" // B3VecTov128f

#include "box3d/box3d.h"
#include "box3d/math_functions.h"

typedef struct CastContext
{
	DynamicMoverCastResult result;
	b3BodyId moverId;
} CastContext;

static float CastCallback( b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t userMaterialId,
						   int triangleIndex, int childIndex, void* context )
{
	(void)triangleIndex; (void)childIndex;

	CastContext* castContext = (CastContext*)context;
	b3BodyId bodyId = b3Shape_GetBody( shapeId );

	if ( B3_ID_EQUALS( bodyId, castContext->moverId ) || b3Shape_IsSensor( shapeId ) )
		return -1.0f;

	DynamicMoverCastResult* result = &castContext->result;
	result->point = point;
	result->normal = normal;
	result->bodyId = bodyId;
	result->userMaterialId = userMaterialId;
	result->fraction = fraction;
	result->hit = true;
	return fraction;
}

DynamicMoverDef DefaultDynamicMoverDef( void )
{
	DynamicMoverDef def = { 0 };
	def.position = b3Pos_zero;
	def.capsule = ( b3Capsule ){ { 0.0f, -0.5f, 0.0f }, { 0.0f, 0.5f, 0.0f }, 0.3f };
	def.density = 200.0f;
	def.filter = b3DefaultFilter();
	def.jumpSpeed = 7.0f;
	def.maxSpeed = 6.0f;
	def.minSpeed = 0.1f;
	def.stopSpeed = 3.0f;
	def.accelerate = 20.0f;
	def.airSteer = 0.25f;
	def.friction = 8.0f;
	def.gravityScale = 1.5f;
	def.maxGroundForce = 6000.0f;
	def.maxAirForce = 1500.0f;
	def.pogoRestLength = 0.9f;
	def.pogoHertz = 3.0f;
	def.pogoDampingRatio = 0.8f;
	def.pogoCompressionScale = 100.0f;
	def.pogoTensionScale = 100.0f;
	def.platformMaterialId = 0;
	def.enableSkirtPogo = true;
	def.enablePreSolveEvents = false;
	def.enableSensorEvents = false;
	return def;
}

static void CreateMoverJoint( DynamicMover* mover, b3BodyId baseId )
{
	b3MoverJointDef moverDef = b3DefaultMoverJointDef();
	moverDef.linearVelocity = mover->velocity;
	moverDef.maxForce = mover->maxGroundForce;
	moverDef.base.bodyIdA = baseId;
	moverDef.base.bodyIdB = mover->moverId;
	moverDef.base.collideConnected = true;
	mover->moverJointId = b3CreateMoverJoint( mover->worldId, &moverDef );
}

void DynamicMover_Init( DynamicMover* mover )
{
	DynamicMoverDef def = DefaultDynamicMoverDef();
	mover->jumpSpeed = def.jumpSpeed;
	mover->maxSpeed = def.maxSpeed;
	mover->minSpeed = def.minSpeed;
	mover->stopSpeed = def.stopSpeed;
	mover->accelerate = def.accelerate;
	mover->airSteer = def.airSteer;
	mover->friction = def.friction;
	mover->gravityScale = def.gravityScale;
	mover->maxGroundForce = def.maxGroundForce;
	mover->maxAirForce = def.maxAirForce;
	mover->pogoRestLength = def.pogoRestLength;
	mover->pogoHertz = def.pogoHertz;
	mover->pogoDampingRatio = def.pogoDampingRatio;
	mover->pogoCompressionScale = def.pogoCompressionScale;
	mover->pogoTensionScale = def.pogoTensionScale;
	mover->enableSkirtPogo = def.enableSkirtPogo;
	mover->platformMaterialId = def.platformMaterialId;
	mover->minGroundNormalY = 0.7f;

	mover->capsule = def.capsule;
	mover->filter = def.filter;

	mover->worldId = b3_nullWorldId;
	mover->groundId = b3_nullBodyId;
	mover->moverId = b3_nullBodyId;
	mover->surfaceBodyId = b3_nullBodyId;
	mover->moverJointId = b3_nullJointId;
	mover->pogoJointId = b3_nullJointId;

	MemSet(&mover->castResult, 0, sizeof(mover->castResult));

	mover->velocity = b3Vec3_zero;
	mover->timeSliding = 0.0f;
	mover->slideDuration = 0.2f;
	mover->onGround = false;
	mover->walkable = false;
	mover->jumping = false;
	mover->jumpTicks = 0;

	mover->pogoImpulse = 0.0f;
	mover->pogoVelocity = 0.0f;
	mover->pogoLength = 0.0f;

	for ( int i = 0; i < DYNAMIC_MOVER_PROBE_CAPACITY; ++i )
	{
		mover->probes[i] = ( DynamicMoverProbe ){ b3Pos_zero, b3Vec3_zero, 1.0f, false };
	}

	mover->probeCount = 0;
	mover->selectedProbe = 0;
}

void DynamicMover_Create( DynamicMover* mover, b3WorldId worldId, const DynamicMoverDef* def )
{
	mover->capsule = def->capsule;
	mover->filter = def->filter;
	mover->jumpSpeed = def->jumpSpeed;
	mover->maxSpeed = def->maxSpeed;
	mover->minSpeed = def->minSpeed;
	mover->stopSpeed = def->stopSpeed;
	mover->accelerate = def->accelerate;
	mover->airSteer = def->airSteer;
	mover->friction = def->friction;
	mover->gravityScale = def->gravityScale;
	mover->maxGroundForce = def->maxGroundForce;
	mover->maxAirForce = def->maxAirForce;
	mover->pogoRestLength = def->pogoRestLength;
	mover->pogoHertz = def->pogoHertz;
	mover->pogoDampingRatio = def->pogoDampingRatio;
	mover->pogoCompressionScale = def->pogoCompressionScale;
	mover->pogoTensionScale = def->pogoTensionScale;
	mover->enableSkirtPogo = def->enableSkirtPogo;
	mover->platformMaterialId = def->platformMaterialId;

	mover->worldId = worldId;
	mover->velocity = b3Vec3_zero;
	mover->onGround = false;
	mover->walkable = false;
	mover->jumping = false;
	mover->jumpTicks = 0;
	mover->pogoImpulse = 0.0f;
	mover->pogoVelocity = 0.0f;
	mover->pogoLength = 0.0f;
	mover->pogoJointId = b3_nullJointId;

	b3BodyDef bodyDef = b3DefaultBodyDef();
	mover->groundId = b3CreateBody( worldId, &bodyDef );

	bodyDef.type = b3_dynamicBody;
	bodyDef.position = def->position;
	bodyDef.gravityScale = mover->gravityScale;
	bodyDef.motionLocks.angularX = true;
	bodyDef.motionLocks.angularY = true;
	bodyDef.motionLocks.angularZ = true;
	bodyDef.enableSleep = false;
	bodyDef.name = "mover";
    bodyDef.userData = (void*)(((u64)4) << 32ull); // PhysBit_Player

	mover->moverId = b3CreateBody( worldId, &bodyDef );

	b3ShapeDef shapeDef = b3DefaultShapeDef();
	shapeDef.density = def->density;
	shapeDef.baseMaterial.friction = 0.0f;
	shapeDef.baseMaterial.restitution = 0.0f;
	shapeDef.baseMaterial.customColor = b3_colorDodgerBlue;
	shapeDef.filter = def->filter;
	shapeDef.enablePreSolveEvents = def->enablePreSolveEvents;
	shapeDef.enableSensorEvents = def->enableSensorEvents;

	b3CreateCapsuleShape( mover->moverId, &shapeDef, &mover->capsule );

	CreateMoverJoint( mover, mover->groundId );
}

void DynamicMover_Destroy( DynamicMover* mover )
{
	if (B3_IS_NON_NULL(mover->moverId))  b3DestroyBody(mover->moverId);
	if (B3_IS_NON_NULL(mover->groundId)) b3DestroyBody(mover->groundId);

	mover->groundId = b3_nullBodyId;
	mover->moverId = b3_nullBodyId;
	mover->moverJointId = b3_nullJointId;
	mover->pogoJointId = b3_nullJointId;
}

void DynamicMover_SetGravityScale( DynamicMover* mover, float gravityScale )
{
	mover->gravityScale = gravityScale;
	if (B3_IS_NON_NULL(mover->moverId))
		b3Body_SetGravityScale( mover->moverId, gravityScale );
}

bool DynamicMover_Jump( DynamicMover* mover )
{
	if ( mover->onGround == false || mover->walkable == false )
		return false;

	float surfaceVelocity = 0.0f;
	if ( b3Body_IsValid( mover->castResult.bodyId ) )
	{
		b3Vec3 v = b3Body_GetWorldPointVelocity( mover->castResult.bodyId, mover->castResult.point );
		surfaceVelocity = v.y;
	}

	float mass = b3Body_GetMass( mover->moverId );
	float vy = b3Body_GetLinearVelocity( mover->moverId ).y;

	float dv = b3MaxFloat( 0.0f, mover->jumpSpeed - vy ) + surfaceVelocity;
	b3Body_ApplyLinearImpulseToCenter( mover->moverId, ( b3Vec3 ){ 0.0f, mass * dv, 0.0f }, true );
	
    DynamicMover_Launch( mover );
	return true;
}

void DynamicMover_Launch( DynamicMover* mover )
{
	mover->onGround = false;
	mover->walkable = false;
	mover->jumping = true;
	mover->jumpTicks = 0;
}

void DynamicMover_Update( DynamicMover* mover, float timeStep, b3Vec3 forward, b3Vec3 right, b3Vec2 throttle,
						  float speedScale )
{
	float stepDownLength = mover->pogoRestLength;
	float rayLength = mover->onGround ? mover->pogoRestLength + stepDownLength : mover->pogoRestLength;
	b3Vec3 translation = { 0.0f, -rayLength, 0.0f };

	b3Pos position = b3Body_GetPosition( mover->moverId );
	b3Pos origin = b3OffsetPos( position, mover->capsule.center1 );

	b3QueryFilter queryFilter = {
		.categoryBits = mover->filter.categoryBits,
		.maskBits = mover->filter.maskBits,
		.id = 0,
		.name = "dynamic_mover_pogo",
	};

	CastContext castContext = { .moverId = mover->moverId };
	b3World_CastRay( mover->worldId, origin, translation, queryFilter, CastCallback, &castContext );
	mover->castResult = castContext.result;

	mover->probes[0] = ( DynamicMoverProbe ){ origin, translation, mover->castResult.hit ? mover->castResult.fraction : 1.0f,
											  mover->castResult.hit };
	mover->probeCount = 1;
	mover->selectedProbe = 0;

	b3Vec3 pogoOffset = b3Vec3_zero;

	bool centerWalkable = mover->castResult.hit && mover->castResult.normal.y >= mover->minGroundNormalY;
	if ( centerWalkable == false && mover->enableSkirtPogo )
	{
		b3Vec3 moverVelocity = b3Body_GetLinearVelocity( mover->moverId );
		b3Vec3 heading = { moverVelocity.x, 0.0f, moverVelocity.z };
		if ( b3LengthSquared( heading ) < FLT_EPSILON )
		{
			heading = b3Vec3_axisX;
		}

		heading = b3Normalize( heading );
		b3Vec3 side = { -heading.z, 0.0f, heading.x };

		float bestDisplacement = FLT_MAX;
		for ( int i = 0; i < DYNAMIC_MOVER_SKIRT_COUNT; ++i )
		{
			b3CosSin cs = b3ComputeCosSin( 2.0f * B3_PI * i / DYNAMIC_MOVER_SKIRT_COUNT );
			b3Vec3 offset = b3MulSV( mover->capsule.radius, b3Add( b3MulSV( cs.cosine, heading ), b3MulSV( cs.sine, side ) ) );

			b3Pos skirtOrigin = b3OffsetPos( origin, offset );
			CastContext skirtContext = { .moverId = mover->moverId };
			b3World_CastRay( mover->worldId, skirtOrigin, translation, queryFilter, CastCallback, &skirtContext );
			const DynamicMoverCastResult* result = &skirtContext.result;

			mover->probes[mover->probeCount] =
				( DynamicMoverProbe ){ skirtOrigin, translation, result->hit ? result->fraction : 1.0f, result->hit };

			if ( result->hit && result->normal.y >= mover->minGroundNormalY )
			{
				float displacement = b3AbsFloat( result->fraction * rayLength - mover->pogoRestLength );
				if ( displacement < bestDisplacement )
				{
					bestDisplacement = displacement;
					mover->castResult = *result;
					mover->selectedProbe = mover->probeCount;
					pogoOffset = offset;
				}
			}

			mover->probeCount += 1;
		}
	}

	if ( mover->jumping )
	{
		mover->jumpTicks += 1;

		if ( mover->jumpTicks > 2 && mover->castResult.hit && b3Dot( mover->velocity, mover->castResult.normal ) <= 0.0f )
		{
			mover->jumping = false;
		}
	}

	if ( mover->castResult.hit )
	{
		mover->walkable = mover->castResult.normal.y >= mover->minGroundNormalY;
		mover->onGround = mover->jumping == false;

		if ( mover->onGround )
		{
			if ( mover->walkable )
			{
				mover->timeSliding = b3MaxFloat( mover->timeSliding - timeStep, 0.0f );
			}
			else
			{
				mover->timeSliding = b3MinFloat( mover->timeSliding + timeStep, mover->slideDuration );
			}
		}
	}
	else
	{
		mover->onGround = false;
		mover->walkable = false;
		mover->timeSliding = b3MaxFloat( mover->timeSliding - timeStep, 0.0f );
	}

	b3BodyId baseId = mover->groundId;
	if ( mover->castResult.hit && mover->platformMaterialId != 0 && mover->castResult.userMaterialId == mover->platformMaterialId &&
		 b3Body_GetType( mover->castResult.bodyId ) == b3_kinematicBody )
	{
		baseId = mover->castResult.bodyId;
	}

	bool moverJointValid = b3Joint_IsValid( mover->moverJointId );

	if ( moverJointValid == false || B3_ID_EQUALS( b3Joint_GetBodyA( mover->moverJointId ), baseId ) == false )
	{
		if ( moverJointValid )
		{
			b3DestroyJoint( mover->moverJointId, false );
		}

		CreateMoverJoint( mover, baseId );
	}

	mover->velocity = b3Sub( b3Body_GetLinearVelocity( mover->moverId ), b3Body_GetLinearVelocity( baseId ) );

	if ( mover->onGround )
	{
		float speed = sqrtf( mover->velocity.x * mover->velocity.x + mover->velocity.z * mover->velocity.z );
		if ( speed < mover->minSpeed )
		{
			mover->velocity.x = 0.0f;
			mover->velocity.z = 0.0f;
		}
		else
		{
			float control = speed < mover->stopSpeed ? mover->stopSpeed : speed;

			float drop = control * mover->friction * timeStep;
			float newSpeed = b3MaxFloat( 0.0f, speed - drop );
			float ratio = newSpeed / speed;
			mover->velocity.x *= ratio;
			mover->velocity.z *= ratio;
		}
	}

	float maxForce;
	if ( mover->onGround )
	{
		if ( mover->slideDuration > 0.0f )
		{
			float scale = b3ClampFloat( 1.0f - mover->timeSliding / mover->slideDuration, 0.0f, 1.0f );
			maxForce = scale * mover->maxGroundForce;
		}
		else
		{
			maxForce = mover->walkable ? mover->maxGroundForce : 0.0f;
		}
	}
	else
	{
		maxForce = mover->maxAirForce;
	}

	b3MoverJoint_SetMaxVelocityForce( mover->moverJointId, maxForce );

	float maxSpeed = speedScale * mover->maxSpeed;
	b3Vec3 desiredVelocity = b3Add( b3MulSV( maxSpeed * throttle.x, forward ), b3MulSV( maxSpeed * throttle.y, right ) );
	desiredVelocity.y = 0.0f;

	float desiredSpeed;
	b3Vec3 desiredDirection = b3GetLengthAndNormalize( &desiredSpeed, desiredVelocity );

	if ( desiredSpeed > maxSpeed )
	{
		desiredSpeed = maxSpeed;
	}

	float currentSpeed = b3Dot( mover->velocity, desiredDirection );
	float addSpeed = desiredSpeed - currentSpeed;
	if ( addSpeed > 0.0f )
	{
		float steer = mover->walkable ? 1.0f : mover->airSteer;
		float accelSpeed = steer * mover->accelerate * maxSpeed * timeStep;
		if ( accelSpeed > addSpeed )
		{
			accelSpeed = addSpeed;
		}

		mover->velocity.x += accelSpeed * desiredDirection.x;
		mover->velocity.z += accelSpeed * desiredDirection.z;
	}

	if ( mover->castResult.hit == true && b3IsNormalized( mover->castResult.normal ) )
	{
		mover->pogoLength = mover->castResult.fraction * rayLength;

		float moverMass = b3Body_GetMass( mover->moverId );
		float gravity = b3Length( b3World_GetGravity( mover->worldId ) );
		float moverWeight = mover->gravityScale * gravity * moverMass;

		bool pogoJointValid = b3Joint_IsValid( mover->pogoJointId );

		if ( B3_ID_EQUALS( mover->castResult.bodyId, mover->surfaceBodyId ) == false || pogoJointValid == false )
		{
			if ( pogoJointValid )
			{
				mover->pogoImpulse = b3PogoJoint_GetImpulse( mover->pogoJointId );
				mover->pogoVelocity = b3PogoJoint_GetCurrentSpeed( mover->pogoJointId );

				b3DestroyJoint( mover->pogoJointId, false );
				mover->pogoJointId = b3_nullJointId;
			}

			mover->surfaceBodyId = mover->castResult.bodyId;

			b3PogoJointDef pogoDef = b3DefaultPogoJointDef();
			pogoDef.base.localFrameA.p = b3Body_GetLocalPoint( mover->surfaceBodyId, mover->castResult.point );
			pogoDef.base.localFrameB.p = b3Add( mover->capsule.center1, pogoOffset );
			pogoDef.normal = mover->castResult.normal;
			pogoDef.base.bodyIdA = mover->surfaceBodyId;
			pogoDef.base.bodyIdB = mover->moverId;
			pogoDef.base.collideConnected = true;
			pogoDef.restLength = mover->pogoRestLength;
			pogoDef.hertz = mover->pogoHertz;
			pogoDef.dampingRatio = mover->pogoDampingRatio;
			pogoDef.maxCompressionForce = mover->pogoCompressionScale * moverWeight;

			pogoDef.impulse = mover->pogoImpulse;
			pogoDef.velocity = mover->pogoVelocity;

			if ( mover->jumping )
			{
				pogoDef.maxTensionForce = 0.0f;
			}
			else
			{
				pogoDef.maxTensionForce = mover->pogoTensionScale * moverWeight;
			}

			mover->pogoJointId = b3CreatePogoJoint( mover->worldId, &pogoDef );
		}
		else
		{
			b3Transform localFrameA;
			localFrameA.p = b3Body_GetLocalPoint( mover->surfaceBodyId, mover->castResult.point );
			localFrameA.q = b3Quat_identity;
			b3Joint_SetLocalFrameA( mover->pogoJointId, localFrameA );

			b3Transform localFrameB;
			localFrameB.p = b3Add( mover->capsule.center1, pogoOffset );
			localFrameB.q = b3Quat_identity;
			b3Joint_SetLocalFrameB( mover->pogoJointId, localFrameB );

			b3PogoJoint_SetNormal( mover->pogoJointId, mover->castResult.normal );

			if ( mover->jumping )
			{
				b3PogoJoint_SetMaxTensionForce( mover->pogoJointId, 0.0f );
			}
			else
			{
				b3PogoJoint_SetMaxTensionForce( mover->pogoJointId, mover->pogoTensionScale * moverWeight );
			}

			b3PogoJoint_SetMaxCompressionForce( mover->pogoJointId, mover->pogoCompressionScale * moverWeight );

			b3PogoJoint_SetSpringHertz( mover->pogoJointId, mover->pogoHertz );
			b3PogoJoint_SetSpringDampingRatio( mover->pogoJointId, mover->pogoDampingRatio );
		}
	}
	else
	{
		if ( b3Joint_IsValid( mover->pogoJointId ) )
		{
			b3DestroyJoint( mover->pogoJointId, false );
			mover->pogoJointId = b3_nullJointId;
		}

		mover->surfaceBodyId = b3_nullBodyId;
		mover->pogoImpulse = 0.0f;
		mover->pogoVelocity = 0.0f;
		mover->pogoLength = 0.0f;
	}

	b3MoverJoint_SetLinearVelocity( mover->moverJointId, mover->velocity );
}

void DynamicMover_Draw( const DynamicMover* mover )
{
	for ( int i = 0; i < mover->probeCount; ++i )
	{
		const DynamicMoverProbe* probe = mover->probes + i;
		b3Pos end = b3OffsetPos( probe->origin, b3MulSV( probe->fraction, probe->translation ) );

		if ( probe->hit == false )
		{
			GizmoLine(B3VecTov128f(probe->origin), B3VecTov128f(end), (u32)b3_colorGray );
			continue;
		}

		b3HexColor color = i == mover->selectedProbe ? b3_colorGreen : b3_colorYellow;
        GizmoLine(B3VecTov128f(probe->origin), B3VecTov128f(end), (u32)color);
		// GizmoCube(B3VecTov128f(end), VecSet1(3.0f), (u32)color);
	}
}
