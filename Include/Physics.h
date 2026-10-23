#ifndef C_PHYSICS
#define C_PHYSICS

#include "RenderLimits.h"
#include "Entity.h"
#include "Async.h"

#include <box3d/id.h>
#include <box3d/math_functions.h>
#include <box3d/types.h>

#include <SDL3/SDL_atomic.h>

/////////////////////////////
//         PHYSICS         //
/////////////////////////////
#define PHYS_PICK_MAX_DIST   1.0e5f

// collision categories for static geometry. scene picking filters to surface only, matching the
// old cpu-BVH picking 
enum PhysBit_
{
    PhysBit_Surface  = 1ull,
    PhysBit_Terrain  = 2ull,
    PhysBit_Player   = 4ull,
    PhysBit_Foliage  = 8ull
};
typedef u32 PhysBit;

// persisted physics overrides for a single surface entity. only entities that deviate
// from the default static-mesh collider are saved; body/shape stored as b3BodyType /
// b3ShapeType, lockBits packs the six b3MotionLocks (linear xyz then angular xyz).
typedef struct ScenePhysicsRecord_
{
    u32 sparseIdx;
    u32 bodyType;
    u32 shapeType;
    u32 lockBits;
    f32 friction, restitution, density;
    f32 linearDamping, angularDamping, gravityScale, sleepThreshold;
} ScenePhysicsRecord;

// global (not per-scene) physics world settings, persisted to PhysicsSettings.txt at the
// working directory root. applied to each scene's world on creation and on edit.
typedef struct PhysicsSettings_
{
    f32  gravity[3];
    u32  substepCount;
    bool enableSleep;
    bool enableContinuous;
} PhysicsSettings;

extern PhysicsSettings g_PhysicsSettings;

typedef struct PhysicsSystem_
{
    bool ready;
    // static collision mesh handles, one per primitive group of each static render set. shapes
    // reference these (box3d does not copy mesh data), so they must outlive the world.
    struct b3MeshData*   meshes[MAX_GROUP];
    b3BodyId*            bodies;
    SDL_AtomicInt        colliderBuildRunning;
    SDL_AtomicInt        colliderBuildDone;
    AsyncCallback        colliderBuildCallback;
    s32                  colliderBuildResult;
    // physics overrides parsed from a .scene file, applied once the async collider
    // build finishes (bodies do not exist until then). tlsf-owned, freed on apply.
    ScenePhysicsRecord* pending;
    u32                 numPending;
    AsyncTask*          buildTask;
} PhysicsSystem;

// the box3d world is a single global, scene independent instance (Physics_Init creates
// it lazily on first use) so bodies from different scenes - including gFoliage's private
// scene - can physically interact. Scene_InitPhysics/Scene_PhysicsDestroy only manage the
// scene's own body/mesh bookkeeping inside that shared world.
void Physics_Init(void);
void Physics_Destroy(void);
b3WorldId Physics_GetWorld(void);

void Scene_InitPhysics(struct Scene_* scene);
void Scene_DestroyPhysics(struct Scene_* scene);
void Physics_Update(struct Scene_* scene, f32 deltaTime);

// loads/saves g_PhysicsSettings from PhysicsSettings.txt (load is one-shot, cached).
void Physics_Settings_Load(void);
void Physics_Settings_Save(void);
// pushes g_PhysicsSettings (gravity/sleep/continuous) onto the shared physics world.
void Physics_ApplyWorldSettings(void);

const char** Physics_ShapeNames(); // b3ShapeType

// builds a static rigid body with a triangle-mesh collider for every static mesh instance in the
// scene's surface render sets. call once after a scene finishes loading.
void Physics_BuildStaticCollidersAsync(struct Scene_* scene, AsyncCallback callback);
void Physics_BuildStaticColliders(struct Scene_* scene);
// yea buddy
b3BodyId Physics_GetPhysicsBody(struct Scene_* scene, const Entity* entity);
// after an entity moved call this to update its transformation in physics system
void Entity_SyncPhysicsBody(struct Scene_* scene, const Entity* entity);
void Entity_TogglePhysics(struct Scene_* scene, Entity* entity, bool enabled);
bool Entity_IsPhysicsEnabled(struct Scene_* scene, Entity* entity);
// kinematic, dynamic, static
void Entity_SetPhysicsBodyType(struct Scene_* scene, Entity* entity, b3BodyType type);
// sphere, box, hull, mesh
bool Entity_SetPhysicsShapeAuto(struct Scene_* scene, const Entity* entity, b3ShapeType type);

// terrain colliders are owned per-chunk (u64 stored body id + mesh pointer live on tChunk),
// not by the scene - no shared slot pool/cap, just the global physics world. inOutBody/
// inOutMesh are read (0/NULL means "none yet") and written back by these calls.
bool Physics_SyncTerrainChunkMesh(u64* inOutBody, struct b3MeshData** inOutMesh,
                                  b3Vec3* vertices, u32 vertexCount,
                                  s32* indices, u32 indexCount);
void Physics_DestroyTerrainChunk(u64* inOutBody, struct b3MeshData** inOutMesh);

// Gives a dynamic body a default box mass from the shape AABB so it responds to gravity
// (mesh shapes compute zero mass). Shared by the inspector and the scene loader.
void Physics_ApplyDefaultDynamicMass(b3BodyId body, b3ShapeId shape);
// Reads the surface body at sparseIdx into *out; returns false when it matches the default
// static-mesh collider (nothing to persist). Used by the serializer to save only overrides.
bool Physics_GetEntityOverride(const struct Scene_* scene, u32 sparseIdx, ScenePhysicsRecord* out);
// Applies scene->pendingPhysics onto the freshly built bodies, then frees the buffer.
void Physics_ApplyPendingOverrides(struct Scene_* scene);

void PhysicsDestroyEntityBody(struct Scene_* scene, const Entity* entity);

void PhysicsCreateEntityBody(struct Scene_* scene, const Entity* entity, const void* data, b3ShapeType shapeType);

static inline b3Vec3 v128fToB3Vec3(v128f v)   { return (b3Vec3){ VecGetX(v), VecGetY(v), VecGetZ(v) }; }
static inline b3Vec3 Float3ToB3Vec3(float3 v) { return (b3Vec3){ v.x, v.y, v.z }; }
static inline float3 B3VecToFloat3(b3Pos p)   { return (float3){p.x, p.y, p.z }; }
static inline v128f  B3VecTov128f(b3Pos p)    { return VecSetR(p.x, p.y, p.z, 0.0f); }

purefn v128f B3ToQuat(b3Quat q) {
    return VecSetR(q.v.x, q.v.y, q.v.z, q.s); 
}

purefn b3Quat ToB3Quat(v128f q)
{
    b3Quat res;
    q = QNorm(q);
    VecStore(&res.v.x, q);
    return res;
}

purefn u64 B3QuatToEntityRotation(b3Quat q) { return PackQuaternionS16NormRet(VecSetR(q.v.x, q.v.y, q.v.z, q.s)); }

purefn b3Vec3 b3ProjectOnPlane(b3Vec3 v, b3Vec3 n) {
    return b3Sub(v, b3MulSV(b3Dot(v, n), n));
}

purefn b3Vec3 EntityGetB3Pos(const Entity* e) {
    return v128fToB3Vec3(EntityGetPosV(e));
}

purefn b3Vec3 EntityGetB3Scale(const Entity* e) {
    return Float3ToB3Vec3(EntityGetScale(e));
}

purefn b3Quat EntityGetB3Quat(const Entity* e) {
    return ToB3Quat(EntityGetRotation(e));
}

#endif // C_PHYSICS