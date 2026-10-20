
#include <box3d/box3d.h>
#include "Math/Bitpack.h"
#include "Math/Half.h"
#include "Include/Scene.h"
#include "Include/UIWindow.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/UIRenderer.h"
#include "Include/Camera.h"

typedef struct PlaneCapture
{
    b3CollisionPlane planes[16];
    b3ShapeId shape;
    int count;
} PlaneCapture;

typedef struct Character_
{
    float  verticalSpeed;
    bool   grounded;
    b3Vec3 groundNormal;

    f32  characterSpeed;
    f32  jumpSpeed;
    f32  maxSpeed;
    f32  acceleration;
    f32  decceleration;
    f32  currentSpeed; // [0, 1]
    f32  speedSmoothVelocity;

    s32 mAtackIndex;
    s32 mComboIndex;
    s32 mIdle2Index;
    s32 mJumpIndex;
    s32 mImpactIndex;
    s32 mKickIndex;
    s32 mTurnLeft90Index;
    s32 mTurnRight90Index;
    s32 mTurn180Index;
    bool characterActive;
    bool jumped;
    
    EntityID     characterEntity;
    SceneBundle* characterBundle;
    SparseData*  characterSparse;
    u32          numCharacterEntities;
    AnimatedCharacter animated;
} Character;

static const float gravity = -25.0f;
static const float groundMinNormalY = 0.7f;
static const float snapDistance = 0.3f;
static const float moveTolerance = 0.001f;

static Character ch;
extern Camera g_Camera;

static void SpawnCharacter()
{
    Scene* scene = GetActiveScene();
    
    u32 characterId  = Scene_BundleIdx(scene, ch.characterBundle);
    u32 numEntity = scene->skinnedSet.numEntities;
    ch.characterSparse = Scene_Spawn(scene, characterId, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecSet1(0.1f), EntityFlags_None);
    ch.numCharacterEntities = scene->skinnedSet.numEntities - numEntity;
    ch.characterEntity = EntityIDFromSparseData(scene, ch.characterSparse);
    
    ch.groundNormal    = (b3Vec3){ 0.0f, 1.0f, 0.0f };
    ch.jumpSpeed       = 10.0f;
    ch.maxSpeed        = 10.0f;
    ch.acceleration    = 20.0f;
    ch.decceleration   = 10.0f;
    ch.characterSpeed  = 8.0f;
    ch.verticalSpeed   = 0.0f;
    ch.speedSmoothVelocity = 0.0f;
    ch.grounded        = false;
    ch.characterActive = false;
    ch.jumped          = false;

    u32 animOffset = scene->bundleRefs[characterId].animAlloc.animOffset;
    s32 a_idle         = animOffset + FindAnimIndex(ch.characterBundle, "Idle");
    s32 a_walk         = animOffset + FindAnimIndex(ch.characterBundle, "Walk");
    s32 a_jog_forward  = animOffset + FindAnimIndex(ch.characterBundle, "Run");
    s32 a_jog_backward = animOffset + a_jog_forward; // FindAnimIndex(_character, "Jog_Backward");
    s32 a_strafe_left  = animOffset + FindAnimIndex(ch.characterBundle, "StrafeLeft");
    s32 a_strafe_right = animOffset + FindAnimIndex(ch.characterBundle, "StrafeRight");

    ch.mAtackIndex       = animOffset + FindAnimIndex(ch.characterBundle, "Slash2");
    ch.mComboIndex       = animOffset + FindAnimIndex(ch.characterBundle, "SwordCombo");
    ch.mIdle2Index       = animOffset + FindAnimIndex(ch.characterBundle, "Idle2");
    ch.mJumpIndex        = animOffset + FindAnimIndex(ch.characterBundle, "Jump");
    ch.mImpactIndex      = animOffset + FindAnimIndex(ch.characterBundle, "Impact");
    ch.mKickIndex        = animOffset + FindAnimIndex(ch.characterBundle, "Kick");
    ch.mTurnLeft90Index  = animOffset + FindAnimIndex(ch.characterBundle, "TurnLeft90");
    ch.mTurnRight90Index = animOffset + FindAnimIndex(ch.characterBundle, "TurnRight90");
    ch.mTurn180Index     = animOffset + FindAnimIndex(ch.characterBundle, "Turn180");

    // idle, left&right strafe
    AnimatedSetAnim(&ch.animated, aLeft  , 0, a_strafe_left);
    AnimatedSetAnim(&ch.animated, aMiddle, 0, a_idle);
    AnimatedSetAnim(&ch.animated, aRight , 0, a_strafe_right);
    // walk, jog, run
    AnimatedSetAnim(&ch.animated, aMiddle, 1, a_walk);
    AnimatedSetAnim(&ch.animated, aMiddle, 2, a_jog_forward);

    // set second row to idle 
    AnimatedSetAnim(&ch.animated, aLeft  , 1, a_strafe_left);  // a_diagonal_left);
    AnimatedSetAnim(&ch.animated, aRight , 1, a_strafe_right); // a_diagonal_right);
                                    
    // set first row to idle        
    AnimatedSetAnim(&ch.animated, aLeft  , 0, a_jog_forward); // a_diagonal_left);
    AnimatedSetAnim(&ch.animated, aRight , 0, a_jog_forward); // a_diagonal_right);
 
    // copy first row to inverse first row
    SmallMemCpy(ch.animated.mLocomotionIndicesInv[0],
                ch.animated.mLocomotionIndices[0], sizeof(s32) * 3);

    AnimatedSetAnim(&ch.animated, aLeft  , -1, a_jog_forward); // a_diagonal_left);
    AnimatedSetAnim(&ch.animated, aMiddle, -1, a_jog_backward);
    AnimatedSetAnim(&ch.animated, aRight , -1, a_jog_forward); // a_diagonal_right);
}

