
#include "Gameplay.h"

#include <box3d/box3d.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include "Include/Scene.h"
#include "Include/Terrain.h"
#include "Include/Camera.h"
#include "Include/BVH.h"
#include "Include/AssetManager.h"
#include "Include/Editor.h"
#include "Include/Rendering.h" // light

#include "Include/UIWindow.h"
#include "Include/UIRenderer.h"

extern Camera g_Camera;
extern SDL_GPUDevice* g_GPUDevice;
Gameplay g_Gameplay;

static const char* modelPaths[NumTools] = {
    "Assets/Meshes/Character/Character.gltf",
    "Assets/Meshes/Pickaxe/scene.gltf",
    "Assets/Meshes/Axe/scene.gltf",
    "Assets/Meshes/Shovel/scene.gltf"
};

static const char* toolNames[NumTools] = {
    "Character", "Pickaxe", "Axe", "Shovel"
};

static SceneBundleStage* loadedModels[NumTools] = { 0, 0, 0, 0 };

static void OnObjectLoaded(ModelID modelId)
{
    SceneBundleStage* stage = loadedModels[modelId];
    loadedModels[modelId] = NULL;
    Gameplay* gp = (Gameplay*)&g_Gameplay;
    Character* ch = &gp->character;
    Scene* scene = gp->scene;
    u32 bundleIdx = Scene_AddBundleFinalize(scene, stage);
    if (bundleIdx == INVALID_BUNDLE) {
        SDL_Log("import to scene failed: %s", stage->path);
        return;
    }

    SpawnResult sparseResult = Scene_Spawn(scene, bundleIdx, VecZero(), QIdentity(), VecOne(), EntityFlags_None);
    EntityID id = EntityIDFromSparseData(scene, sparseResult.sparse);
    Entity* entity = Scene_GetEntity(scene, id);
    if (modelId == ToolNone)
    { 
        EntityScaleRec(scene, entity, VecSet1(ch->scale), true);
        ch->bundle = scene->bundleRefs[bundleIdx].bundle;
        ch->sparse = sparseResult.sparse;
        ch->numEntities = sparseResult.numEntityAdded;
        ch->entity = id;
        SpawnCharacter(ch, scene, entity, bundleIdx);
    }
    else
    {
        Tool* tool = gp->tools + modelId -1;
        tool->numEntity = sparseResult.numEntityAdded;
        tool->id = id;
    }
}

static void OnBeforeDestroyScene(void* ctx, void* scene)
{
    Gameplay* gp = (Gameplay*)ctx;
    Scene_RemoveEntity((Scene*)scene, gp->character.entity);
    for (s32 i = 0; i < NumTools; i++)
        Scene_RemoveEntity((Scene*)scene, gp->tools[i].id);
    gp->character.entity = gp->sphereID = gp->axe.id = gp->pickAxe.id = INVALID_ENTITY;
}

static s32 LoadObjectAsync(void* userData)
{
    u64 idx = (u64)userData;
    loadedModels[idx] = Scene_AddBundleFromPathStage(GetActiveScene(), modelPaths[idx]);
    return 1;
}

static void OnOpenScene(void* g, void* sc)
{
    Gameplay* gp = (Gameplay*)g;
    gp->scene = (Scene*)sc;
    u32 sphereBundle = Scene_AddBundleCached(gp->scene, GetUnitRock(), "Rock");
    gp->sphereID = EntityIDFromSparseData(gp->scene,
        Scene_Spawn(gp->scene, sphereBundle, VecZero(), QIdentity(), VecOne(), EntityFlags_None).sparse);
    
    for (u64 i = 0; i < NumTools; i++)
        AsyncRun(toolNames[i], LoadObjectAsync, NULL, (void*)i);
}

static void AxeHitTest()
{
    BVHHit hit = {};
    v128f origin = Vec3Load(&g_Camera.position.x);
    v128f direction = VecNorm(Vec3Load(&g_Camera.front));
    hit.hit.t = FLT_MAX;

    if ((GetMousePressed(MouseButton_Left) || GetGamepadButtonPressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) 
        && Physics_PhysicsRaycastPick(origin, direction, 10.0f, &hit, PhysBit_Foliage))
    {
        RenderSet* rs = &Foliage_GetScene()->surfaceSet;
        SparseData* sparse = &rs->sparseData[hit.entityID & 0x00FFFFFFu];
        if (sparse->parent) sparse -= sparse->parent;
        Entity* entity = &rs->entities[sparse->id];
        b3BodyId body = Physics_GetPhysicsBody(Foliage_GetScene(), entity);

        b3ShapeId shapeId;
        b3Body_GetShapes(body, &shapeId, 1);
        b3Body_SetType(body, b3_dynamicBody);
        Physics_ApplyDefaultDynamicMass(body, shapeId);
        b3Body_ApplyForceToCenter(body, (b3Vec3){0.0f, 100.0f, 1.0f}, true);
    }
}

