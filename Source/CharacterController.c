
#include <box3d/box3d.h>
#include <SDL3/SDL_gpu.h>

#include "Math/Bitpack.h"
#include "Math/Half.h"
#include "Include/Scene.h"
#include "Include/UIWindow.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/UIRenderer.h"
#include "Include/Camera.h"
#include "Include/Rendering.h"

typedef struct PlaneCapture
{
    b3CollisionPlane planes[16];
    b3ShapeId shape;
    int count;
} PlaneCapture;

typedef struct Character_
{
    v128f center;
    SceneBundle* bundle;
    SparseData*  sparse;
    AnimationBundleAlloc* animAlloc;
    const mat4x4* invBind;

    b3Vec3 groundNormal;
    b3Vec3 position;
        
    f32 verticalSpeed;
    f32 characterSpeed;
    f32 jumpSpeed;
    f32 maxSpeed;
    f32 acceleration;
    f32 decceleration;
    f32 currentSpeed; // [0, 1]
    f32 trigerTime;
    f32 transitionTime;
    f32 transitionSpeed;
    f32 animSpeed;
    f32 anim0Start;
    f32 anim1Start;

    EntityID characterEntity;
    u32 numCharacterEntities;
    
    s16 atack, idle2, jump;
    u16 idle , walk, jogRun;
    
    u16 lastAnimation;
    u16 trigerAnim;
    u16 headJoint;
    u16 jointOffset;
    u16 rightHandJoint;

    u8  anim1BoneStart;
    u8  anim1BoneEnd;

    bool grounded;
    bool characterActive;
    bool jumped;
} Character;

static const float gravity = -25.0f;
static const float groundMinNormalY = 0.7f;
static const float snapDistance = 0.3f;
static const float moveTolerance = 0.001f;

static Character ch;
static float3 cameraOffset = { 0.0f, -1.3, 0.15f };
static float cameraXRotation = MATH_HalfPI;

static SDL_GPUTransferBuffer* boneTransfer = NULL;
static SDL_GPUFence* boneFence = NULL;
static mat4x4 boneMat;

extern Camera g_Camera;
extern SDL_GPUDevice* g_GPUDevice;

static EntityID sphereID = INVALID_ENTITY;
static const float characterScale = 1.0f;

static void SpawnCharacter()
{
    Scene* scene = GetActiveScene();
    u32 characterId = Scene_BundleIdx(scene, ch.bundle);
    u32 numEntity = scene->skinnedSet.numEntities;
    ch.sparse = Scene_Spawn(scene, characterId, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecSet1(characterScale), EntityFlags_None);
    ch.numCharacterEntities = scene->skinnedSet.numEntities - numEntity;
    ch.characterEntity = EntityIDFromSparseData(scene, ch.sparse);
    u32 bundleID = Scene_AddBundleCached(scene, GetUnitSphere(), "head bone");
    sphereID = EntityIDFromSparseData(scene,
                                      Scene_Spawn(scene, bundleID, VecZero(), QIdentity(), VecOne(), EntityFlags_None));

    EntityID entityID = EntityIDFromSparseData(scene, ch.sparse + 1);
    Entity* entity = Scene_GetEntity(scene, entityID);
    RenderSet* rs = Scene_GetEntityRenderSet(scene, entityID);
    ch.center = VecMulf(RenderSet_GroupLocalCenter(rs->primitiveGroups + EntityGetPrimitiveID(entity)), characterScale);
    VecSetY(ch.center, 0.0f);

    ch.invBind = (const mat4x4 *)scene->bundleRefs[EntityGetBundleId(&scene->skinnedSet, entity)].bundle->skins[0].inverseBindMatrices;
    ch.animAlloc = &scene->bundleRefs[characterId].animAlloc;
    ch.jointOffset = ch.animAlloc->skinSlot * MAX_BONES;

    SceneBundle* characterBundle = scene->bundleRefs[characterId].bundle;
    u16* nodeToJoint = characterBundle->skins[0].nodeToJoint;

    ch.headJoint      = ch.jointOffset + nodeToJoint[Maxs32(FindANodeFromName(characterBundle, "Head"), 0)];
    ch.rightHandJoint = ch.jointOffset + nodeToJoint[Maxs32(FindANodeFromName(characterBundle, "RightHand"), 0)];

    ch.groundNormal    = (b3Vec3){ 0.0f,  1.0f, 0.0f };
    ch.position        = (b3Vec3){ 0.0f, 10.0f, 0.0f }; 
    ch.jumpSpeed       = 10.0f;
    ch.maxSpeed        = 10.0f;
    ch.acceleration    = 20.0f;
    ch.decceleration   = 10.0f;
    ch.characterSpeed  = 8.0f;
    ch.verticalSpeed   = 0.0f;
    ch.grounded = ch.characterActive = ch.jumped = false;
    
    // also these anims exist: "StrafeLeft", "StrafeRight"
    u16 animOffset = ch.animAlloc->animOffset;
    ch.idle         = animOffset + FindAnimIndex(ch.bundle, "Idle1");
    ch.walk         = animOffset + FindAnimIndex(ch.bundle, "Walk");
    ch.jogRun       = animOffset + FindAnimIndex(ch.bundle, "Run");
    ch.atack        = animOffset + FindAnimIndex(ch.bundle, "Shash");
    ch.idle2        = animOffset + FindAnimIndex(ch.bundle, "Idle");
    ch.jump         = animOffset + FindAnimIndex(ch.bundle, "Jump");
    
    ch.trigerAnim      = ch.lastAnimation   = ch.idle;
    ch.transitionTime  = ch.trigerTime      = 0.0f;
    ch.transitionSpeed = 1.0f;
    ch.animSpeed       = 2.0f;
    ch.anim0Start      = ch.anim1Start      = TimeSinceStartup();
    ch.anim1BoneStart  = 0;
    ch.anim1BoneEnd    = ch.animAlloc->numNodes;

    boneTransfer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &(SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size  = sizeof(u32) * MatrixNumInt32
    });
}

