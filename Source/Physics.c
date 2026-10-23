#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/Terrain.h"
#include "Include/JobSystem.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/BVH.h"
#include "Include/Algorithm.h"
#include "Include/FileSystem.h"
#include "Math/Bitpack.h"

#include <box3d/box3d.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

extern Graphics gGFX; // cpu mega buffers, declared per translation unit as elsewhere (BVH.c)

// finite segment length for picking rays; the ray dir is unit length so fraction*length is world t

#define PHYSICS_SETTINGS_PATH "PhysicsSettings.txt"


#define PHYSICS_TERRAIN_USERDATA (1ull << 32ull)
#define PHYSICS_FOLIAGE_USERDATA (((u64)PhysBit_Foliage) << 32ull)

// single global box3d world, independent of any Scene's lifetime. bodies from every
// scene (including gFoliage's private one) live here so they can physically interact.
static b3WorldId gPhysicsWorld = { 0 };
static JobSystem* gPhysicsJobSystem = NULL;

static void PhysicsDestroyMeshStorage(Scene* scene);
static void PhysicsDestroyLiveStaticColliders(Scene* scene);
static u32 PhysicsCountMeshes(Scene* scene);
static void BuildCollidersForScene(Scene* scene);

extern bool b3IsValidMesh(const b3MeshData* meshData);

b3WorldId Physics_GetWorld(void) { return gPhysicsWorld; }

PhysicsSettings g_PhysicsSettings = {
    .gravity = { 0.0f, -9.81f, 0.0f },
    .substepCount = 4,
    .enableSleep = true,
    .enableContinuous = true
};

static const char* shapeNames[] = { "Capsule", "Compound", "Height", "Hull", "Mesh", "Sphere", "None" };

const char** Physics_ShapeNames() { return shapeNames; }

void Physics_Settings_Save(void)
{
    const PhysicsSettings* s = &g_PhysicsSettings;
    char text[256];
    int n = SDL_snprintf(text, sizeof(text),
        "gravity %f %f %f\nsubsteps %u\nsleep %d\ncontinuous %d\n",
        s->gravity[0], s->gravity[1], s->gravity[2], s->substepCount,
        s->enableSleep ? 1 : 0, s->enableContinuous ? 1 : 0);
    if (n > 0) WriteAllBytes(PHYSICS_SETTINGS_PATH, text, (unsigned long)n);
}

void Physics_Settings_Load(void)
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    if (!FileExist(PHYSICS_SETTINGS_PATH)) return;

    uint64_t size = 0;
    char* text = ReadAllTextAlloc(PHYSICS_SETTINGS_PATH, &size, NULL);
    if (!text) return;

    PhysicsSettings* s = &g_PhysicsSettings;
    int sleep = s->enableSleep, cont = s->enableContinuous;
    for (const char* line = text; *line; )
    {
        // each sscanf is a no-op unless the line begins with its keyword
        SDL_sscanf(line, "gravity %f %f %f", &s->gravity[0], &s->gravity[1], &s->gravity[2]);
        SDL_sscanf(line, "substeps %u", &s->substepCount);
        SDL_sscanf(line, "sleep %d", &sleep);
        SDL_sscanf(line, "continuous %d", &cont);
        while (*line && *line != '\n') line++;
        while (*line == '\n' || *line == '\r') line++;
    }
    s->enableSleep = sleep != 0;
    s->enableContinuous = cont != 0;
    s->substepCount = Minu32(Maxu32(s->substepCount, 1u), 32u);
    FreeAllText(text);
}


void Physics_ApplyWorldSettings(void)
{
    if (B3_IS_NULL(gPhysicsWorld)) return;
    const PhysicsSettings* s = &g_PhysicsSettings;
    b3World_SetGravity(gPhysicsWorld, (b3Vec3){ s->gravity[0], s->gravity[1], s->gravity[2] });
    b3World_EnableSleeping(gPhysicsWorld, s->enableSleep);
    b3World_EnableContinuous(gPhysicsWorld, s->enableContinuous);
}

// b3EnqueueTaskCallback: box3d's b3TaskCallback (void(void*)) matches JobSystemFn directly, so the
// task is queued as-is. Returns the job handle as an opaque userTask that box3d hands back to the
// finish callback. On a full/failed queue we run the task inline and return NULL so box3d skips finish.
static void* PhysicsEnqueueTask(b3TaskCallback* task, void* taskContext,
                         void* userContext, const char* taskName)
{
    (void)taskName;
    JobSystem* jobSystem = (JobSystem*)userContext;
    JobHandle handle = JobSystem_Execute(jobSystem, (JobSystemFn)task, taskContext);
    if (handle == 0) {
        task(taskContext);
        return NULL;
    }
    return (void*)(uintptr_t)(u32)handle;
}

// b3FinishTaskCallback: waits for the job enqueued above to finish.
static void PhysicsFinishTask(void* userTask, void* userContext)
{
    JobSystem* jobSystem = (JobSystem*)userContext;
    JobHandle handle = (JobHandle)(u32)(uintptr_t)userTask;
    JobSystem_WaitJob(jobSystem, handle);
}

static void* PhysicsAllocFn(size_t size, int32_t alignment) {
    return AllocAligned((uint64_t)size, (uint64_t)alignment);
}

static void PhysicsFreeFn(void* mem, size_t size) {
    FreeAligned(mem);
}