static mat4x4 GetBone(void* mapped, u32 joint, Character* ch, Entity* character)
{
    ALIGNSIMD u32 buffer[AnimMtxNumF32];
    MemCopy(buffer, (u32*)mapped + (joint * AnimMtxNumF32), sizeof(buffer));
    mat4x4 boneMat = UnpackGPUBone(buffer);
    mat4x4 bindMatrix = M44Inverse(ch->anim.invBind[joint - ch->anim.jointOffset]);
    bindMatrix = M44Multiply(bindMatrix, boneMat);
    return M44Multiply(bindMatrix,
                M44PositionRotationScaleVec(VecZero(), QNorm(EntityGetRotation(character)), VecSet1(ch->scale)));
}

static void HandleInputs(Character* ch, Gameplay* gp)
{
    if (GetKeyPressed(SDLK_SPACE) || GetGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH))
        ch->anim.jumpRequested = true;

    if (GetKeyPressed(SDLK_L) || GetGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
        cPlayOnce(&ch->anim, ch->anim.atack, 0.35f, 1.0f, true);

    if (GetKeyPressed(SDLK_J) || GetKeyPressed(SDLK_K))
    {
        ch->active = !ch->active;
        ch->position = (b3Vec3){ 0.0f, 10.0f, 0.0f };
        ch->verticalSpeed = 0.0f;
        ch->grounded = false;
        CameraMode wantedMode = GetKeyPressed(SDLK_K) ? CameraMode_FPS : CameraMode_TPS;
        wantedMode = ch->active ? wantedMode : CameraMode_Fly;
        CameraSwitchMode(&g_Camera, wantedMode);

        if (ch->active == false)
            g_Camera.position = F3Sub(g_Camera.position, F3MulF(g_Camera.front, 6.0f));
    }

    f32 shift = GetKeyDown(SDLK_LSHIFT) ? -1.0f : 1.0f;
    if (GetKeyDown(SDLK_0) && shift == -1.0f) gp->currentTool = ToolNone;
    if (GetKeyDown(SDLK_1) && shift == -1.0f) gp->currentTool = ToolAxe;
    if (GetKeyDown(SDLK_2) && shift == -1.0f) gp->currentTool = ToolPickaxe;
    if (GetKeyDown(SDLK_3) && shift == -1.0f) gp->currentTool = ToolShovel;

    const f32 delta = 0.01f;
    if (GetKeyDown(SDLK_KP_1)) gp->cameraOffset.x  += delta * shift;
    if (GetKeyDown(SDLK_KP_2)) gp->cameraOffset.y  += delta * shift;
    if (GetKeyDown(SDLK_KP_3)) gp->cameraOffset.z  += delta * shift;
    if (GetKeyDown(SDLK_KP_4)) gp->cameraXRotation += delta * shift * MATH_PI;
}