static void CharacterLoaded(SceneAsyncRequest* request)
{
    Scene* scene = GetActiveScene();
    u32 bundleIdx = Scene_AddBundleFinalize(scene, request->sceneBundleStage);
    if (bundleIdx == INVALID_BUNDLE) {
        AX_ERROR("import to scene failed: %s", request->path);
        return;
    }
    ch.bundle = scene->bundleRefs[bundleIdx].bundle;
    SpawnCharacter();
}

void OpenSceneCallback(const char* path)
{
    SceneAsyncBegin(SceneAsyncOp_ImportMesh, "Assets/Meshes/Character/Character. gltf", "LoadCharacter", CharacterLoaded);
}

void BeforeDestroySceneCallback(Scene* scene)
{
    if (ch.characterEntity != INVALID_ENTITY) Scene_RemoveEntity(GetActiveScene(), ch.characterEntity);
    if (sphereID != INVALID_ENTITY) Scene_RemoveEntity(GetActiveScene(), sphereID);
    if (boneTransfer) SDL_ReleaseGPUTransferBuffer(g_GPUDevice, boneTransfer);
    ch.characterEntity = INVALID_ENTITY;
    boneTransfer = NULL;
}

static b3Capsule MakeMover(void)
{
    return (b3Capsule){
        .center1 = (b3Vec3){ 0.0f, -0.85f, 0.0f },
        .center2 = (b3Vec3){ 0.0f,  0.55f, 0.0f },
        .radius = 0.5f
    };
}

static b3QueryFilter MakeFilter(void)
{
    b3QueryFilter filter;
    filter.categoryBits = ~PhysBit_Player;
    filter.maskBits = ~0;
    filter.id = 0;
    filter.name = NULL;
    return filter;
}

static bool CapturePlaneFcn( b3ShapeId shapeId, const b3PlaneResult* planes, int planeCount, void* context )
{
    PlaneCapture* capture = context;
    capture->shape = shapeId;
    for ( int i = 0; i < planeCount && capture->count < 16; ++i )
    {
        capture->planes[capture->count++] = (b3CollisionPlane){
            .plane        = planes[i].plane,
            .pushLimit    = 0.1f,
            .push         = 0.0f,
            .clipVelocity = true
        };
    }
    return true;
}

static bool FindGround(b3WorldId worldId, b3Pos origin, b3Capsule* mover, b3QueryFilter filter, b3Vec3* normal)
{
    PlaneCapture capture = { 0 };
    b3World_CollideMover(worldId, origin, mover, filter, CapturePlaneFcn, &capture);

    bool found = false;
    float best = groundMinNormalY;
    for (int i = 0; i < capture.count; ++i)
    {
        b3Vec3 n = capture.planes[i].plane.normal;
        if (n.y >= best)
        {
            best = n.y;
            *normal = n;
            found = true;
        }
    }
    return found;
}

