
#include "Gameplay.h"

#include <box3d/box3d.h>

#include "Include/Scene.h"
#include "Include/Terrain.h"
#include "Include/Camera.h"
#include "Include/BVH.h"

extern Camera g_Camera;
extern SDL_GPUDevice* g_GPUDevice;
Gameplay g_Gameplay;

static void OnCharacterLoaded(SceneAsyncRequest* request)
{
    Scene* scene = GetActiveScene();
    u32 bundleIdx = Scene_AddBundleFinalize(scene, request->sceneBundleStage);
    if (bundleIdx == INVALID_BUNDLE) {
        AX_ERROR("import to scene failed: %s", request->path);
        return;
    }
    Character* ch = &((Gameplay*)request->context)->character;
    ch->bundle = scene->bundleRefs[bundleIdx].bundle;
    SpawnCharacter(ch);
}

static void OnBeforeDestroyScene(void* sc, void* scene)
{
    Character* ch = (Character*)sc;
    if (ch->entity != INVALID_ENTITY) Scene_RemoveEntity(GetActiveScene(), ch->entity);
    if (ch->sphereID != INVALID_ENTITY) Scene_RemoveEntity(GetActiveScene(), ch->sphereID);
    ch->entity = ch->sphereID = INVALID_ENTITY;
}

static void OnOpenScene(void* g, void* sc)
{
    Gameplay* gp = (Gameplay*)g;
    gp->scene = (Scene*)sc;
    SceneAsyncBegin(SceneAsyncOp_ImportMesh, "Assets/Meshes/Character/Character.gltf",
                    "LoadCharacter", OnCharacterLoaded, gp);
}

static void UpdatePicking()
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

static void GetBones(Scene* scene, Character* ch, Entity* character)
{
    u32 headNode = ch->anim.headJoint;
    u32 dense = ch->sparse->id;
    u32 base = dense * MAX_BONES * AnimMtxNumF32;

    if (g_Gameplay.boneTransfer && g_Gameplay.boneFence && SDL_QueryGPUFence(g_GPUDevice, g_Gameplay.boneFence))
    {
        void* mapped = SDL_MapGPUTransferBuffer(g_GPUDevice, g_Gameplay.boneTransfer, false);
        if (mapped)
        {
            ALIGNSIMD u32 buffer[AnimMtxNumF32];
            MemCopy(buffer, (u32*)mapped + (headNode * AnimMtxNumF32), sizeof(buffer));
            mat4x4 boneMat = UnpackGPUBone(buffer);
            mat4x4 bindMatrix = M44Inverse(ch->anim.invBind[ch->anim.headJoint - ch->anim.jointOffset]);
            boneMat = M44Multiply(bindMatrix, boneMat);
            boneMat = M44Multiply(boneMat, M44PositionRotationScaleVec(VecZero(), EntityGetRotation(character), VecSet1(ch->scale)));
            g_Gameplay.boneMat = boneMat;
            SDL_UnmapGPUTransferBuffer(g_GPUDevice, g_Gameplay.boneTransfer);
        }
        SDL_ReleaseGPUFence(g_GPUDevice, g_Gameplay.boneFence);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUBuffer(copyPass,
                              &(SDL_GPUBufferRegion){ 
                              .buffer = scene->animSystem.boneBuffer, 
                              .offset = base * sizeof(u32), 
                              .size   = sizeof(u32) * AnimMtxNumF32 * MAX_BONES },
                              &(SDL_GPUTransferBufferLocation){
                              .transfer_buffer = g_Gameplay.boneTransfer,
                              .offset = 0});
    SDL_EndGPUCopyPass(copyPass);
    g_Gameplay.boneFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
}

static void HandleInputs(Character* ch, Gameplay* gp)
{
    if (GetKeyPressed(SDLK_SPACE) || GetGamepadButtonPressed(SDL_GAMEPAD_BUTTON_SOUTH)) 
        ch->anim.jumpRequested = true;

    if (GetMouseDown(MouseButton_Left) || GetGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
        cPlayOnce(&ch->anim, ch->anim.atack, 0.35f, 1.5f, true);

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
    const f32 delta = 0.01f;
    if (GetKeyDown(SDLK_KP_1)) gp->cameraOffset.x  += delta * shift;
    if (GetKeyDown(SDLK_KP_2)) gp->cameraOffset.y  += delta * shift;
    if (GetKeyDown(SDLK_KP_3)) gp->cameraOffset.z  += delta * shift;
    if (GetKeyDown(SDLK_KP_4)) gp->cameraXRotation += delta * shift * MATH_PI;
}

void Gameplay_Init()
{
    g_Gameplay.gravity            = -25.0f;
    g_Gameplay.groundMinNormalY   = 0.7f;
    g_Gameplay.snapDistance       = 0.3f;
    g_Gameplay.moveTolerance      = 0.001f;
    g_Gameplay.cameraOffset       = (float3){ 0.0f, -1.33, 0.3f };
    g_Gameplay.cameraXRotation    = MATH_HalfPI;
    g_Gameplay.boneTransfer       = NULL;
    g_Gameplay.boneFence          = NULL;
    g_Gameplay.character.scale    = 1.8f;
    g_Gameplay.character.sphereID = INVALID_ENTITY;
    g_Gameplay.character.entity   = INVALID_ENTITY;
    SceneManager* sm = GetSceneManager();
    EventAdd(&sm->OpenEvent, OnOpenScene, &g_Gameplay);
    EventAdd(&sm->BeforeDestroyEvent, OnBeforeDestroyScene, &g_Gameplay.character);
    
    g_Gameplay.boneTransfer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size  = sizeof(u32) * AnimMtxNumF32 * MAX_BONES
    });
}

void Gameplay_Update()
{
    UpdatePicking();
    Entity* character = RenderSet_GetEntity(&g_Gameplay.scene->skinnedSet, g_Gameplay.character.entity);
    if (character == NULL) return;
    HandleInputs(&g_Gameplay.character, &g_Gameplay);
    UpdateCharacter(&g_Gameplay, character);
}

void Gameplay_Destroy()
{
    if (g_Gameplay.boneTransfer) SDL_ReleaseGPUTransferBuffer(g_GPUDevice, g_Gameplay.boneTransfer);
    g_Gameplay.boneTransfer = NULL;
}