static int PhysicsAssertFn(const char* condition, const char* fileName, int lineNumber) {
    AX_ERROR("condition:%s, filename:%s, lineNumber:%d", condition, fileName, lineNumber);
    return IsDebugMode();
}

static void PhysicsLogFn(const char* txt) {
    AX_LOG("%s", txt);
}

void Physics_Init(void)
{
    if (B3_IS_NON_NULL(gPhysicsWorld)) return;

    b3SetAllocator(PhysicsAllocFn, PhysicsFreeFn);
    b3SetAssertFcn(PhysicsAssertFn);
    b3SetLogFcn(PhysicsLogFn);
    b3Version version = b3GetVersion();
    AX_LOG("Box3D version %d.%d.%d\n", version.major, version.minor, version.revision);
    Physics_Settings_Load();

    gPhysicsJobSystem = JobSystem_Create(0, 0);
    b3WorldDef worldDef      = b3DefaultWorldDef();
    worldDef.workerCount     = JobSystem_GetThreadCount(gPhysicsJobSystem);
    worldDef.enqueueTask     = PhysicsEnqueueTask;
    worldDef.finishTask      = PhysicsFinishTask;
    worldDef.userTaskContext = gPhysicsJobSystem;

    gPhysicsWorld = b3CreateWorld(&worldDef);
    Physics_ApplyWorldSettings();
}

void Physics_Destroy(void)
{
    if (B3_IS_NON_NULL(gPhysicsWorld))
        b3DestroyWorld(gPhysicsWorld);
    gPhysicsWorld = b3_nullWorldId;
    if (gPhysicsJobSystem)
        JobSystem_Destroy(gPhysicsJobSystem);
    gPhysicsJobSystem = NULL;
}

void Scene_InitPhysics(Scene* scene)
{
    Physics_Init();
    scene->physics.bodies = (b3BodyId*)AllocZeroTLSF(scene->surfaceSet.maxEntities, sizeof(b3BodyId));
    if (!scene->physics.bodies)
        AX_WARN("physics: body slot allocation failed");
}

void Scene_DestroyPhysics(Scene* scene)
{
    // Scene_BuildStaticCollidersAsync may still be running on a worker thread holding a raw
    // pointer to this Scene (e.g. new scene created right after load, before the build
    // finished) - tearing down now would race its writes. physicsColliderBuildDone is the
    // worker's own completion signal, so wait for it instead of adding a join primitive.
    while (SDL_GetAtomicInt(&scene->physics.colliderBuildRunning) && !SDL_GetAtomicInt(&scene->physics.colliderBuildDone))
        SDL_Delay(1);

    // the world is shared with every other scene now, so this scene's bodies must be
    // destroyed individually instead of tearing down the whole world.
    // terrain colliders are NOT touched here - they're owned per-chunk by MarchingTerrain
    // (tChunk.physicsBody/physicsMesh), not by this scene, and are torn down on their own
    // chunk lifecycle (tFreeChunkSlot/tClearChunkCache/tMarchingDestroy).
    PhysicsDestroyLiveStaticColliders(scene);

    if (scene->physics.bodies) DeAllocTLSF(scene->physics.bodies);
    if (scene->physics.pending) DeAllocTLSF(scene->physics.pending);
    scene->physics.bodies = NULL;
    scene->physics.pending = NULL;
    scene->physics.numPending = 0;
    SDL_SetAtomicInt(&scene->physics.colliderBuildRunning, 0);
    SDL_SetAtomicInt(&scene->physics.colliderBuildDone, 0);
    scene->physics.colliderBuildCallback = NULL;
    scene->physics.colliderBuildResult = 0;
}

// Resolves the body slot for an entity, or NULL when the entity/slot is out of
// range. The slot's body may still be null (no body created yet).
static b3BodyId* PhysicsEntitySlot(Scene* scene, const Entity* entity)
{
    if (!scene || !entity || EntityGetSparseID(entity) == INVALID_ENTITY) return NULL;
    return &scene->physics.bodies[EntityGetSparseID(entity)];
}

static uintptr_t PhysicsBodyUserData(EntityID entityID, bool isFoliage)
{
    return (uintptr_t)entityID | (((uintptr_t)isFoliage) * PHYSICS_FOLIAGE_USERDATA);
}

static bool PhysicsUserDataIsTerrain(void* userData)
{
    return (((uintptr_t)userData) & PHYSICS_TERRAIN_USERDATA) != 0u;
}

static EntityID PhysicsUserDataEntity(void* userData)
{
    return (EntityID)((uintptr_t)userData & 0xFFFFFFFFull);
}

static bool PhysicsUserDataIsFoliage(void* userData)
{
    return ((uintptr_t)userData & PHYSICS_FOLIAGE_USERDATA) != 0;
}

