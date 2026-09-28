

#include <box3d/box3d.h>
#include "Include/Scene.h"
#include "Include/Camera.h"
#include "Include/UIRenderer.h"
#include "Include/UIWindow.h"
#include "Include/BVH.h"
#include "Include/AssetManager.h"

typedef struct PlaneCapture
{
    b3CollisionPlane planes[16];
    b3ShapeId shape;
    int count;
} PlaneCapture;

float characterSpeed = 8.0f;
float jumpSpeed      = 10.0f;
float maxSpeed       = 10.0f;
float acceleration   = 20.0f;
float decceleration  = 10.0f;
float currentSpeed   = 0.0f; // [0, 1]

bool characterActive = false;
bool jumped = false;

static const float gravity = -25.0f;
static const float groundMinNormalY = 0.7f;
static const float snapDistance = 0.3f;
static const float moveTolerance = 0.001f;

static float verticalSpeed = 0.0f;
static bool grounded = false;
static b3Vec3 groundNormal = { 0.0f, 1.0f, 0.0f };

EntityID characterEntity = INVALID_ENTITY;
SceneBundle* capsuleBundle;

extern Camera g_Camera;

// # make "get movement axis" function for cross platform compatability
// # use get surface info function  instead of bottom two
// # IsJumping function; if jumped and not grounded else jumped == false
// # if IsJumping don't jump
// currentSpeed += hasAnyInput * acceleration * dt;
// currentSpeed -= hasAnyInput * decceleration * dt;
// multiply characterSpeed by currentSpeed
// if (currentSpeed < 0.9 && !hasAnyInput) LinearVelocity *= currentSpeed; // WARNING deltatime
// project movement direction with surface normal instead of Vec3Up

static void CharacterUI()
{
    if (GetKeyPressed(SDLK_C)) Clay_SetDebugModeEnabled(!Clay_IsDebugModeEnabled());

    static bool open = true;
    if (UIBeginWindowId(CLAY_ID("Character"), "Character",
                        (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 260.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIText("Character Settings");
        UIEditFloat("speed", &characterSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("jump force", &jumpSpeed, 0.0f, 100.0f, 0.2f, 2);
        UIEditFloat("maxSpeed", &maxSpeed, 0.0f, 100.0f, 0.1f, 2);
        UIEndWindow();
    }
}

void OpenSceneCallback(const char* path)
{
    Scene* scene = Scene_GetActive();
    if (!capsuleBundle) capsuleBundle  = GenerateCapsule(0.5f, 1.8f, 16u);
    u32 capsuleId  = Scene_AddBundle(scene, capsuleBundle, "Character");
    characterEntity = Scene_Spawn(scene, capsuleId, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecOne());

    Entity* character = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    Entity_TogglePhysics(scene, character, false);
}

void BeforeDestroySceneCallback(Scene* scene)
{
    characterEntity = INVALID_ENTITY;
}

static b3Capsule MakeMover(void)
{
    return (b3Capsule){
        .center1 = (b3Vec3){ 0.0f, -0.85f, 0.0f },
        .center2 = (b3Vec3){ 0.0f, 0.85f, 0.0f },
        .radius = 0.5f
    };
}

static b3QueryFilter MakeFilter(void)
{
    b3QueryFilter filter;
    filter.categoryBits = PHYS_CAT_SURFACE | PHYS_CAT_TERRAIN;
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

static void UpdateCharacter()
{
    Scene* scene = Scene_GetActive();
    Entity* character = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    if (character == NULL)
    {
        characterEntity = INVALID_ENTITY;
        return;
    }

    if (GetKeyPressed(SDLK_J) || GetKeyPressed(SDLK_K))
    {
        characterActive = !characterActive;
        character->position = VecZero();
        verticalSpeed = 0.0f;
        grounded = false;
        CameraMode wantedMode = GetKeyPressed(SDLK_K) ? CameraMode_FPS : CameraMode_TPS;
        wantedMode = characterActive ? wantedMode : CameraMode_Fly;
        CameraSwitchMode(&g_Camera, wantedMode);
        character->flags &= ~EntityFlags_NoMesh;
        // only fps mode entity is not visible
        character->flags |= (characterActive && wantedMode == CameraMode_FPS) * EntityFlags_NoMesh; // toggle visibility of capsule

        if (characterActive == false)
            g_Camera.position = F3Sub(g_Camera.position, F3MulF(g_Camera.front, 6.0f));
    }
    if (!characterActive) return;
    
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
    if (grounded && wishLength > 0.0f)
    {
        wish = b3MulSV(wishLength, b3Normalize(b3ProjectOnPlane(wish, groundNormal)));
    }

    if (grounded) verticalSpeed = 0.0f;
    else verticalSpeed += gravity * dt;

    if (grounded && GetKeyPressed(SDLK_SPACE))
    {
        verticalSpeed = jumpSpeed;
        grounded = false;
    }

    b3Vec3 velocity = b3MulSV(characterSpeed, wish);
    velocity.y += verticalSpeed;

    b3Vec3 target = b3Add(v128fToB3Vec3(character->position), b3MulSV(dt, velocity));
    b3Vec3 start = v128fToB3Vec3(character->position);

    for (int iteration = 0; iteration < 5; ++iteration)
    {
        b3Pos origin = v128fToB3Vec3(character->position);

        PlaneCapture capture = { 0 };
        b3World_CollideMover(worldId, origin, &mover, filter, CapturePlaneFcn, &capture);

        b3Vec3 remaining = b3Sub(target, v128fToB3Vec3(character->position));
        b3PlaneSolverResult result = b3SolvePlanes(remaining, capture.planes, capture.count);

        float fraction = b3World_CastMover(worldId, origin, &mover, result.delta, filter, NULL, NULL);
        b3Vec3 delta = b3MulSV(fraction, result.delta);
        character->position = VecAdd(character->position, B3VecTov128f(delta));

        if (b3LengthSquared(delta) < moveTolerance * moveTolerance) break;
    }

    if (verticalSpeed > 0.0f)
    {
        float intended = verticalSpeed * dt;
        float actual = v128fToB3Vec3(character->position).y - start.y;
        if (actual < intended * 0.5f) verticalSpeed = 0.0f;
    }

    bool wasGrounded = grounded;
    b3Vec3 normal = { 0.0f, 1.0f, 0.0f };
    grounded = verticalSpeed <= 0.0f && 
               FindGround(worldId, v128fToB3Vec3(character->position), &mover, filter, &normal);

    if (!grounded && wasGrounded && verticalSpeed <= 0.0f)
    {
        b3Pos origin = v128fToB3Vec3(character->position);
        b3Vec3 down = { 0.0f, -snapDistance, 0.0f };
        float fraction = b3World_CastMover(worldId, origin, &mover, down, filter, NULL, NULL);
        if (fraction < 1.0f)
        {
            character->position = VecAdd(character->position, B3VecTov128f(b3MulSV(fraction, down)));
            grounded = FindGround(worldId, v128fToB3Vec3(character->position), &mover, filter, &normal);
        }
    }

    groundNormal = grounded ? normal : (b3Vec3){ 0.0f, 1.0f, 0.0f };
    Vec3Store(&g_Camera.position.x, character->position);
    g_Camera.position.y += 1.6f;
    g_Camera.target = g_Camera.position;
}

void Gameplay_Update()
{
    UpdateCharacter();
    CharacterUI();
}
