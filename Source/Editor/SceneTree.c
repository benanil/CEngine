#include "EditorInternal.h"
#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/Bitset.h"
#include "Include/UIWindow.h"
#include "Include/Algorithm.h"

// caller owned tree expansion state, node bits start closed,
// bundle flags are stored inverted so bundle rows start open
#define SCENE_TREE_MAX_NODES MAX_ENTITY
static u64 sceneNodeOpenBits[2][SCENE_TREE_MAX_NODES / 64];
static u8  sceneBundleClosed[2];

static u32  sceneTreeRowBudget;

static bool SceneNodeIsOpen(EntityID id)
{
    return BitsetGet(sceneNodeOpenBits[EntityIDIsSkinned(id)], EntityIDSparse(id));
}

static void SceneNodeFlipOpen(EntityID id)
{
    BitsetFlip(sceneNodeOpenBits[EntityIDIsSkinned(id)], EntityIDSparse(id));
}

static Clay_String SceneNodeLabel(const ANode* node, s32 nodeIdx)
{
    if (node->name && node->name[0]) return UIStr(node->name);

    char* text = UIFrameStringAlloc(20u);
    if (!text) return CLAY_STRING("Node");
    u32 len = 5u;
    MemCopy(text, "Node ", len);
    len += (u32)IntToString(text + len, (int64_t)nodeIdx, 0);
    text[len] = '\0';
    return (Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text };
}

// selecting a node in the tree selects its whole subtree in the viewport: the node and
// every descendant mesh node of the same spawned instance become gizmo targets
static void SceneSelectNodeInViewport(const Scene* scene, EntityID id)
{
    SceneSelectObject(id);
    EditorGizmoSetTarget(id);
}

// right click selects the hovered row so the context menu targets it, last frame's layout
bool SceneRowRightClicked(Clay_ElementId id)
{
    if (!GetMousePressed(MouseButton_Right)) return false;
    Clay_ElementData data = Clay_GetElementData(id);
    if (!data.found) return false;
    Clay_PointerData pointer = Clay_GetPointerState();
    return pointer.position.x >= data.boundingBox.x && pointer.position.x <= data.boundingBox.x + data.boundingBox.width &&
           pointer.position.y >= data.boundingBox.y && pointer.position.y <= data.boundingBox.y + data.boundingBox.height;
}

static const char* GetEntityMeshName(const RenderSet* set, EntityID id)
{
    const Scene* scene = set->hookScene;
    const Entity* entity = Scene_GetEntity(scene, id);
    u32 bundleId = EntityGetBundleId(set, entity);
    if (!PTR_VALID(set->bundles[bundleId])) return "Custom"; // runtime
    Range range = set->bundlePrimRange[bundleId];
    u32 localId = EntityGetPrimitiveID(entity) - range.start;

    for (s32 i = 0, j = 0; i < set->bundles[bundleId]->numMeshes; i++)
    {
        const AMesh* mesh = &set->bundles[bundleId]->meshes[i];
        for (s32 k = 0; k < mesh->numPrimitives; k++, j++)
            if (j == localId) 
                return mesh->name ? mesh->name : "--";
    }
    return "--";
}

static void SceneNodeTree(const RenderSet* set, SparseData* sparse, u32 depth)
{
    if (sceneTreeRowBudget == 0u) return;
    sceneTreeRowBudget--;

    EntityID entityId = EntityIDFromSparseData(set->hookScene, sparse);
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeNode"), (u32)(u64)sparse);

    u32 flags = 0u;
    if (sparse->subNodeCount <= 0) flags |= UITreeNodeFlags_Leaf;
    if (entityId == sceneSelectedEntity) flags |= UITreeNodeFlags_Selected;
    
    bool open = SceneNodeIsOpen(entityId);
    bool selected = entityId == sceneSelectedEntity;
    if (UITreeNode(id, UIStr(GetEntityMeshName(set, entityId)), depth, flags, open, &selected))
    {
        SceneNodeFlipOpen(entityId);
        open = !open;
    }
    if (selected || SceneRowRightClicked(id))
    {
        sceneSelectedEntity = entityId;
        if (selected) SceneSelectNodeInViewport(GetActiveScene(), entityId);
    }
    if (!open) return;

    u32 sparseId = EntityIDSparse(entityId);
    for (s32 c = 0; c < sparse->subNodeCount; c++)
    {
        s32 child = sparseId + set->subNodes[sparse->subNodeOffset + c];
        SceneNodeTree(set, set->sparseData + child, depth + 1u);
    }
}

static void SceneRenderSetTree(RenderSet* rs, bool skinned)
{
    Clay_ElementId id = Clay_GetElementIdWithIndex(CLAY_STRING("SceneTreeRoot"), skinned);

    u32 flags = 0u;
    if (rs->numEntities <= 0) flags |= UITreeNodeFlags_Leaf;
    // if (bundleIdx == sceneSelectedEntity) flags |= UITreeNodeFlags_Selected;

    bool open = !sceneBundleClosed[skinned];
    bool selected = false;

    if (UITreeNode(id, CLAY_STRING("set"), 0u, flags, open, &selected))
    {
        sceneBundleClosed[skinned] ^= 1u;
        open = !open;
    }

    if (!open) return;

    // normalized bundles have parent indices built, roots are the unparented nodes
    for (s32 i = 0; i < rs->numEntities; i++)
    {
        SparseData* sparse = rs->sparseData + EntityGetSparseID(&rs->entities[i]);
        if (sparse->parent <= 0)
            SceneNodeTree(rs, sparse, 1u);
    }
}

void SceneTreeRender(Scene* scene, Clay_ElementId windowID)
{
    sceneTreeRowBudget = SCENE_TREE_MAX_NODES;
    CLAY(CLAY_ID("SceneWindowTree"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("SceneWindowTree"), 0.0f), 2u)) 
    {
        if (scene->surfaceSet.numEntities > 0) SceneRenderSetTree(&scene->surfaceSet, false);
        // SceneRenderSetTree(&scene->skinnedSet, false);
        
        if (sceneTreeRowBudget == 0u)
        {
            CLAY_TEXT(CLAY_STRING("... node limit reached, collapse some rows"), CLAY_TEXT_CONFIG({
                .fontSize = 13,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
    }
}