void Physics_Update(Scene* scene, float deltaTime)
{
    if (SDL_GetAtomicInt(&scene->physics.colliderBuildDone))
    {
        SDL_SetAtomicInt(&scene->physics.colliderBuildDone, 0);
        SDL_SetAtomicInt(&scene->physics.colliderBuildRunning, 0);
        if (scene->physics.colliderBuildResult)
        {
            BuildCollidersForScene(scene);
            tInvalidatePhysics();
            AX_LOG("physics: built %u static collider meshes\n", PhysicsCountMeshes(scene));
        }
               
        AsyncCallback callback = scene->physics.colliderBuildCallback;
        scene->physics.colliderBuildCallback = NULL;
        if (callback) callback(scene, scene->physics.colliderBuildResult);
    }
    
    b3World_Step(gPhysicsWorld, deltaTime, (int)g_PhysicsSettings.substepCount);

    // Write moved bodies back onto their entities. Move events only report bodies
    // that actually changed this step (dynamic/kinematic), so we skip the static
    // majority instead of scanning every entity. userData encodes the sparse id
    b3BodyEvents events = b3World_GetBodyEvents(gPhysicsWorld);
    for (int i = 0; i < events.moveCount; i++)
    {
        const b3BodyMoveEvent* move = &events.moveEvents[i];
        Scene* opScene = PhysicsUserDataIsFoliage(move->userData) ? GetFoliageScene() : scene;
        RenderSet* set = &opScene->surfaceSet;
        u32 sparseIdx = PhysicsUserDataEntity(move->userData);
        u32 dense = set->sparseData[sparseIdx].id;
        Entity* entity = &set->entities[dense];
        v128f pos = B3VecTov128f(move->transform.p);
        v128f oldRot = EntityGetRotation(entity);
        v128f rot = B3ToQuat(move->transform.q);
        Scene_AddPosRec(opScene, entity, VecSub(pos, entity->position));
        Scene_RotateRec(opScene, entity, QMul(QInverse(oldRot), rot));
        opScene->renderDataDirty |= 1;
    }
}

static void PhysicsDestroyBodies(b3BodyId* bodies, u32 maxEntities)
{
    if (!bodies) return;
    for (u32 i = 0; i < maxEntities; i++)
    {
        if (B3_IS_NON_NULL(bodies[i]))
            b3DestroyBody(bodies[i]);
        bodies[i] = b3_nullBodyId;
    }
}

static void PhysicsDestroyMeshStorage(Scene* scene)
{
    for (u32 i = 0; i < scene->surfaceSet.maxGroups; i++)
    {
        if (!PTR_VALID(scene->physics.meshes[i])) continue;
        b3DestroyMesh(scene->physics.meshes[i]);
        scene->physics.meshes[i] = NULL;
    }
}

static void PhysicsDestroyLiveStaticColliders(Scene* scene)
{
    PhysicsDestroyBodies(scene->physics.bodies, scene->surfaceSet.maxEntities);
    PhysicsDestroyMeshStorage(scene);
}

static u32 PhysicsCountMeshes(Scene* scene)
{
    u32 count = 0u;
    for (u32 i = 0; i < scene->surfaceSet.numGroups; i++)
        if (scene->physics.meshes[i]) count++;
    return count;
}

static b3MeshData* PhysicsEnsureGroupMesh(Scene* scene, u32 groupIdx)
{
    RenderSet* renderset = &scene->surfaceSet;
    if (groupIdx >= renderset->numGroups || groupIdx == 0) return NULL;
    b3MeshData** meshes = scene->physics.meshes;
    if (b3IsValidMesh(meshes[groupIdx])) return meshes[groupIdx];
    
    PrimitiveGroup* group = &renderset->primitiveGroups[groupIdx];
    if (group->lodNumIndices[0] < 3 || group->lodNumVertices[0] == 0) return NULL;
    if (group->bundleIdx >= scene->numBundles || !scene->bundleRefs[group->bundleIdx].bundle)
    {
        AX_WARN("physics: missing bundle for collider group %u", groupIdx);
        return NULL;
    }
    
    const SceneBundle* bundle = scene->bundleRefs[group->bundleIdx].bundle;
    const APrimitive* prim = &bundle->meshes[group->meshIndex].primitives[group->primitiveIndex];
    u32 vertexOffset = group->lodVertexOffset[0];
    u32 numVertices  = group->lodNumVertices[0];
    u32 indexOffset  = group->lodIndexOffset[0];
    u32 numIndices   = group->lodNumIndices[0];
    if (numIndices < 3 || numVertices == 0) return NULL;
    v128f decodeMin    = VecLoad(prim->min);
    v128f decodeExtent = VecMax(VecSub(VecLoad(prim->max), decodeMin), VecSet1(1.0e-6f));
    
    size_t vertBytes = (size_t)numVertices * sizeof(b3Vec3);
    size_t indexBytes = (size_t)numIndices * sizeof(int32_t);
    b3Vec3*  verts = (b3Vec3*)SDL_malloc(vertBytes);
    int32_t* idx   = (int32_t*)SDL_malloc(indexBytes);
    if (!verts || !idx)
    {
        AX_WARN("physics collider temp allocation failed vertices=%u indices=%u", numVertices, numIndices);
        if (verts) SDL_free(verts);
        if (idx) SDL_free(idx);
        return NULL;
    }
    
    const AVertex* vb = gGFX.SurfaceVertexBuffer + vertexOffset;
    for (u32 v = 0; v < numVertices; v++)
        verts[v] = v128fToB3Vec3(VecAdd(decodeMin, VecMul(UnpackUnorm16x4(vb[v].position), decodeExtent)));
    
    const u32* ib = gGFX.IndexBuffer + indexOffset;
    for (u32 k = 0; k < numIndices; k++)
    {
        u32 index = ib[k];
        if (index < vertexOffset || index >= vertexOffset + numVertices)
        {
            AX_WARN("physics collider skipped: invalid index group=%u mesh=%u primitive=%u k=%u vertexRange=%u..%u index=%u indexOffset=%u numIndices=%u",
                    groupIdx, group->meshIndex, group->primitiveIndex, k, vertexOffset, vertexOffset + numVertices,
                    index, indexOffset, numIndices);
            SDL_free(idx);
            SDL_free(verts);
            return NULL;
        }
        idx[k] = (int32_t)(index - vertexOffset); // mega-absolute -> group-local
    }
    
    b3MeshDef md      = {0};
    md.vertices       = verts;
    md.indices        = idx;
    md.vertexCount    = (int)numVertices;
    md.triangleCount  = (int)(numIndices / 3);
    md.identifyEdges  = true;
    b3MeshData* mesh  = b3CreateMesh(&md, NULL, 0);
    SDL_free(idx);
    SDL_free(verts);
    if (!mesh) return NULL;
    
    meshes[groupIdx] = mesh;
    return mesh;
}

