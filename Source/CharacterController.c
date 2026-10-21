
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
    SceneBundle* characterBundle;
    SparseData*  characterSparse;

    b3Vec3 groundNormal;
    b3Vec3 position;

    f32  verticalSpeed;
    f32  characterSpeed;
    f32  jumpSpeed;
    f32  maxSpeed;
    f32  acceleration;
    f32  decceleration;
    f32  currentSpeed; // [0, 1]
    f32  trigerTime;
    f32  transitionTime;
    f32  transitionSpeed;
    f32  animSpeed;
    f32  anim0Start;
    f32  anim1Start;

    EntityID characterEntity;
    u32 numCharacterEntities;
    
    s16 atack;
    s16 combo;
    s16 idle2;
    s16 jump;
    s16 impact;
    s16 kick;
    s16 turnLeft90;
    s16 turnRight90;
    s16 turn180;
    u16 idle;
    u16 walk;
    u16 jogRun;
    
    u16 lastAnimation;
    u16 trigerAnim;

    bool grounded;
    bool characterActive;
    bool jumped;
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
    ch.position        = (b3Vec3){0.0f, 10.0f, 0.0f}; 
    ch.jumpSpeed       = 10.0f;
    ch.maxSpeed        = 10.0f;
    ch.acceleration    = 20.0f;
    ch.decceleration   = 10.0f;
    ch.characterSpeed  = 8.0f;
    ch.verticalSpeed   = 0.0f;
    ch.grounded = ch.characterActive = ch.jumped = false;

    // also these anims exist: "StrafeLeft", "StrafeRight"
    u32 animOffset = scene->bundleRefs[characterId].animAlloc.animOffset;
    ch.idle         = animOffset + FindAnimIndex(ch.characterBundle, "Idle");
    ch.walk         = animOffset + FindAnimIndex(ch.characterBundle, "Walk");
    ch.jogRun       = animOffset + FindAnimIndex(ch.characterBundle, "Run");
    ch.atack        = animOffset + FindAnimIndex(ch.characterBundle, "Slash2");
    ch.combo        = animOffset + FindAnimIndex(ch.characterBundle, "SwordCombo");
    ch.idle2        = animOffset + FindAnimIndex(ch.characterBundle, "Idle2");
    ch.jump         = animOffset + FindAnimIndex(ch.characterBundle, "Jump");
    ch.impact       = animOffset + FindAnimIndex(ch.characterBundle, "Impact");
    ch.kick         = animOffset + FindAnimIndex(ch.characterBundle, "Kick");
    ch.turnLeft90   = animOffset + FindAnimIndex(ch.characterBundle, "TurnLeft90");
    ch.turnRight90  = animOffset + FindAnimIndex(ch.characterBundle, "TurnRight90");
    ch.turn180      = animOffset + FindAnimIndex(ch.characterBundle, "Turn180");
    
    ch.trigerAnim     = ch.lastAnimation   = ch.idle;
    ch.transitionTime = ch.trigerTime      = 0.0f;
    ch.animSpeed      = ch.transitionSpeed = 1.0f;
    ch.anim0Start     = ch.anim1Start      = TimeSinceStartup();
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
        ch.position = (b3Vec3){ 0.0f, 10.0f, 0.0f };
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

    float speedTarget = Maxf32(Absf32(axis.y), Absf32(axis.x));
    if (GetKeyDown(SDLK_LSHIFT)) speedTarget *= 2.0f;
    ch.currentSpeed = Lerpf32(ch.currentSpeed, speedTarget, dt * 10.0f); // SmoothDamp(ch.currentSpeed, axis.y, &ch.smoothVel, smoothTime, 2.0f, dt);
    
    b3Vec3 velocity = b3MulSV(ch.currentSpeed * ch.characterSpeed, wish);
    velocity.y += ch.verticalSpeed;

    b3Vec3 target = b3Add(ch.position, b3MulSV(dt, velocity));
    b3Vec3 start = ch.position;

    for (int iteration = 0; iteration < 5; ++iteration)
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
    g_Camera.position = B3VecToFloat3(ch.position);
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

