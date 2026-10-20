//------------------------------------------------------------------------
// Scene tree context menu
#include "EditorInternal.h"
#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/UIWindow.h"
#include "Include/AssetManager.h"
#include "Include/Animation.h"

#include <box3d/box3d.h>

typedef struct SceneInspectorCache_
{
    bool valid;
    u32 skinned;
    u32 groupIdx;
    u32 entityIdx;
    v128f position;
    u64 rotation;
    u64 scalePacked;
    f32 positionUi[3];
    s32 rotationUi[3];
    f32 scaleUi[3];
} SceneInspectorCache;

static SceneInspectorCache sceneInspectorCache;

static bool sceneInspectorOpen    = true;
static bool scenePhysicsOpen      = true;
static bool scenePhysicsLocksOpen = false;

extern s32 sceneSelectedBundle;
extern u32 sceneSelectedEntity;
extern SceneObjectSelection sceneObjectSelection;

void SceneEventDuplicateBundle(void* unused)
{
    (void)unused;
    Scene* scene = GetActiveScene();
    if (!scene || sceneSelectedEntity >= scene->numBundles || !scene->bundleRefs[sceneSelectedEntity].bundle) return;

    Entity* entity = Scene_GetEntity(scene, sceneSelectedEntity);
    const SceneBundleRef* ref = &scene->bundleRefs[sceneSelectedEntity];
    const RenderSet* set = ref->skinned ? &scene->skinnedSet : &scene->surfaceSet;

    v128f position = VecZero();
    v128f rotation = QIdentity();
    v128f scale    = VecSet1(1.0f);
    if (ref->renderIdx < set->numBundles)
    {
        Range range = set->bundlePrimRange[ref->renderIdx];
        for (u32 g = range.start; g < range.start + range.count; g++)
        {
            const PrimitiveGroup* group = &set->primitiveGroups[g];
            if (group->numEntities == 0u) continue;
            const Entity* entity = &set->entities[group->entityOffset];
            position = entity->position;
            rotation = EntityGetRotation(entity);
            scale    = EntityGetScaleV(entity);
            break;
        }
    }
    position = VecAdd(position, VecSetR(1.0f, 0.0f, 0.0f, 0.0f));
    Scene_Spawn(scene, EntityGetBundleId(set, entity), position, rotation, scale, EntityFlags_ColliderEnabled);
}

void SceneEventDeleteBundle(void* unused)
{
    (void)unused;
    Scene* scene = GetActiveScene();
    Scene_RemoveBundle(scene, sceneSelectedEntity);
    sceneSelectedBundle = INVALID_BUNDLE;
    sceneObjectSelection.valid = false;
    EditorGizmoClear(); // group indices shifted
}

static bool SceneResolveSelectedObject(Scene* scene, RenderSet** outSet, PrimitiveGroup** outGroup, Entity** outEntity)
{
    if (!scene || !sceneObjectSelection.valid) return false;
    *outEntity = Scene_GetEntity(scene, sceneObjectSelection.entityId);
    if (*outEntity == NULL) return false;
    RenderSet* set = Scene_GetEntityRenderSet(scene, sceneObjectSelection.entityId);
    *outSet = set;
    *outGroup = set->primitiveGroups + sceneObjectSelection.groupIdx;
    return true;
}

void SceneDeleteSelectedObject(void)
{
    Scene* scene = GetActiveScene();
    RenderSet* set;
    PrimitiveGroup* group;
    Entity* entity;
    if (!SceneResolveSelectedObject(scene, &set, &group, &entity)) return;
    (void)group;
    (void)entity;

    RenderSet_RemoveEntity(set, sceneObjectSelection.groupIdx, sceneObjectSelection.entityId);
    scene->renderDataDirty = 1;
    sceneObjectSelection.valid = false;
    EditorGizmoClear();
}