void PhysicsCreateEntityBody(Scene* scene, const Entity* entity, void* data, b3ShapeType shapeType)
{
    EntityFlags entityFlags = EntityGetFlags(entity);
    bool colliderEnabled = (entityFlags & EntityFlags_ColliderEnabled) != 0;
    b3BodyId* slot = PhysicsEntitySlot(scene, entity);
    if (!slot || B3_IS_NON_NULL(slot[0]) || !colliderEnabled || !!(entityFlags & EntityFlags_NoMesh)) return;
    u32 primitiveIdx = EntityGetPrimitiveID(entity);
    if (primitiveIdx >= scene->surfaceSet.numGroups || primitiveIdx == 0) return;
    
    b3MeshData* mesh = PhysicsEnsureGroupMesh(scene, primitiveIdx);
    if (!b3IsValidMesh(mesh) && !data) return; // if there is data this is not mesh collider don't return
    
    b3BodyDef bd  = b3DefaultBodyDef();
    bd.type       = b3_staticBody;
    bd.position   = v128fToB3Vec3(entity->position);
    bd.rotation   = ToB3Quat(EntityGetRotation(entity));
    u32 sparseID  = EntityGetSparseID(entity);
    SparseData sparseData = scene->surfaceSet.sparseData[sparseID];
    bd.userData   = (void*)PhysicsBodyUserData(sparseID | (sparseData.gen << 24), scene->isFoliageScene);
    b3BodyId body = b3CreateBody(gPhysicsWorld, &bd);
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.filter.categoryBits = scene->isFoliageScene ? PhysBit_Foliage : PhysBit_Surface;
    b3ShapeId shape;
    
    switch (shapeType)
    {
        case b3_capsuleShape:  shape = b3CreateCapsuleShape(body, &sd, data); break;
        case b3_hullShape:     shape = b3CreateHullShape(body, &sd, data); break;
        case b3_meshShape:     shape = b3CreateMeshShape(body, &sd, mesh, EntityGetB3Scale(entity)); break;
        case b3_sphereShape:   shape = b3CreateSphereShape(body, &sd, data); break;
        default: shape = b3_nullShapeId; AX_WARN("unsupported physics shape1"); break; // compound / height
    }

    if (B3_IS_NULL(shape)) {
        b3DestroyBody(body);
        return;
    }
    
    *slot = body;
}

bool Entity_IsPhysicsEnabled(Scene* scene, Entity* entity)
{
    u32 sparseId = EntityGetSparseID(entity);
    if (!scene->physics.bodies || sparseId >= scene->surfaceSet.maxEntities) return false;
    b3BodyId body = scene->physics.bodies[sparseId];
    return !B3_IS_NULL(body) && b3Body_IsEnabled(body);
}

void Entity_TogglePhysics(Scene* scene, Entity* entity, bool enabled)
{
    u32 sparseId = EntityGetSparseID(entity);
    if (!scene->physics.bodies || sparseId >= scene->surfaceSet.maxEntities) return;
    b3BodyId body = scene->physics.bodies[sparseId];
    bool isEnabled = !B3_IS_NULL(body) && b3Body_IsEnabled(body);
    if (B3_IS_NULL(body))
    {
        PhysicsCreateEntityBody(scene, entity, NULL, b3_meshShape);
        body = scene->physics.bodies[sparseId];
    }
    if (B3_IS_NULL(body)) return;
    if (isEnabled) b3Body_Enable(body); else b3Body_Disable(body);
    EntityFlags entityFlags = EntityGetFlags(entity);
    entityFlags &= ~EntityFlags_ColliderEnabled;
    entityFlags |= isEnabled * EntityFlags_ColliderEnabled;
    EntitySetFlags(entity, entityFlags);
}

void Entity_SyncPhysicsBody(Scene* scene, const Entity* entity)
{
    b3BodyId* slot = PhysicsEntitySlot(scene, entity);
    if (!slot || B3_IS_NULL(slot[0]))
    {
        PhysicsCreateEntityBody(scene, entity, NULL, b3_meshShape);
        return;
    }
    b3BodyId body = slot[0];
    
    // Rescale the collider mesh only when the entity scale actually changed. A
    // plain move/rotate must not rebuild the mesh tree, since this runs every
    // frame while a gizmo drag is active. The applied scale is read back from
    // the shape, so no per-entity scale needs to be cached alongside the body.
    b3ShapeId shape;
    if (b3Body_GetShapes(body, &shape, 1) > 0 && b3Shape_GetType(shape) == b3_meshShape)
    {
        b3Vec3 newScale = EntityGetB3Scale(entity);
        b3Vec3 curScale = b3Shape_GetMesh(shape).scale;
        if (newScale.x != curScale.x || newScale.y != curScale.y || newScale.z != curScale.z)
        {
            b3MeshData* mesh = PhysicsEnsureGroupMesh(scene, EntityGetPrimitiveID(entity));
            if (mesh) b3Shape_SetMesh(shape, mesh, newScale);
        }
    }
    
    b3Body_SetTransform(body, v128fToB3Vec3(entity->position), EntityGetB3Quat(entity));
}