static void CharacterUI(Character* ch, Gameplay* gp)
{
    static bool open = true;
    if (UIBeginWindowId(CLAY_ID("Character"), "Character",
                        (float2) { 1400.0f, 40.0f }, (float2) { 500.0f, 260.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIText("Character Settings");
        UIEditFloat("speed", &ch->speed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("jump force", &ch->jumpSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloatN("cameraOffset", &gp->cameraOffset.x, 3, -4.0f, 4.0f, 3);
        UIEditFloat("cameraXRot", &gp->cameraXRotation, 0.0f, 100.0f, 0.1f, 3);
        UIReadonlyRow(CLAY_ID("CharVelocityR"), "velocity", UIFmtV3(&ch->velocity.x, 3));
        f32 sqr = b3LengthSquared(ch->velocity);
        UIReadonlyRow(CLAY_ID("CharSpeedR"), "speed", UIFmtF(Sqrtf(sqr + 0.001f), 3));
        u32 toolIdx = gp->currentTool;
        if (toolIdx != ToolNone && gp->tools[toolIdx].id != INVALID_ENTITY) 
        {
            const Tool* tool = gp->tools + toolIdx - 1;
            UIEditFloatN("tool offset", (f32*)&tool->offset, 3, -1.0f, 1.0f, 3);
            UIEditFloatN("tool euler", (f32*)&tool->euler, 3, -180.0f, 180.0f, 3);
            UIEditFloat("tool scale", (f32*)&tool->scale, -1.0f, 1.0f, 0.01f, 3);
        }
        UIEndWindow();
    }
}

static void DebugSphere(Gameplay* gp, Entity* character)
{
    Entity* sphereEntity = Scene_GetEntity(gp->scene, gp->sphereID);
    if (sphereEntity)
    {
        v128f neckPos = VecAdd(EntityGetPosV(character), M44ExtractPositionV(gp->headBoneMat));
        // EntitySetPositionV(sphereEntity, neckPos);
        EntitySetScaleV(sphereEntity, VecSet1(0.07f));
        LightGPU light;
        MemsetZero(&light, sizeof(LightGPU));
        neckPos = VecAddf(neckPos, 3.4f);
        Vec3Store(light.positionRadius, neckPos);
        light.positionRadius[3] = 7.0f;
        f32 directionCone[4] = { 0.0f, -1.0f, 0.0f, 0.0f };
        f32 color[3] = { 1.0f, 0.9f, 0.75f };
        LightGPU_SetDirectionCone(&light, directionCone);
        LightGPU_SetColor3(&light, color);
        LightGPU_SetIntensity(&light, 16.5f);
        light.type = LightType_Point;
        light.flags = LIGHT_FLAG_SHADOWED;
        light.shadowIndex = 0;
        RendererSetLights(&light, 1);
    }
}

void HoldTool(mat4x4 hand, Gameplay* gp, const Tool* tool)
{
    Entity* entity = RenderSet_GetEntity(&gp->scene->surfaceSet, tool->id);
    if (!entity) return;
    
    RenderSet* surfaceSet = &gp->scene->surfaceSet;
    SparseData* axeSparse = surfaceSet->sparseData + EntityGetSparseID(entity);
    v128f position = Vec3Load(&gp->character.position.x); 
    position = VecAdd(M44ExtractPositionV(hand), position);
    v128f rotation = QMul(QFromEulerVec3(F3MulF(tool->euler, MATH_DegToRad)), M44ExtractRotation(hand));
    GizmoSetLineWidth(0.006f);
    hand = M44FromQuaternionV(rotation);
    GizmoDrawRing(position, hand.r[1], 0.05f, 314159265u | 0xFF000000u);
    position = VecAdd(position, VecMulf(hand.r[0], VecGetX(tool->offset)));
    position = VecAdd(position, VecMulf(hand.r[1], VecGetY(tool->offset)));
    position = VecAdd(position, VecMulf(hand.r[2], VecGetZ(tool->offset)));
    
    for (u32 i = 0; i < tool->numEntity; i++)
    {
        Entity* obj = &surfaceSet->entities[axeSparse[i].id];
        EntitySetPositionV(obj, position);
        EntitySetRotation(obj, rotation);
        EntitySetScaleV(obj, VecSet1(tool->scale));
    }
}

void Gameplay_UseBones(SDL_GPUFence* fence)
{
    Gameplay* gp = &g_Gameplay;
    Character* ch = &gp->character;
    Entity* character = RenderSet_GetEntity(&gp->scene->skinnedSet, gp->character.entity);
    if (!character || !fence) return;

    if (gp->boneTransfer && SDL_WaitForGPUFences(g_GPUDevice, true, &fence, 1))
    {
        void* mapped = SDL_MapGPUTransferBuffer(g_GPUDevice, gp->boneTransfer, false);
        if (mapped)
        {
            gp->headBoneMat = GetBone(mapped, ch->anim.headJoint, ch, character);
            gp->handBoneMat = GetBone(mapped, ch->anim.rightHandJoint, ch, character);
            SDL_UnmapGPUTransferBuffer(g_GPUDevice, gp->boneTransfer);
        }
        SDL_ReleaseGPUFence(g_GPUDevice, fence);
    }
    else
        SDL_ReleaseGPUFence(g_GPUDevice, fence);

    if (ch->active)
        g_Camera.position = F3Add(M44ExtractPosition(gp->headBoneMat), g_Camera.position);

    if (gp->currentTool != ToolNone)
    {
        mat4x4 hand = gp->handBoneMat;
        hand.r[0] = VecNorm(hand.r[0]);
        hand.r[1] = VecNorm(hand.r[1]);
        hand.r[2] = VecNorm(hand.r[2]);
        HoldTool(hand, gp, gp->tools + gp->currentTool - 1);
    }
}

void Gameplay_InitEvents()
{
    SceneManager* sm = GetSceneManager();
    EventAdd(&sm->OpenEvent, OnOpenScene, &g_Gameplay);
    EventAdd(&sm->BeforeDestroyEvent, OnBeforeDestroyScene, &g_Gameplay);
    g_Gameplay.boneTransfer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size  = sizeof(u32) * AnimMtxNumF32 * MAX_BONES
    });
}

void Gameplay_Init()
{
    g_Gameplay.gravity            = -25.0f;
    g_Gameplay.groundMinNormalY   = 0.7f;
    g_Gameplay.moveTolerance      = 0.001f;
    g_Gameplay.cameraOffset       = (float3){ 0.0f, -0.0, 0.3f };
    g_Gameplay.cameraXRotation    = MATH_HalfPI;
    g_Gameplay.boneTransfer       = NULL;
    g_Gameplay.character.scale    = 1.8f;
    g_Gameplay.character.rotation = QIdentity();
    g_Gameplay.character.entity   = INVALID_ENTITY;
    g_Gameplay.sphereID           = INVALID_ENTITY;
    g_Gameplay.axe.id             = INVALID_ENTITY;
    g_Gameplay.pickAxe.id         = INVALID_ENTITY;
    g_Gameplay.boneTransfer = NULL;
    g_Gameplay.currentTool  = ToolNone;
    
    g_Gameplay.axe.scale    = 0.0025f;
    g_Gameplay.axe.offset   = VecSetR(-0.05, 0.4f, -0.176f, 0.0f);
    g_Gameplay.axe.euler    = (float3){-142.0f , 91.0f , 140.0f };
    
    g_Gameplay.pickAxe.scale  = 0.5f;
    g_Gameplay.pickAxe.offset = VecSetR(-0.15f, 0.4f, 0.05f, 0.0f);
    g_Gameplay.pickAxe.euler  = (float3){ 0.0f, 0.0f, -90.0f };

    g_Gameplay.shovel.scale  = 1.5f;
    g_Gameplay.shovel.offset = VecSetR(0.18f, 1.5f, 0.05f, 0.0f);
    g_Gameplay.shovel.euler  = (float3){ 10.0f, 0.0f, 90.0f };
}

void Gameplay_Update(f32 dt)
{
    AxeHitTest();
    for (s32 i = 0; i < NumTools; i++)
        if (loadedModels[i]) OnObjectLoaded(i);
    
    Gameplay* gp = &g_Gameplay;
    Entity* character = RenderSet_GetEntity(&gp->scene->skinnedSet, gp->character.entity);
    if (character == NULL) return;
    Character* ch = &gp->character;
    
    CharacterUI(ch, &g_Gameplay);
    HandleInputs(ch, &g_Gameplay);
    
    CharacterMovement(&g_Gameplay, character, dt);
    cAnimate(gp->scene, ch, dt);

    // must come after character movement because ch->rotation
    if (ch->active)
    {
        float3 cameraOffset = gp->cameraOffset;
        if (g_Camera.mode != CameraMode_FPS) cameraOffset.y += VecGetY(ch->size) * 0.7f;
        g_Camera.position = F3Add(B3VecToFloat3(ch->position), QMulVec3(cameraOffset, ch->rotation));
    }
    g_Camera.target = g_Camera.position;

    GizmoSetLineWidth(0.02f);
    GizmoDrawCapsuleAt(B3VecTov128f(ch->position), VecGetY(ch->size), VecGetZ(ch->size), 0xFF0000FFu);
    // DebugSphere(&g_Gameplay, character);
}

void Gameplay_Destroy()
{
    SceneManager* sm = GetSceneManager();
    sm->OpenEvent.numAction = sm->BeforeDestroyEvent.numAction = 0;

    if (g_Gameplay.boneTransfer) SDL_ReleaseGPUTransferBuffer(g_GPUDevice, g_Gameplay.boneTransfer);
    g_Gameplay.boneTransfer = NULL;
}