void TriggerAnim(u16 anim, f32 transitionTime)
{
    ch.trigerAnim = anim;
    ch.transitionTime = 1.0f;
    ch.transitionSpeed = 1.0f / transitionTime;
    ch.anim1Start = TimeSinceStartup();
}

static void Animate()
{
    Scene* scene = GetActiveScene();
    Entity* character = RenderSet_GetEntity(&scene->skinnedSet, ch.characterEntity);
    if (character == NULL) return;
    
    f32 timeSinceStartup = TimeSinceStartup();
    f32 dt = GetDeltaTime();
    
    u32 anim0 = ch.idle, anim1 = ch.walk;
    GPUAnimationInstance instance;
    instance.blend = FloatToHalf(Fractf32(ch.currentSpeed));
    if (ch.currentSpeed >= 1.0f) {
        anim0 = ch.walk;
        anim1 = ch.jogRun;
    }

    if (ch.trigerTime > 0.0f) {
        anim0 = ch.trigerAnim;
        instance.blend = ZeroFP16;
        ch.trigerTime -= dt;
    }

    if (ch.transitionTime > 0.0f) {
        anim0 = ch.lastAnimation;
        anim1 = ch.trigerAnim;
        instance.blend = FloatToHalf(ch.transitionTime);
        ch.transitionTime -= dt * ch.transitionSpeed;
        ch.anim1Start = timeSinceStartup;
        if (ch.transitionTime < 0.0f)
        {
            ch.anim0Start = timeSinceStartup;
            ch.trigerTime = scene->animSystem.animData[ch.trigerAnim].duration;
        }
    }

    const GPUAnimationData* anim0Data = &scene->animSystem.animData[anim0];
    const GPUAnimationData* anim1Data = &scene->animSystem.animData[anim1];
    f32 anim0t = Fractf32(((timeSinceStartup - ch.anim0Start) * ch.animSpeed) / anim0Data->duration);
    f32 anim1t = Fractf32(((timeSinceStartup - ch.anim1Start) * ch.animSpeed) / anim1Data->duration);
    instance.animRatio2  = (u32)PackUnorm16(anim0t) | ((u32)PackUnorm16(anim1t) << 16);
    instance.animIdx2    = anim0 | (anim1 << 16);

    float2 targetMovement = GetMovementAxis();
    f32 x = g_Camera.yaw * -MATH_DegToRad;
    f32 inputAngle = ATan2(targetMovement.y, -targetMovement.x);
    f32 movementValue = F2LenSq(targetMovement);
    if (movementValue < 0.001f) inputAngle = 0.0f;
    x += -inputAngle;

    Quaternion mRotation = EntityGetRotation(character);
    if (movementValue > 0.1f)
    {
        const float rotationSpeed = 12.0f;
        mRotation = QSlerp(mRotation, QFromYAngle(x + MATH_PI), GetDeltaTime() * rotationSpeed);
    }

    u32 charSparse = SparseIDFromSparseData(&scene->skinnedSet, ch.characterSparse);
    v128f position = VecAdd(Vec3Load(&ch.position.x), VecSetR(0.0f, -1.3f, 0.0f, 0.0f));
    for (s32 i = 0; i < ch.numCharacterEntities; i++)
    {
        Entity* mesh = &scene->skinnedSet.entities[ch.characterSparse[i].id];
        EntitySetPositionV(mesh, position);
        EntitySetRotation(mesh, mRotation);
        AnimationSystem_SetInstance(&scene->animSystem, charSparse + i, instance);
    }
}

void UpdateCharacter()
{
    if (GetKeyDown(SDLK_K))     TriggerAnim(ch.kick, 0.5f);
    if (GetKeyDown(SDLK_SPACE)) TriggerAnim(ch.jump, 0.25f);
    if (GetMouseDown(MouseButton_Left)) TriggerAnim(ch.atack, 0.5f);
    
    Animate();
    CharacterMovement();
    CharacterUI();
}