void Physics_DestroyTerrainChunk(u64* inOutBody, struct b3MeshData** inOutMesh)
{
    if (!inOutBody || !inOutMesh) return;
    b3BodyId body = b3LoadBodyId(*inOutBody);
    if (B3_IS_NON_NULL(body)) b3DestroyBody(body);
    *inOutBody = 0u;
    if (*inOutMesh)
    {
        b3DestroyMesh(*inOutMesh);
        *inOutMesh = NULL;
    }
}

bool Physics_SyncTerrainChunkMesh(u64* inOutBody, struct b3MeshData** inOutMesh,
                                       b3Vec3* vertices, u32 vertexCount,
                                       s32* indices, u32 indexCount)
{
    if (!inOutBody || !inOutMesh || B3_IS_NULL(gPhysicsWorld)) return false;
    if (!vertices || !indices || vertexCount == 0u || indexCount < 3u) return false;
    
    b3MeshDef md     = {0};
    md.vertices      = vertices;
    md.indices       = indices;
    md.vertexCount   = (int)vertexCount;
    md.triangleCount = (int)(indexCount / 3u);
    md.identifyEdges = true;
    b3MeshData* mesh = b3CreateMesh(&md, NULL, 0);
    if (!mesh) {
        AX_WARN("physics: terrain mesh build failed");
        return false;
    }
    
    b3MeshData* oldMesh = *inOutMesh;
    b3BodyId body = b3LoadBodyId(*inOutBody);
    if (B3_IS_NULL(body))
    {
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type     = b3_staticBody;
        bd.userData = (void*)(u64)PHYSICS_TERRAIN_USERDATA;
        body = b3CreateBody(gPhysicsWorld, &bd);
        
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.filter.categoryBits = PhysBit_Terrain;
        b3ShapeId shape = b3CreateMeshShape(body, &sd, mesh, b3Vec3_one);
        if (B3_IS_NULL(shape))
        {
            b3DestroyBody(body);
            b3DestroyMesh(mesh);
            AX_WARN("physics: terrain shape build failed");
            return false;
        }
        
        *inOutBody = b3StoreBodyId(body);
    }
    else
    {
        b3ShapeId shape;
        int numShapes = b3Body_GetShapes(body, &shape, 1);
        b3Shape_SetMesh(shape, mesh, b3Vec3_one);
    }
    
    *inOutMesh = mesh;
    if (oldMesh) b3DestroyMesh(oldMesh);
    return true;
}

b3BodyId Physics_GetPhysicsBody(Scene* scene, const Entity* entity)
{
    return scene->physics.bodies[EntityGetSparseID(entity) ];
}

void Entity_SetPhysicsBodyType(Scene* scene, Entity* entity, b3BodyType type)
{
    b3BodyId body = Physics_GetPhysicsBody(scene, entity);
    b3ShapeId shapeId;
    bool hasShape = b3Body_GetShapes(body, &shapeId, 1) > 0;
    b3Body_SetType(body, type);
    if (type == (u32)b3_dynamicBody && hasShape && b3Body_GetMass(body) <= 0.0f)
        Physics_ApplyDefaultDynamicMass(body, shapeId);
}

bool Entity_SetPhysicsShapeAuto(Scene* scene, const Entity* entity, b3ShapeType type)
{
    b3BodyId* slot = PhysicsEntitySlot(scene, entity);
    RenderSet* set = &scene->surfaceSet;
    u32 groupIdx = EntityGetPrimitiveID(entity);
    if (!slot || B3_IS_NULL(slot[0]) || groupIdx >= set->numGroups) return false;
    
    b3ShapeId shape;
    if (b3Body_GetShapes(slot[0], &shape, 1) <= 0) return false;
    
    // Mesh restores the original triangle collider (rebuilt/cached per group).
    if (type == b3_meshShape)
    {
        b3MeshData* mesh = PhysicsEnsureGroupMesh(scene, groupIdx);
        if (!mesh) return false;
        b3Shape_SetMesh(shape, mesh, EntityGetB3Scale(entity));
        return true;
    }
    
    // Primitives are baked in body-local space. The mesh collider lives in the
    // primitive's model space scaled by the entity world scale, and box3d
    // primitives take no scale, so we fold the scale into their dimensions.
    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    
    v128f scaleV = EntityGetScaleV(entity);
    v128f localMin = VecMul(group->aabbMin, scaleV);
    v128f localMax = VecMul(group->aabbMax, scaleV);
    v128f center = VecMulf(VecAdd(localMin, localMax), 0.5f);
    v128f half   = VecMax(VecMulf(VecSub(localMax, localMin), 0.5f), VecSet1(0.01f));
    f32 hx = VecGetX(half), hy = VecGetY(half), hz = VecGetZ(half);
    b3Vec3 c = v128fToB3Vec3(center);
    
    switch (type)
    {
        case b3_sphereShape:
        {
            b3Sphere sphere = { .center = c, .radius = Maxf32(Maxf32(hx, hy), hz) };
            b3Shape_SetSphere(shape, &sphere);
            return true;
        }
        case b3_capsuleShape:
        {
            // Capsule along Y, radius from the other two half-extents.
            f32 radius   = Maxf32(hx, hz);
            f32 halfSpan = Maxf32(hy - radius, 0.0f);
            b3Capsule capsule = {
                .center1 = { c.x, c.y - halfSpan, c.z },
                .center2 = { c.x, c.y + halfSpan, c.z },
                .radius  = radius
            };
            b3Shape_SetCapsule(shape, &capsule);
            return true;
        }
        case b3_hullShape:
        {
            b3BoxHull box = b3MakeOffsetBoxHull(hx, hy, hz, c);
            b3Shape_SetHull(shape, &box.base);
            return true;
        }
        default:
            AX_WARN("unsupported physics shape");
            return false; // compound / height are not supported from the inspector
    }
}