static void SceneInspectorRefreshCache(const Entity* entity)
{
    sceneInspectorCache.valid       = true;
    sceneInspectorCache.skinned     = sceneObjectSelection.skinned;
    sceneInspectorCache.groupIdx    = sceneObjectSelection.groupIdx;
    sceneInspectorCache.entityIdx   = sceneObjectSelection.entityId;
    sceneInspectorCache.position    = entity->position;
    sceneInspectorCache.rotation    = entity->rotation;
    sceneInspectorCache.scalePacked = entity->scale;

    sceneInspectorCache.positionUi[0] = VecGetX(entity->position);
    sceneInspectorCache.positionUi[1] = VecGetY(entity->position);
    sceneInspectorCache.positionUi[2] = VecGetZ(entity->position);

    float3 euler = QToEulerAngles(EntityGetRotation(entity));
    sceneInspectorCache.rotationUi[0] = Clamps32((s32)(euler.x * MATH_RadToDeg + (euler.x >= 0.0f ? 0.5f : -0.5f)), -180, 180);
    sceneInspectorCache.rotationUi[1] = Clamps32((s32)(euler.y * MATH_RadToDeg + (euler.y >= 0.0f ? 0.5f : -0.5f)), -180, 180);
    sceneInspectorCache.rotationUi[2] = Clamps32((s32)(euler.z * MATH_RadToDeg + (euler.z >= 0.0f ? 0.5f : -0.5f)), -180, 180);

    v128f scaleV = EntityGetScaleV(entity);
    sceneInspectorCache.scaleUi[0] = VecGetX(scaleV);
    sceneInspectorCache.scaleUi[1] = VecGetY(scaleV);
    sceneInspectorCache.scaleUi[2] = VecGetZ(scaleV);
}

static void SceneInspectorValidateCache(const Entity* entity)
{
    if (!sceneInspectorCache.valid ||
        sceneInspectorCache.skinned != sceneObjectSelection.skinned ||
        sceneInspectorCache.groupIdx != sceneObjectSelection.groupIdx ||
        sceneInspectorCache.entityIdx != sceneObjectSelection.entityId)
    {
        SceneInspectorRefreshCache(entity);
        return;
    }

    if (VecGetX(sceneInspectorCache.position) != VecGetX(entity->position) ||
        VecGetY(sceneInspectorCache.position) != VecGetY(entity->position) ||
        VecGetZ(sceneInspectorCache.position) != VecGetZ(entity->position) ||
        sceneInspectorCache.rotation != entity->rotation ||
        sceneInspectorCache.scalePacked != entity->scale)
        SceneInspectorRefreshCache(entity);
}

// A dimmed "label  value" row for physics properties that are simulation output
// (mass, velocities, center of mass) and cannot be edited from the inspector.
static void UIPhysicsReadonlyRow(Clay_ElementId id, const char* label, const char* value)
{
    CLAY(id, {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(24.0f) },
            .childGap = 8,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY_TEXT(UIStr(label), CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = UIGetClayColor(UIColor_Text) }));
        // element spacer, never a whitespace-only CLAY_TEXT (that corrupts the text batch)
        CLAY(CLAY_ID_LOCAL("Spacer"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1.0f) } } }) {}
        CLAY_TEXT(UIStr(value), CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = UIGetClayColor(UIColor_SubText) }));
    }
}

// A per-axis lock row: a leading label followed by three checkboxes (X/Y/Z)
// laid out horizontally rather than stacked. Returns true when any axis toggled.
static bool UIPhysicsLockRow(Clay_ElementId rowId, Clay_String label,
                             bool* x, bool* y, bool* z)
{
    bool changed = false;
    CLAY(rowId, {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 6,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        CLAY(CLAY_ID_LOCAL("Label"), {
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(84.0f), CLAY_SIZING_GROW(0) },
                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
            }
        }) {
            CLAY_TEXT(label, CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = UIGetClayColor(UIColor_SubText) }));
        }
        changed |= UICheckboxID("X", rowId.id * 0 + 0, x);
        changed |= UICheckboxID("Y", rowId.id * 1 + 1, y);
        changed |= UICheckboxID("Z", rowId.id * 2 + 2, z);
    }
    return changed;
}

// formats into a per-frame string; safe to pass straight to UIPhysicsReadonlyRow
static const char* UIPhysicsFmtF(f32 v, int decimals)
{
    char* s = UIFrameStringAlloc(32u);
    if (!s) return "";
    SDL_snprintf(s, 32u, "%.*f", decimals, v);
    return s;
}

