
float characterForce = 25.0f;
float jumpForce      = 800.0f;
float maxSpeed       = 10.0f;
// todo(anil)
float acceleration   = 10.0f;
float currentSpeed   = 0.0f;

static EntityID characterEntity = INVALID_ENTITY;
SceneBundle* sphereBundle;

static void CharacterUI()
{
    if (GetKeyPressed(SDLK_C)) Clay_SetDebugModeEnabled(!Clay_IsDebugModeEnabled());

    static bool open = true;
    if (UIBeginWindowId(CLAY_ID("Character"), "Character",
                        (float2) { 18.0f, 18.0f }, (float2) { 500.0f, 760.0f }, 
                        &open, UIWindowFlags_NoClose))
    {
        UIText("Character Settings");
        UIEditFloat(CLAY_ID("CharForce"), CLAY_STRING("speed"), &characterForce, 0.0f, 1000.0f, 5.0f, 2);
        UIEditFloat(CLAY_ID("JumpForce"), CLAY_STRING("jump force"), &jumpForce, 0.0f, 10000.0f, 10.0f, 2);
        UIEditFloat(CLAY_ID("maxSpeed") , CLAY_STRING("maxSpeed"), &maxSpeed, 0.0f, 100.0f, 0.1f, 2);
        UIEndWindow();
    }
}

void OpenSceneCallback(const char* path)
{
    Scene* scene = Scene_GetActive();
    if (!sphereBundle) sphereBundle  = GenerateSphere(1.0f, 16u, 16u);
    u32 capsule  = Scene_AddBundle(scene, sphereBundle, "Character");
    characterEntity = Scene_Spawn(scene, capsule, VecSetR(0.0f, 0.0f, 0.0f, 0.f), QIdentity(), VecOne());

    Entity* character = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    b3BodyId body = Entity_GetPhysicsBody(scene, character);

    b3ShapeId shapeId;
    b3MotionLocks locks = {};
    locks.angularX = locks.angularY = locks.angularZ = true;
    b3Body_SetMotionLocks(body, locks);
    b3Body_GetShapes(body, &shapeId, 1);
    b3Shape_SetDensity(shapeId, 20.0f, true);
    b3Body_SetGravityScale(body, 2.0f);

    if(b3Body_IsValid(body))
        b3Body_SetBullet(body, true);
}

void BeforeDestroySceneCallback(Scene* scene)
{
    characterEntity = INVALID_ENTITY;
}

static void UpdateCharacter()
{
    Scene* scene = Scene_GetActive();
    static bool ballActive = false;
    Entity* ball = RenderSet_GetEntity(&scene->surfaceSet, characterEntity);
    if (ball == NULL) {
        characterEntity = INVALID_ENTITY; // deleted somewhere we lost its reference
        return;
    }

    b3BodyId body = Entity_GetPhysicsBody(scene, ball);
    if (GetKeyPressed(SDLK_J))
    {
        ballActive = !ballActive;
        ball->position = VecZero();
        Entity_SetPhysicsBodyType(scene, ball, ballActive ? b3_dynamicBody : b3_staticBody);
    }
    
    if (GetKeyPressed(SDLK_SPACE))
    {
        b3Body_ApplyLinearImpulseToCenter(body, (b3Vec3) { 0.0f, jumpForce, 0.0f }, false);
    }

    if (!ballActive)
        return;
    
    float fwdButton = GetKeyDown(SDLK_W) ? 1.0f : GetKeyDown(SDLK_S) ? -1.0f : 0.0f;
    float rgtButton = GetKeyDown(SDLK_D) ? 1.0f : GetKeyDown(SDLK_A) ? -1.0f : 0.0f;
    // if no input slowly stop
    if (Absf32(fwdButton) + Absf32(rgtButton) < MATH_Epsilon)
    {
        b3Vec3 vel = b3Body_GetLinearVelocity(body);
        float slowDown = 1.0f - (float)GetDeltaTime();
        b3Body_SetLinearVelocity(body, b3Mul(vel, (b3Vec3){slowDown, slowDown, slowDown}));
    }
    else
    {
        b3Vec3 forward = Float3ToB3Vec3(F3Proj(F3MulF(g_Camera.Front, fwdButton), F3Up()));
        b3Vec3 right   = Float3ToB3Vec3(F3Proj(F3MulF(g_Camera.Right, rgtButton), F3Up()));
        b3Vec3 direction = b3Normalize(b3Add(forward, right));
        b3Body_ApplyLinearImpulseToCenter(body, b3Mul(direction, (b3Vec3){characterForce, characterForce, characterForce}), true);
    }
    b3Vec3 velocity = b3Body_GetLinearVelocity(body);
    float velocityMagnitude = b3Length(velocity);
    if (velocityMagnitude > maxSpeed)
    {
        b3Vec3 newVelocity = b3MulSV(velocityMagnitude, b3Normalize(velocity));
        b3Body_SetLinearVelocity(body, newVelocity);        
    }
    // Entity_SyncPhysicsBody(scene, ball);
}

void Gameplay_Update()
{
    UpdateCharacter();
    CharacterUI();
}