void Physics_ApplyDefaultDynamicMass(b3BodyId body, b3ShapeId shape)
{
    b3AABB aabb = b3Shape_GetAABB(shape);
    f32 w = Maxf32(aabb.upperBound.x - aabb.lowerBound.x, 0.01f);
    f32 h = Maxf32(aabb.upperBound.y - aabb.lowerBound.y, 0.01f);
    f32 d = Maxf32(aabb.upperBound.z - aabb.lowerBound.z, 0.01f);
    f32 k = 1.0f / 12.0f;
    b3MassData md = {
        .mass = 1.0f,
        .center = { 0.0f, 0.0f, 0.0f },
        .inertia = {
            .cx = { k * (h * h + d * d), 0.0f, 0.0f },
            .cy = { 0.0f, k * (w * w + d * d), 0.0f },
            .cz = { 0.0f, 0.0f, k * (w * w + h * h) }
        }
    };
    b3Body_SetMassData(body, md);
}

bool Physics_GetEntityOverride(const Scene* scene, u32 sparseIdx, ScenePhysicsRecord* out)
{
    if (!scene->physics.bodies || sparseIdx >= scene->surfaceSet.maxEntities) return false;
    b3BodyId body = scene->physics.bodies[sparseIdx];
    if (B3_IS_NULL(body)) return false;
    
    b3ShapeId shape;
    bool hasShape = b3Body_GetShapes(body, &shape, 1) > 0;
    u32 bodyType  = (u32)b3Body_GetType(body);
    u32 shapeType = hasShape ? (u32)b3Shape_GetType(shape) : (u32)b3_meshShape;
    // nothing to persist for the default static-mesh collider
    if (bodyType == (u32)b3_staticBody && shapeType == (u32)b3_meshShape) return false;
    
    b3MotionLocks locks = b3Body_GetMotionLocks(body);
    out->sparseIdx = sparseIdx;
    out->bodyType  = bodyType;
    out->shapeType = shapeType;
    out->lockBits  = (u32)locks.linearX | ((u32)locks.linearY << 1) | ((u32)locks.linearZ << 2)
                   | ((u32)locks.angularX << 3) | ((u32)locks.angularY << 4) | ((u32)locks.angularZ << 5);
    out->friction       = hasShape ? b3Shape_GetFriction(shape) : 0.0f;
    out->restitution    = hasShape ? b3Shape_GetRestitution(shape) : 0.0f;
    out->density        = hasShape ? b3Shape_GetDensity(shape) : 0.0f;
    out->linearDamping  = b3Body_GetLinearDamping(body);
    out->angularDamping = b3Body_GetAngularDamping(body);
    out->gravityScale   = b3Body_GetGravityScale(body);
    out->sleepThreshold = b3Body_GetSleepThreshold(body);
    return true;
}

void Physics_ApplyPendingOverrides(Scene* scene)
{
    RenderSet* set = &scene->surfaceSet;
    for (u32 i = 0; i < scene->physics.numPending; i++)
    {
        const ScenePhysicsRecord* rec = &scene->physics.pending[i];
        if (rec->sparseIdx >= set->maxEntities) continue;
        u32 dense = set->sparseData[rec->sparseIdx].id;
        if (dense >= set->numEntities) continue;
        const Entity* entity = &set->entities[dense];
        b3BodyId* slot = PhysicsEntitySlot(scene, entity);
        if (!slot || B3_IS_NULL(slot[0])) continue;
        b3BodyId body = slot[0];
        
        // swap the shape first so the material/mass below act on the final shape
        if (rec->shapeType != (u32)b3_meshShape)
            Entity_SetPhysicsShapeAuto(scene, entity, (b3ShapeType)rec->shapeType);
        
        b3ShapeId shape;
        bool hasShape = b3Body_GetShapes(body, &shape, 1) > 0;
        if (hasShape)
        {
            b3Shape_SetFriction(shape, rec->friction);
            b3Shape_SetRestitution(shape, rec->restitution);
            b3Shape_SetDensity(shape, rec->density, true);
        }
        b3Body_SetLinearDamping(body, rec->linearDamping);
        b3Body_SetAngularDamping(body, rec->angularDamping);
        b3Body_SetGravityScale(body, rec->gravityScale);
        b3Body_SetSleepThreshold(body, rec->sleepThreshold);
        
        b3MotionLocks locks = {
            .linearX  = (rec->lockBits >> 0) & 1u, .linearY  = (rec->lockBits >> 1) & 1u,
            .linearZ  = (rec->lockBits >> 2) & 1u, .angularX = (rec->lockBits >> 3) & 1u,
            .angularY = (rec->lockBits >> 4) & 1u, .angularZ = (rec->lockBits >> 5) & 1u
        };
        b3Body_SetMotionLocks(body, locks);
        
        // SetType recomputes mass (from density for primitives); a dynamic mesh still
        // reads zero mass, so give it the same default box mass as the inspector.
        if (rec->bodyType != (u32)b3_staticBody)
        {
            b3Body_SetType(body, (b3BodyType)rec->bodyType);
            if (rec->bodyType == (u32)b3_dynamicBody && hasShape && b3Body_GetMass(body) <= 0.0f)
                Physics_ApplyDefaultDynamicMass(body, shape);
        }
    }
    if (scene->physics.pending) DeAllocTLSF(scene->physics.pending);
    scene->physics.pending = NULL;
    scene->physics.numPending = 0;
}