static void CharacterMovement(Scene* scene, Entity* character)
{
    if (!ch.characterActive) return;
    f32 dt = GetDeltaTime();
    b3WorldId worldId = Physics_GetWorld();
    b3Capsule mover = MakeMover();
    b3QueryFilter filter = MakeFilter();

    float2 axis = F2NormSafe(GetMovementAxis());
    b3Vec3 forward = Float3ToB3Vec3(g_Camera.front);
    forward.y = 0.0f;
    forward = b3Normalize(forward);
    b3Vec3 right = Float3ToB3Vec3(g_Camera.right);
    right.y = 0.0f;
    right = b3Normalize(right);

    b3Vec3 wish = b3Add(b3MulSV(axis.y, forward), b3MulSV(axis.x, right));
    f32 wishLength = b3Length(wish);
    if (wishLength > 1.0f)
    {
        wish = b3MulSV(1.0f / wishLength, wish);
        wishLength = 1.0f;
    }
    if (ch.grounded && wishLength > 0.0f)
    {
        wish = b3MulSV(wishLength, b3Normalize(b3ProjectOnPlane(wish, ch.groundNormal)));
    }

    if (ch.grounded) ch.verticalSpeed = 0.0f;
    else ch.verticalSpeed += gravity * dt;

    if (ch.grounded && GetKeyPressed(SDLK_SPACE))
    {
        ch.verticalSpeed = ch.jumpSpeed;
        ch.grounded = false;
    }
    f32 speedTarget = Absf32(F2Len(axis) - MATH_Epsilon);
    if (GetKeyDown(SDLK_LSHIFT)) speedTarget *= 2.0f;
    ch.currentSpeed = MoveTowards(ch.currentSpeed, speedTarget, dt * 5.0f); // SmoothDamp(ch.currentSpeed, axis.y, &ch.smoothVel, smoothTime, 2.0f, dt);
    
    b3Vec3 velocity = b3MulSV(ch.currentSpeed * ch.characterSpeed, wish);
    velocity.y += ch.verticalSpeed;

    b3Vec3 target = b3Add(ch.position, b3MulSV(dt, velocity));
    b3Vec3 start = ch.position;

    for (s32 iteration = 0; iteration < 5; ++iteration)
    {
        b3Pos origin = ch.position;

        PlaneCapture capture = { 0 };
        b3World_CollideMover(worldId, origin, &mover, filter, CapturePlaneFcn, &capture);

        b3Vec3 remaining = b3Sub(target, ch.position);
        b3PlaneSolverResult result = b3SolvePlanes(remaining, capture.planes, capture.count);

        float fraction = b3World_CastMover(worldId, origin, &mover, result.delta, filter, NULL, NULL);
        b3Vec3 delta = b3MulSV(fraction, result.delta);
        ch.position = b3Add(ch.position, delta);

        if (b3LengthSquared(delta) < moveTolerance * moveTolerance) break;
    }

    if (ch.verticalSpeed > 0.0f)
    {
        float intended = ch.verticalSpeed * dt;
        float actual = ch.position.y - start.y;
        if (actual < intended * 0.5f) ch.verticalSpeed = 0.0f;
    }

    bool wasGrounded = ch.grounded;
    b3Vec3 normal = { 0.0f, 1.0f, 0.0f };
    ch.grounded = ch.verticalSpeed <= 0.0f && FindGround(worldId, ch.position, &mover, filter, &normal);

    if (!ch.grounded && wasGrounded && ch.verticalSpeed <= 0.0f)
    {
        b3Pos origin = ch.position;
        b3Vec3 down = { 0.0f, -snapDistance, 0.0f };
        float fraction = b3World_CastMover(worldId, origin, &mover, down, filter, NULL, NULL);
        if (fraction < 1.0f)
        {
            b3Vec3 newPos = b3Add(ch.position, b3MulSV(fraction, down));
            ch.grounded = FindGround(worldId, ch.position, &mover, filter, &normal);
        }
    }
    
    ch.groundNormal = ch.grounded ? normal : (b3Vec3){ 0.0f, 1.0f, 0.0f };

    Quaternion mRotation;
    if (g_Camera.mode == CameraMode_TPS)
    {
        f32 x = g_Camera.yaw * -MATH_DegToRad;
        f32 inputAngle = ATan2(axis.y, -axis.x);
        f32 movementValue = F2LenSq(axis);
        if (movementValue < 0.001f) inputAngle = 0.0f;
        x += -inputAngle;
    
        mRotation = EntityGetRotation(character);
        if (movementValue > 0.1f)
        {
            const float rotationSpeed = 12.0f;
            mRotation = QSlerp(mRotation, QFromYAngle(x + MATH_PI), GetDeltaTime() * rotationSpeed);
        }
    }
    else
    {
        mRotation = QFromYAngle(g_Camera.yaw * -MATH_DegToRad + cameraXRotation);
    }

    v128f position = VecAdd(Vec3Load(&ch.position.x), VecSetR(0.0f, -1.3f, 0.0f, 0.0f));
    position = VecSub(position, QMulVec3V(ch.center, mRotation));

    for (s32 i = 0; i < ch.numCharacterEntities; i++)
    {
        Entity* mesh = &scene->skinnedSet.entities[ch.sparse[i].id];
        EntitySetPositionV(mesh, position);
        EntitySetRotation(mesh, mRotation);
    }

    g_Camera.position = F3Add(B3VecToFloat3(ch.position), QMulVec3(cameraOffset, mRotation));
    g_Camera.position = F3Add(M44ExtractPosition(boneMat), g_Camera.position);
    g_Camera.target = g_Camera.position;
}