static const char* UIPhysicsFmtV3(f32 x, f32 y, f32 z, int decimals)
{
    char* s = UIFrameStringAlloc(96u);
    if (!s) return "";
    SDL_snprintf(s, 96u, "%.*f, %.*f, %.*f", decimals, x, decimals, y, decimals, z);
    return s;
}

// Visualizes and edits the box3d body attached to the selected entity.
// Simulation-output values (mass, velocities, center of mass) are read-only;
// body type / dynamics / material / lock widgets push their changes back through
// the box3d setters. Note the underlying collider is a static mesh, so a body
// type change is runtime-only and is reset to static on the next collider build.
static void SceneInspectorPhysicsUI(Scene* scene, Entity* entity)
{
    if (sceneObjectSelection.skinned || (EntityGetFlags(entity) & EntityFlags_NoMesh) != 0) return;
    u32 sparseId = EntityGetSparseID(entity);
    if (!scene->physicsBodies || sparseId >= scene->surfaceSet.maxEntities) return;

    b3BodyId body = Physics_GetPhysicsBody(scene, entity);
    UIDivider(CLAY_ID("InspectorPhysicsDivider"));
    scenePhysicsOpen ^= UICollapsingHeader(CLAY_ID("InspectorPhysicsHeader"), CLAY_STRING("Physics Body"), scenePhysicsOpen);
    if (!scenePhysicsOpen) return;
    
    bool isEnabled = Entity_IsPhysicsEnabled(scene, entity);
    if (UICheckbox("Enabled", &isEnabled))
    {
        Entity_TogglePhysics(scene, entity, isEnabled);
        body = scene->physicsBodies[sparseId];
    }

    if (B3_IS_NULL(body)) return;

    b3ShapeId shapeId;
    bool hasShape = b3Body_GetShapes(body, &shapeId, 1) > 0;

    // Body type is editable. Order matches b3BodyType (static, kinematic, dynamic).
    static const char* bodyTypes[] = { "Static", "Kinematic", "Dynamic" };
    u32 bodyType = (u32)b3Body_GetType(body);
    if (bodyType >= ARRAY_SIZE(bodyTypes)) bodyType = 0u;
    if (UIDropdown("Body Type", bodyTypes, ARRAY_SIZE(bodyTypes), &bodyType))
    {
        b3Body_SetType(body, (b3BodyType)bodyType);
        if (bodyType == (u32)b3_dynamicBody && hasShape && b3Body_GetMass(body) <= 0.0f)
            Physics_ApplyDefaultDynamicMass(body, shapeId);
    }

    // Shape type is editable. Order matches b3ShapeType so the index maps directly.
    // Mesh restores the original triangle collider; compound/height are no-ops.
    if (hasShape)
    {
        u32 shapeType = (u32)b3Shape_GetType(shapeId);
        if (shapeType >= b3_shapeTypeCount) shapeType = 0u;
        if (UIDropdown("Shape", Physics_ShapeNames(), b3_shapeTypeCount, &shapeType))
            Entity_SetPhysicsShapeAuto(scene, entity, (b3ShapeType)shapeType);
    }

    // Mass properties and velocities are simulation output -> read-only.
    UISectionHeader("State");
    UIPhysicsReadonlyRow(CLAY_ID("InspectorPhysicsMass"), "Mass", UIPhysicsFmtF(b3Body_GetMass(body), 3));

    b3Pos com = b3Body_GetMassData(body).center;
    UIPhysicsReadonlyRow(CLAY_ID("InspectorPhysicsCom"), "Center of Mass",
                         UIPhysicsFmtV3((f32)com.x, (f32)com.y, (f32)com.z, 3));
    
    b3Vec3 linVel = b3Body_GetLinearVelocity(body);
    UIPhysicsReadonlyRow(CLAY_ID("InspectorPhysicsLinVel"), "Linear Velocity",
                         UIPhysicsFmtV3(linVel.x, linVel.y, linVel.z, 3));

    b3Vec3 angVel = b3Body_GetAngularVelocity(body);
    UIPhysicsReadonlyRow(CLAY_ID("InspectorPhysicsAngVel"), "Angular Velocity",
                         UIPhysicsFmtV3(angVel.x, angVel.y, angVel.z, 3));

    // Integration scalars: each edit is pushed straight to the body.
    UISectionHeader("Dynamics");
    f32 linearDamping = b3Body_GetLinearDamping(body);
    if (UIEditFloat("Linear Damping", &linearDamping, 0.0f, 100.0f, 0.01f, 3))
        b3Body_SetLinearDamping(body, linearDamping);

    f32 angularDamping = b3Body_GetAngularDamping(body);
    if (UIEditFloat("Angular Damping", &angularDamping, 0.0f, 100.0f, 0.01f, 3))
        b3Body_SetAngularDamping(body, angularDamping);

    f32 gravityScale = b3Body_GetGravityScale(body);
    if (UIEditFloat("Gravity Scale", &gravityScale, -10.0f, 10.0f, 0.1f, 3))
        b3Body_SetGravityScale(body, gravityScale);

    f32 sleepThreshold = b3Body_GetSleepThreshold(body);
    if (UIEditFloat("Sleep Threshold", &sleepThreshold, 0.0f, 100.0f, 0.01f, 3))
        b3Body_SetSleepThreshold(body, sleepThreshold);

    // Surface material of the primary shape.
    if (hasShape)
    {
        UISectionHeader("Material");
        f32 friction = b3Shape_GetFriction(shapeId);
        if (UIEditFloat("Friction", &friction, 0.0f, 1.0f, 0.01f, 3))
            b3Shape_SetFriction(shapeId, friction);

        f32 restitution = b3Shape_GetRestitution(shapeId);
        if (UIEditFloat("Restitution", &restitution, 0.0f, 1.0f, 0.01f, 3))
            b3Shape_SetRestitution(shapeId, restitution);

        f32 density = b3Shape_GetDensity(shapeId);
        // updateBodyMass = true so mass properties recompute (matters once bodies are dynamic).
        if (UIEditFloat("Density", &density, 0.0f, 100000.0f, 0.1f, 3))
            b3Shape_SetDensity(shapeId, density, true);
    }

    // Motion locks, collapsible; each axis triple is laid out horizontally.
    scenePhysicsLocksOpen ^= UICollapsingHeader(CLAY_ID("InspectorPhysicsLocksHeader"), CLAY_STRING("Motion Locks"), scenePhysicsLocksOpen);
    if (scenePhysicsLocksOpen)
    {
        b3MotionLocks locks = b3Body_GetMotionLocks(body);
        bool locksChanged = UIPhysicsLockRow(CLAY_ID("InspectorPhysLockLinRow"), CLAY_STRING("Position"),
                                             &locks.linearX, &locks.linearY, &locks.linearZ);

        locksChanged |= UIPhysicsLockRow(CLAY_ID("InspectorPhysLockAngRow"), CLAY_STRING("Rotation"),
                                         &locks.angularX, &locks.angularY, &locks.angularZ);
        if (locksChanged)
            b3Body_SetMotionLocks(body, locks);
    }
}