void Physics_DestroyEntityBody(Scene* scene, const Entity* entity)
{
    b3BodyId* slot = PhysicsEntitySlot(scene, entity);
    if (!slot) return;
    if (B3_IS_NON_NULL(slot[0])) b3DestroyBody(slot[0]);
    slot[0] = b3_nullBodyId;
}

static void Physics_DestroyBodiesInRange(Scene* scene, u32 firstGroup, u32 groupCount)
{
    if (!scene || groupCount == 0u) return;
    RenderSet* set = &scene->surfaceSet;
    if (firstGroup >= set->numGroups) return;
    u32 lastGroup = Minu32(firstGroup + groupCount, set->numGroups);
    for (u32 g = firstGroup; g < lastGroup; g++)
    {
        PrimitiveGroup* group = &set->primitiveGroups[g];
        for (u32 e = 0; e < group->numEntities; e++)
            Physics_DestroyEntityBody(scene, &set->entities[group->entityOffset + e]);
    }
}

static void Physics_RemoveGroupMeshes(Scene* scene, u32 firstGroup, u32 groupCount)
{
    if (!scene || groupCount == 0u) return;
    b3MeshData** meshes = scene->physics.meshes;
    RenderSet* set = &scene->surfaceSet;
    groupCount = Minu32(groupCount, set->maxGroups - firstGroup);
    u32 afterRemoved = firstGroup + groupCount;
    u32 last = Minu32(afterRemoved, set->maxGroups);
    for (u32 i = firstGroup; i < last; i++)
    {
        if (!meshes[i]) continue;
        b3DestroyMesh(meshes[i]);
        meshes[i] = NULL;
    }
    if (afterRemoved >= set->maxGroups) return;
    for (u32 i = afterRemoved; i < set->maxGroups; i++)
        meshes[i - groupCount] = meshes[i];
    u32 clearStart = set->maxGroups - groupCount;
    for (u32 i = clearStart; i < set->maxGroups; i++)
        meshes[i] = NULL;
}

void RenderSet_AddEntitiesCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count)
{
    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    u32 cnt = group->numEntities - localStartIdx;
    for (u32 i = 0; i < cnt; i++)
        PhysicsCreateEntityBody(set->hookScene, &set->entities[group->entityOffset + localStartIdx + i], NULL, b3_meshShape);
}

void RenderSet_RemoveRangeCallback(RenderSet* set, u32 groupIdx, u32 localStartIdx, u32 count)
{
    PrimitiveGroup* group = &set->primitiveGroups[groupIdx];
    u32 cnt = group->numEntities - localStartIdx;
    for (u32 i = 0; i < cnt; i++)
        Physics_DestroyEntityBody(set->hookScene, &set->entities[group->entityOffset + localStartIdx + i]);
}

void RenderSet_RemoveGroupsCallback(RenderSet* set, u32 firstGroup, u32 groupCount)
{
    Physics_DestroyBodiesInRange(set->hookScene, firstGroup, groupCount);
    Physics_RemoveGroupMeshes(set->hookScene, firstGroup, groupCount);
}

void RenderSet_ClearEntitiesCallback(RenderSet* set)
{
    Physics_DestroyBodiesInRange(set->hookScene, 0u, set->numGroups);
}

// Builds one triangle-mesh collider per primitive group. This can run on the loader thread because
// it only creates mesh data, not world bodies.
static void BuildColliderMeshesOfScene(Scene* scene)
{
    const RenderSet* set = &scene->surfaceSet;
    for (u32 b = 0; b < set->numBundles; b++)
    {
        const SceneBundle* bundle = set->bundles[b];
        Range range = set->bundlePrimRange[b];
        for (u32 g = range.start; g < range.start + range.count; g++)
        {
            PrimitiveGroup* group = &set->primitiveGroups[g];
            if (group->numEntities == 0 || group->lodNumIndices[0] < 3 || group->lodNumVertices[0] == 0)
                continue;
            PhysicsEnsureGroupMesh(scene, g);
        }
    }
}