static void CharacterUI()
{
    static bool open = true;
    if (UIBeginWindowId(CLAY_ID("Character"), "Character",
                        (float2) { 1400.0f, 40.0f }, (float2) { 500.0f, 260.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIText("Character Settings");
        UIEditFloat("speed", &ch.characterSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("jump force", &ch.jumpSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("maxSpeed", &ch.maxSpeed, 0.0f, 100.0f, 0.1f, 2);
        UIEditFloatN("cameraOffset", &cameraOffset.x, 3, -4.0f, 4.0f, 3);
        UIEditFloat("cameraXRot", &cameraXRotation, 0.0f, 100.0f, 0.1f, 3);
        
        UIEndWindow();
    }
}

void TriggerAnim(u16 anim, f32 transitionTime, u8 boneStart, u8 boneEnd)
{
    ch.trigerAnim      = anim;
    ch.transitionTime  = 1.0f;
    ch.transitionSpeed = 1.0f / transitionTime;
    ch.anim1Start      = TimeSinceStartup();
    ch.anim1BoneStart  = boneStart;
    ch.anim1BoneEnd    = (u8)Minu16((u16)boneEnd, ch.animAlloc->numNodes);
}

static void Animate(Scene* scene)
{
    f32 timeSinceStartup = TimeSinceStartup();
    f32 dt = GetDeltaTime();
    
    u32 anim0 = ch.idle, anim1 = ch.walk;
    GPUAnimationInstance instance;
    instance.blend = FloatToHalf(Fractf32(ch.currentSpeed));
    instance.anim1StartBone = ch.anim1BoneStart;
    instance.anim1EndBone   = ch.anim1BoneEnd;

    if (ch.currentSpeed > 1.01f) {
        anim0 = ch.walk;
        anim1 = ch.jogRun;
    }

    if (ch.trigerTime > 0.0f) {
        if (ch.anim1BoneStart == 0 && ch.anim1BoneEnd == ch.animAlloc->numNodes) {
            anim0 = ch.trigerAnim;
            instance.blend = ZeroFP16;
        }
        else
        {
            // upper lower body different animations
            u16 animations[3] = { ch.idle, ch.walk, ch.jogRun };
            ch.lastAnimation = animations[Minu32((u32)Roundf32(ch.currentSpeed), 2)];
            anim0 = ch.lastAnimation;
            anim1 = ch.trigerAnim;
            instance.blend = OneFP16;
        }

        ch.trigerTime -= dt * ch.animSpeed;
        if (ch.trigerTime < 0.0f)
        {
            ch.anim1BoneStart = 0;
            ch.anim1BoneEnd   = ch.animAlloc->numNodes;
        }
    }

    if (ch.transitionTime > 0.0f) {
        anim0 = ch.lastAnimation;
        anim1 = ch.trigerAnim;
        instance.blend = FloatToHalf((1.0f - ch.transitionTime));
        ch.transitionTime -= dt * ch.transitionSpeed;
        ch.anim1Start = timeSinceStartup;
        if (ch.transitionTime < 0.0f)
        {
            ch.anim0Start = timeSinceStartup;
            ch.trigerTime = scene->animSystem.animData[ch.trigerAnim].duration;
        }
    }

    instance.neckAngle  = 0; // FloatToHalf(Clampf32((g_Camera.yaw * MATH_DegToRad) / MATH_PI, -1.0f, 1.0f));
    instance.spineAngle = 0;
    const GPUAnimationData* anim0Data = &scene->animSystem.animData[anim0];
    const GPUAnimationData* anim1Data = &scene->animSystem.animData[anim1];
    f32 anim0t = Fractf32(((timeSinceStartup - ch.anim0Start) * ch.animSpeed) / anim0Data->duration);
    f32 anim1t = Fractf32(((timeSinceStartup - ch.anim1Start) * ch.animSpeed) / anim1Data->duration);
    instance.animRatio2  = (u32)PackUnorm16(anim0t) | ((u32)PackUnorm16(anim1t) << 16);
    instance.animIdx2    = anim0 | (anim1 << 16);

    u32 charSparse = SparseIDFromSparseData(&scene->skinnedSet, ch.sparse);
    for (s32 i = 0; i < ch.numCharacterEntities; i++)
    {
        AnimationSystem_SetInstance(&scene->animSystem, charSparse + i, instance);
    }
}

static void HandleInputs()
{
    u16 numNodes = ch.animAlloc->numNodes;
    // if (GetKeyDown(SDLK_L))     TriggerAnim(ch.kick, 0.05f, 0, numNodes); 
    if (GetKeyDown(SDLK_SPACE)) TriggerAnim(ch.jump, 0.05f, 0, numNodes);
    if (GetMouseDown(MouseButton_Left)) TriggerAnim(ch.atack, 0.05f, ch.animAlloc->spineJoint, ch.animAlloc->neckJoint);

    if (GetKeyPressed(SDLK_J) || GetKeyPressed(SDLK_K))
    {
        ch.characterActive = !ch.characterActive;
        ch.position = (b3Vec3){ 0.0f, 10.0f, 0.0f };
        ch.verticalSpeed = 0.0f;
        ch.grounded = false;
        CameraMode wantedMode = GetKeyPressed(SDLK_K) ? CameraMode_FPS : CameraMode_TPS;
        wantedMode = ch.characterActive ? wantedMode : CameraMode_Fly;
        CameraSwitchMode(&g_Camera, wantedMode);

        if (ch.characterActive == false)
            g_Camera.position = F3Sub(g_Camera.position, F3MulF(g_Camera.front, 6.0f));
    }

    float shift = GetKeyDown(SDLK_LSHIFT) ? -1.0f : 1.0f;
    const float delta = 0.01f;
    if (GetKeyDown(SDLK_KP_1)) cameraOffset.x  += delta * shift;
    if (GetKeyDown(SDLK_KP_2)) cameraOffset.y  += delta * shift;
    if (GetKeyDown(SDLK_KP_3)) cameraOffset.z  += delta * shift;
    if (GetKeyDown(SDLK_KP_4)) cameraXRotation += delta * shift * MATH_PI;
}

static void GetHeadBone(Scene* scene, Entity* character)
{
    u32 headNode = ch.headJoint;

    if (boneTransfer && boneFence && SDL_QueryGPUFence(g_GPUDevice, boneFence))
    {
        void* mapped = SDL_MapGPUTransferBuffer(g_GPUDevice, boneTransfer, false);
        if (mapped)
        {
            ALIGNSIMD u32 buffer[8];
            MemCopy(buffer, mapped, sizeof(buffer));
            boneMat = UnpackGPUBone(buffer);
            mat4x4 bindMatrix = M44Inverse(ch.invBind[ch.headJoint - ch.jointOffset]);
            boneMat = M44Multiply(bindMatrix, boneMat);
            boneMat = M44Multiply(boneMat, M44PositionRotationScaleVec(VecZero(), EntityGetRotation(character), VecSet1(characterScale)));
            SDL_UnmapGPUTransferBuffer(g_GPUDevice, boneTransfer);
        }
        SDL_ReleaseGPUFence(g_GPUDevice, boneFence);
    }

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    u32 dense = ch.sparse->id;
    u32 base = sizeof(u32) * dense * MAX_BONES * MatrixNumInt32;
    SDL_DownloadFromGPUBuffer(copyPass,
                              &(SDL_GPUBufferRegion){ .buffer = scene->animSystem.boneBuffer, 
                              .offset = base + (sizeof(u32) * MatrixNumInt32 * headNode), 
                                                      .size   = sizeof(u32) * MatrixNumInt32 },
                              &(SDL_GPUTransferBufferLocation){ .transfer_buffer = boneTransfer, .offset = 0 });
    SDL_EndGPUCopyPass(copyPass);
    boneFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
}

void UpdateCharacter()
{
    Scene* scene = GetActiveScene();
    Entity* character = RenderSet_GetEntity(&scene->skinnedSet, ch.characterEntity);
    if (character == NULL) return;
    
    GetHeadBone(scene, character);
    HandleInputs();
    CharacterMovement(scene, character);
    Animate(scene);
    CharacterUI();

    Entity* sphereEntity = Scene_GetEntity(scene, sphereID);
    if (sphereEntity)
    {
        v128f neckPos = VecAdd(EntityGetPosV(character), M44ExtractPositionV(boneMat));
        EntitySetPositionV(sphereEntity, neckPos);
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