static void CharacterLoaded(SceneAsyncRequest* request)
{
    Scene* scene = GetActiveScene();
    u32 bundleIdx = Scene_AddBundleFinalize(scene, request->sceneBundleStage);
    if (bundleIdx == INVALID_BUNDLE) {
        AX_ERROR("import to scene failed: %s", request->path);
        return;
    }
    ch.characterBundle = scene->bundleRefs[bundleIdx].bundle;
    SpawnCharacter();
}

void OpenSceneCallback(const char* path)
{
    SceneAsyncBegin(SceneAsyncOp_ImportMesh, "Assets/Meshes/Paladin/Paladin.gltf", "LoadPaladin", CharacterLoaded);
}

void BeforeDestroySceneCallback(Scene* scene)
{
    if (ch.characterEntity != INVALID_ENTITY) Scene_RemoveEntity(GetActiveScene(), ch.characterEntity);
    ch.characterEntity = INVALID_ENTITY;
}

static b3Capsule MakeMover(void)
{
    return (b3Capsule){
        .center1 = (b3Vec3){ 0.0f, -0.85f, 0.0f },
        .center2 = (b3Vec3){ 0.0f,  0.85f, 0.0f },
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

static void CharacterMovement()
{
    Scene* scene = GetActiveScene();
    Entity* character = RenderSet_GetEntity(&scene->skinnedSet, ch.characterEntity);
    if (character == NULL) 
        return;

    if (GetKeyPressed(SDLK_J) || GetKeyPressed(SDLK_K))
    {
        ch.characterActive = !ch.characterActive;
        EntitySetPositionV(character, VecSetR(0.0f, 10.0f, 0.0f, 0.0f));
        ch.verticalSpeed = 0.0f;
        ch.grounded = false;
        CameraMode wantedMode = GetKeyPressed(SDLK_K) ? CameraMode_FPS : CameraMode_TPS;
        wantedMode = ch.characterActive ? wantedMode : CameraMode_Fly;
        CameraSwitchMode(&g_Camera, wantedMode);
        EntityFlags entityFlags = EntityGetFlags(character);
        entityFlags &= ~EntityFlags_NoMesh;
        // only fps mode entity is not visible
        entityFlags |= (ch.characterActive && wantedMode == CameraMode_FPS) * EntityFlags_NoMesh; // toggle visibility of capsule
        EntitySetFlags(character, entityFlags);

        if (ch.characterActive == false)
            g_Camera.position = F3Sub(g_Camera.position, F3MulF(g_Camera.front, 6.0f));
    }
    if (!ch.characterActive) return;

    float dt = GetDeltaTime();
    b3WorldId worldId = Physics_GetWorld();
    b3Capsule mover = MakeMover();
    b3QueryFilter filter = MakeFilter();

    float2 axis = GetMovementAxis();
    b3Vec3 forward = Float3ToB3Vec3(g_Camera.front);
    forward.y = 0.0f;
    forward = b3Normalize(forward);
    b3Vec3 right = Float3ToB3Vec3(g_Camera.right);
    right.y = 0.0f;
    right = b3Normalize(right);

    const float smoothTime = 0.25f;
    ch.currentSpeed = Lerpf32(ch.currentSpeed, Maxf32(Absf32(axis.y), Absf32(axis.x)), dt * 10.0f); // SmoothDamp(ch.currentSpeed, axis.y, &ch.speedSmoothVelocity, smoothTime, 2.0f, dt);

    b3Vec3 wish = b3Add(b3MulSV(axis.y, forward), b3MulSV(axis.x, right));
    float wishLength = b3Length(wish);
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

    b3Vec3 velocity = b3MulSV(ch.currentSpeed * ch.characterSpeed, wish);
    velocity.y += ch.verticalSpeed;

    b3Vec3 target = b3Add(EntityGetB3Pos(character), b3MulSV(dt, velocity));
    b3Vec3 start = EntityGetB3Pos(character);

    for (int iteration = 0; iteration < 5; ++iteration)
    {
        b3Pos origin = EntityGetB3Pos(character);

        PlaneCapture capture = { 0 };
        b3World_CollideMover(worldId, origin, &mover, filter, CapturePlaneFcn, &capture);

        b3Vec3 remaining = b3Sub(target, EntityGetB3Pos(character));
        b3PlaneSolverResult result = b3SolvePlanes(remaining, capture.planes, capture.count);

        float fraction = b3World_CastMover(worldId, origin, &mover, result.delta, filter, NULL, NULL);
        b3Vec3 delta = b3MulSV(fraction, result.delta);
        EntitySetPositionV(character, VecAdd(character->position, B3VecTov128f(delta)));

        if (b3LengthSquared(delta) < moveTolerance * moveTolerance) break;
    }

    if (ch.verticalSpeed > 0.0f)
    {
        float intended = ch.verticalSpeed * dt;
        float actual = EntityGetB3Pos(character).y - start.y;
        if (actual < intended * 0.5f) ch.verticalSpeed = 0.0f;
    }

    bool wasGrounded = ch.grounded;
    b3Vec3 normal = { 0.0f, 1.0f, 0.0f };
    ch.grounded = ch.verticalSpeed <= 0.0f && 
        FindGround(worldId, EntityGetB3Pos(character), &mover, filter, &normal);

    if (!ch.grounded && wasGrounded && ch.verticalSpeed <= 0.0f)
    {
        b3Pos origin = EntityGetB3Pos(character);
        b3Vec3 down = { 0.0f, -snapDistance, 0.0f };
        float fraction = b3World_CastMover(worldId, origin, &mover, down, filter, NULL, NULL);
        if (fraction < 1.0f)
        {
            v128f newPos = VecAdd(character->position, B3VecTov128f(b3MulSV(fraction, down)));
            ch.grounded = FindGround(worldId, EntityGetB3Pos(character), &mover, filter, &normal);
        }
    }
    
    ch.groundNormal = ch.grounded ? normal : (b3Vec3){ 0.0f, 1.0f, 0.0f };
    Vec3Store(&g_Camera.position.x, character->position);
    g_Camera.position.y += 1.6f;
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
        UIEndWindow();
    }
}

static void Animate()
{
    Scene* scene = GetActiveScene();
    Entity* character = RenderSet_GetEntity(&scene->skinnedSet, ch.characterEntity);
    if (character == NULL) return;

    float timeSinceStartup = TimeSinceStartup();
    const GPUAnimationData* idleData = &scene->animSystem.animData[AnimatedGetAnim(&ch.animated, aMiddle, 0)];
    const GPUAnimationData* walkData = &scene->animSystem.animData[AnimatedGetAnim(&ch.animated, aMiddle, 1)];

    GPUAnimationInstance instance;
    instance.animIdx2    = (u32)AnimatedGetAnim(&ch.animated, aMiddle, 0); // idle
    instance.animIdx2   |= (u32)AnimatedGetAnim(&ch.animated, aMiddle, 1) << 16; // walk
    instance.animRatio2  = (u32)PackUnorm16(Fractf32(timeSinceStartup / idleData->duration));
    instance.animRatio2 |= (u32)PackUnorm16(Fractf32(timeSinceStartup / walkData->duration)) << 16;
    
    instance.blend = FloatToHalf(ch.currentSpeed);
    u32 charSparse = SparseIDFromSparseData(&scene->skinnedSet, ch.characterSparse);

    float2 targetMovement = GetMovementAxis();
    float x = g_Camera.yaw * -MATH_DegToRad;
    float inputAngle = ATan2(targetMovement.y, -targetMovement.x);
    float movementValue = F2LenSq(targetMovement);
    if (movementValue < 0.001f) inputAngle = 0.0f;
    x += -inputAngle;

    Quaternion mRotation = EntityGetRotation(character);
    if (movementValue > 0.1f)
    {
        const float rotationSpeed = 12.0f;
        mRotation = QSlerp(mRotation, QFromYAngle(x + MATH_PI), GetDeltaTime() * rotationSpeed);
    }

    for (s32 i = 0; i < ch.numCharacterEntities; i++)
    {
        Entity* mesh = &scene->skinnedSet.entities[ch.characterSparse[i].id];
        EntitySetPositionV(mesh, character->position);
        EntitySetRotation(mesh, mRotation);
        AnimationSystem_SetInstance(&scene->animSystem, charSparse + i, instance);
    }
}

void UpdateCharacter()
{
    Animate();
    CharacterMovement();
    CharacterUI();
}