// Creates static bodies for built collider meshes. Must run on the main thread: Box3D asserts when
// bodies are created while the world is locked or from async worker mutation paths.
static void BuildCollidersForScene(Scene* scene)
{
    const RenderSet* set = &scene->surfaceSet;
    for (u32 b = 0; b < set->numBundles; b++)
    {
        const SceneBundle* bundle = set->bundles[b];
        Range range = set->bundlePrimRange[b];
        for (u32 g = range.start; g < range.start + range.count; g++)
        {
            PrimitiveGroup* group = &set->primitiveGroups[g];
            if (group->numEntities == 0 || group->lodNumIndices[0] < 3 || group->lodNumVertices[0] == 0)
                continue;
            if (!scene->physics.meshes[g]) continue;

            Entity* ents = set->entities + group->entityOffset;
            for (u32 e = 0; e < group->numEntities; e++)
                PhysicsCreateEntityBody(scene, &ents[e], NULL, b3_meshShape);
        }
    }
}

void Physics_BuildStaticColliders(Scene* scene)
{
    PhysicsDestroyLiveStaticColliders(scene);
    if (!scene->physics.bodies) return;

    u32 maxMeshes = scene->surfaceSet.numGroups + scene->surfaceSet.numGroups;
    if (maxMeshes == 0) return;

    BuildColliderMeshesOfScene(scene);
    BuildCollidersForScene(scene);
    // the wholesale destroy above also dropped every terrain chunk collider; tell the
    // terrain to re-create them on its next update (this can run on the async loader)
    tInvalidatePhysics();
    AX_LOG("physics: built %u static collider meshes\n", PhysicsCountMeshes(scene));
}

static s32 BuildStaticColliderMeshesTask(void* data)
{
    Scene* scene = (Scene*)data;
    BuildColliderMeshesOfScene(scene);
    scene->physics.colliderBuildResult = 1;
    SDL_SetAtomicInt(&scene->physics.colliderBuildDone, 1);
    scene->physics.buildTask = NULL;
    return 1;
}

void Physics_BuildStaticCollidersAsync(Scene* scene, AsyncCallback callback)
{
    if (SDL_GetAtomicInt(&scene->physics.colliderBuildRunning))
    {
        AX_WARN("physics collider build already running");
        return;
    }

    PhysicsDestroyLiveStaticColliders(scene);
    scene->physics.colliderBuildCallback = callback;
    scene->physics.colliderBuildResult = 0;
    SDL_SetAtomicInt(&scene->physics.colliderBuildDone, 0);
    SDL_SetAtomicInt(&scene->physics.colliderBuildRunning, 1);
    scene->physics.buildTask = AsyncRun("create colliders task", BuildStaticColliderMeshesTask, NULL, scene);
}

// box3d picking against the static surface colliders. mirrors the BVHHit contract of the cpu-BVH
// path so BVH_RaycastScene can keep whichever of (static physics, skinned cpu-BVH) hit is nearer.
// only writes *hit when the physics hit is closer than the current hit->hit.t. out: 1 when written.
s32 Physics_PhysicsRaycastPick(v128f origin, v128f dir, float rayLen, BVHHit* hit, u64 mask)
{
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits = mask;

    b3RayResult r = b3World_CastRayClosest(gPhysicsWorld, v128fToB3Vec3(origin),
                                           v128fToB3Vec3(VecMulf(dir, rayLen)), filter);
    if (!r.hit) return 0;

    f32 t = r.fraction * rayLen; // dir is unit length, so this is world distance
    if (t >= hit->hit.t) return 0;

    // body userData packs the sparse id plus the render-set kind; resolve it to (group, local entity)
    void* userData = b3Body_GetUserData(b3Shape_GetBody(r.shapeId));
    if (PhysicsUserDataIsTerrain(userData))
    {
        hit->hit.t      = t;
        hit->hit.u      = 0.0f;
        hit->hit.v      = 0.0f;
        hit->skinnedSet = 0xFFFFFFFFu;
        hit->bundleIdx  = 0xFFFFFFFFu;
        hit->groupIdx   = 0u;
        hit->groupLocalID  = 0u; // no chunk id in userData anymore, see PHYSICS_TERRAIN_USERDATA
        hit->triIndex   = (u32)r.triangleIndex;
        return 1;
    }

    EntityID entityID = PhysicsUserDataEntity(userData);
    u32 sparse = entityID & 0x00FFFFFFu;
    bool isFoliage = PhysicsUserDataIsFoliage(userData);
    Scene* scene = isFoliage ? Foliage_GetScene() : GetActiveScene();
    hit->scene = scene;
    const RenderSet* set = &scene->surfaceSet;
    if (sparse >= set->maxEntities) return 0;
    u32 dense = set->sparseData[sparse].id;
    if (dense == INVALID_ENTITY || dense >= set->numEntities) return 0;

    u32 groupIdx = EntityGetPrimitiveID(&set->entities[dense]);
    if (groupIdx >= set->numGroups) return 0;

    hit->hit.t        = t;
    hit->hit.u        = 0.0f;
    hit->hit.v        = 0.0f;
    hit->skinnedSet   = 0;
    hit->groupIdx     = groupIdx;
    hit->groupLocalID = dense - set->primitiveGroups[groupIdx].entityOffset;
    hit->entityID     = entityID;
    hit->triIndex     = (u32)r.triangleIndex;
    // resolve the render group back to the scene bundle for reporting. the group carries its
    // owning render bundle slot and the scene keeps a reverse map, so this is O(1).
    if (hit->skinnedSet != 0xFFFFFFFFu || hit->bundleIdx != 0xFFFFFFFFu)
        hit->bundleIdx = Scene_FindBundleForRenderGroup(scene, hit->skinnedSet != 0, hit->groupIdx);
    return 1;
}