void SceneInspectorUI(Scene* scene)
{
    sceneInspectorOpen ^= UICollapsingHeader(CLAY_ID("SceneInspectorHeader"), CLAY_STRING("Inspector"), sceneInspectorOpen);
    if (!sceneInspectorOpen) return;

    RenderSet* set;
    PrimitiveGroup* group;
    Entity* entity;
    if (!SceneResolveSelectedObject(scene, &set, &group, &entity))
    {
        sceneObjectSelection.valid = false;
        sceneInspectorCache.valid = false;
        CLAY_TEXT(CLAY_STRING("No object selected."), CLAY_TEXT_CONFIG( {
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
        return;
    }

    char* idText = UIFrameStringAlloc(96u);
    if (idText)
    {
        s32 len = SDL_snprintf(idText, 96u, "Bundle %u Group %u Entity %u", sceneObjectSelection.bundleIdx, sceneObjectSelection.groupIdx, sceneObjectSelection.entityId);
        if (len < 0) len = 0;
        CLAY_TEXT(((Clay_String) { .isStaticallyAllocated = false, .length = len, .chars = idText }), CLAY_TEXT_CONFIG( {
            .fontSize = 13,
            .textColor = UIGetClayColor(UIColor_SubText)
        }));
    }

    SceneInspectorValidateCache(entity);

    if (UIEditFloatN("Position", sceneInspectorCache.positionUi, 3u, -100000.0f, 100000.0f, 3))
    {
        v128f oldPos = EntityGetPosV(entity);
        v128f diff = VecSub(Vec3Load(sceneInspectorCache.positionUi), oldPos);
        Scene_AddPosRec(scene, entity, diff);

        sceneInspectorCache.position = entity->position;
        scene->renderDataDirty = 1;
    }

    if (UIEditIntN("Rotation", sceneInspectorCache.rotationUi, 3u, -180, 180))
    {
        v128f oldRot = EntityGetRotation(entity);
        v128f oldScale = EntityGetScaleV(entity);
        v128f center = RenderSet_EntityBoundsCenter(group, entity, oldRot, oldScale);
        v128f q = VecNorm(QFromEuler((f32)sceneInspectorCache.rotationUi[0] * MATH_DegToRad,
                                     (f32)sceneInspectorCache.rotationUi[1] * MATH_DegToRad,
                                     (f32)sceneInspectorCache.rotationUi[2] * MATH_DegToRad));
       
        Scene_RotateRec(scene, entity, QMul(QInverse(oldRot), q));
        
        EntitySetPositionV(entity, VecSub(center, QMulVec3V(VecMul(RenderSet_GroupLocalCenter(group), oldScale), q)));
        sceneInspectorCache.position = entity->position;
        sceneInspectorCache.positionUi[0] = VecGetX(entity->position);
        sceneInspectorCache.positionUi[1] = VecGetY(entity->position);
        sceneInspectorCache.positionUi[2] = VecGetZ(entity->position);
        sceneInspectorCache.rotation = entity->rotation;
        scene->renderDataDirty = 1;
    }

    if (UIEditFloatN("Scale", sceneInspectorCache.scaleUi, 3u, 0.001f, 10.0f, 3))
    {
        v128f oldScale = EntityGetScaleV(entity);
        v128f newScale = VecMax(Vec3Load(sceneInspectorCache.scaleUi), VecSet1(0.001f));
        EntitySetScaleV(entity, newScale);
        Scene_ScaleRec(scene, entity, VecDiv(newScale, oldScale), true);

        sceneInspectorCache.scalePacked = entity->scale;
        scene->renderDataDirty = 1;
    }

    SceneInspectorPhysicsUI(scene, entity);

    if (!sceneObjectSelection.skinned) return;
    if (sceneObjectSelection.bundleIdx >= scene->numBundles) return;
    const SceneBundleRef* ref = &scene->bundleRefs[sceneObjectSelection.bundleIdx];
    const SceneBundle* bundle = ref->bundle;
    if (!bundle || bundle->numAnimations <= 0) return;

    UIDivider(CLAY_ID("InspectorAnimDivider"));
    u32 animLocal = sceneObjectSelection.animIdx;
    u32 numOptions = Minu32((u32)bundle->numAnimations, 64u);
    const char* options[64];
    for (u32 i = 0u; i < numOptions; i++)
        options[i] = bundle->animations[i].name && bundle->animations[i].name[0] ? bundle->animations[i].name : "Animation";
    if (animLocal >= numOptions) animLocal = 0u;

    bool changed = UIDropdown("Animation", options, numOptions, &animLocal);
    changed |= UISliderFloatValue("Time offset", &sceneObjectSelection.animTime, 0.0f, 1.0f, 3);
    if (changed)
    {
        sceneObjectSelection.animIdx = animLocal;
        GPUAnimationInstance instance = {
            .animIdx2   = ref->animAlloc.animOffset + animLocal, 
            .animRatio2 = PackUnorm16(sceneObjectSelection.animTime), 
            .blend = ZeroFP16
        };
        instance.animIdx2   |= instance.animIdx2   << 16; 
        instance.animRatio2 |= instance.animRatio2 << 16;
        AnimationSystem_SetInstance(&scene->animSystem, EntityGetSparseID(entity), instance);
    }
}