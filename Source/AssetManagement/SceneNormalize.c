#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/Memory.h"
#include "Include/Platform.h"

#define CHECK_RETURN(cond, message)   if (cond) { message; return; }
#define CHECK_CONTINUE(cond, message) if (cond) { message; continue; }

static void EmitRemappedNode(const SceneBundle* scene, s32 oldNode, s32* nodeRemap, s32* oldFromNew, s32* numRemapped, s32* stack)
{
    const s32 numNodes = scene->numNodes;
    s32 stackIndex = 0;

    CHECK_RETURN(oldNode < 0 || oldNode >= numNodes, AX_WARN("EmitRemappedNode index out of bounds %d", oldNode));

    stack[stackIndex++] = oldNode;
    while (stackIndex > 0)
    {
        s32 nodeIdx = stack[--stackIndex];
        CHECK_CONTINUE(nodeIdx < 0 || nodeIdx >= numNodes, AX_WARN("EmitRemappedNode index out of bounds %d", nodeIdx));

        if (nodeRemap[nodeIdx] != -1)
            continue;
 
        s32 newNode = (*numRemapped)++;
        nodeRemap[nodeIdx] = newNode;
        oldFromNew[newNode] = nodeIdx;

        const ANode* node = &scene->nodes[nodeIdx];
        for (s32 i = node->numChildren - 1; i >= 0; i--)
        {
            CHECK_CONTINUE(stackIndex >= numNodes, AX_WARN("EmitRemappedNode stack overflow"));
            stack[stackIndex++] = node->children[i];
        }
    }
}

void SceneBundle_BuildParentIndices(SceneBundle* scene)
{
    for (s32 i = 0; i < scene->numNodes; i++)
        scene->nodes[i].parent = -1;

    for (s32 i = 0; i < scene->numNodes; i++)
    {
        const ANode* node = &scene->nodes[i];
        for (s32 c = 0; c < node->numChildren; c++)
        {
            s32 child = node->children[c];
            CHECK_CONTINUE(child < 0 || child >= scene->numNodes, AX_WARN("node %d child index out of bounds %d", i, child));
            CHECK_CONTINUE(scene->nodes[child].parent != -1, AX_WARN("node %d has multiple parents: %d and %d", child, scene->nodes[child].parent, i));
            scene->nodes[child].parent = i;
        }
    }
}

void SceneBundle_FlattenNodes(SceneBundle* scene)
{
    const s32 numNodes = scene->numNodes;
    if (numNodes <= 0 || scene->nodes == NULL)
    {
        AX_WARN("Scene nodes invalid cannot flatten");
        return;
    }

    if (scene->rootNode < 0 || scene->rootNode >= numNodes)
    {
        AX_WARN("scene root node index out of bounds %d", scene->rootNode);
        scene->rootNode = 0;
    }

    uint64_t arenaStart = ArenaGetCurrentOffset();
    s32* nodeRemap  = (s32*)ArenaPushGlobal(numNodes * sizeof(s32));
    s32* oldFromNew = (s32*)ArenaPushGlobal(numNodes * sizeof(s32));
    s32* stack      = (s32*)ArenaPushGlobal(numNodes * sizeof(s32));

    for (s32 i = 0; i < numNodes; i++)
    {
        nodeRemap[i] = -1;
        oldFromNew[i] = -1;
    }

    // Build parents before remapping so every downstream buffer can use parent indices directly.
    SceneBundle_BuildParentIndices(scene);

    s32 numRemapped = 0;
    // Pass 1: emit root node 
    EmitRemappedNode(scene, scene->rootNode, nodeRemap, oldFromNew, &numRemapped, stack);

    // Pass 2: nodes referenced by named scenes get next indices
    for (s32 s = 0; s < scene->numScenes; s++)
        for (s32 i = 0; i < scene->scenes[s].numNodes; i++)
            EmitRemappedNode(scene, scene->scenes[s].nodes[i], nodeRemap, oldFromNew, &numRemapped, stack);

    // Pass 3: everything else, in original order, gets whatever's left
    for (s32 i = 0; i < numNodes; i++)
        EmitRemappedNode(scene, i, nodeRemap, oldFromNew, &numRemapped, stack);

    ASSERT(numRemapped == numNodes);

    ANode* newNodes = (ANode*)ArenaPushGlobal(numNodes * sizeof(ANode));
    for (s32 newIdx = 0; newIdx < numNodes; newIdx++)
    {
        s32 oldIdx = oldFromNew[newIdx];
        ASSERT(oldIdx >= 0);
        newNodes[newIdx] = scene->nodes[oldIdx];

        s32 oldParent = scene->nodes[oldIdx].parent;
        newNodes[newIdx].parent = oldParent >= 0 ? nodeRemap[oldParent] : -1;

        for (s32 c = 0; c < newNodes[newIdx].numChildren; c++)
            newNodes[newIdx].children[c] = nodeRemap[newNodes[newIdx].children[c]];
    }

    // Keep original node allocation ownership; only rewrite its contents in flattened order.
    MemCopy(scene->nodes, newNodes, sizeof(ANode) * numNodes);
    scene->rootNode = nodeRemap[scene->rootNode];

    for (s32 s = 0; s < scene->numScenes; s++)
        for (s32 i = 0; i < scene->scenes[s].numNodes; i++)
        {
            s32 node = scene->scenes[s].nodes[i];
            scene->scenes[s].nodes[i] = node >= 0 && node < numNodes ? nodeRemap[node] : -1;
        }

    for (s32 s = 0; s < scene->numSkins; s++)
    {
        ASkin* skin = &scene->skins[s];
        if (skin->skeleton >= 0)
            skin->skeleton = skin->skeleton < numNodes ? nodeRemap[skin->skeleton] : -1;
        for (s32 i = 0; i < skin->numJoints; i++)
        {
            s32 joint = skin->joints[i];
            skin->joints[i] = joint >= 0 && joint < numNodes ? nodeRemap[joint] : -1;
        }
    }

    for (s32 a = 0; a < scene->numAnimations; a++)
    {
        AAnimation* animation = &scene->animations[a];
        for (s32 c = 0; c < animation->numChannels; c++)
        {
            s32 target = animation->channels[c].targetNode;
            animation->channels[c].targetNode = target >= 0 && target < numNodes ? nodeRemap[target] : -1;
        }
    }

    ArenaSetCurrentOffset(arenaStart);
}

static const char* NodeName(const SceneBundle* scene, s32 nodeIdx)
{
    if (nodeIdx < 0 || nodeIdx >= scene->numNodes || scene->nodes[nodeIdx].name == NULL)
        return "<unnamed>";
    return scene->nodes[nodeIdx].name;
}

static bool NodeIsDescendantOf(const SceneBundle* scene, s32 nodeIdx, s32 parentIdx)
{
    while (nodeIdx >= 0 && nodeIdx < scene->numNodes)
    {
        if (nodeIdx == parentIdx)
            return true;
        nodeIdx = scene->nodes[nodeIdx].parent;
    }
    return false;
}

void SceneBundle_ValidateNodeHierarchy(const SceneBundle* scene)
{
    s32 numRoots = 0;
    for (s32 i = 0; i < scene->numNodes; i++)
        numRoots += scene->nodes[i].parent == -1;

    if (numRoots > 1)
        AX_WARN("scene has %d root nodes after flatten; rendering may need scene root iteration", numRoots);

    for (s32 s = 0; s < scene->numScenes; s++)
    {
        const AScene* sceneRoot = &scene->scenes[s];
        if (sceneRoot->numNodes <= 0)
            AX_WARN("scene %d has no root nodes", s);

        for (s32 i = 0; i < sceneRoot->numNodes; i++)
            if (sceneRoot->nodes[i] < 0 || sceneRoot->nodes[i] >= scene->numNodes)
                AX_WARN("scene %d root %d points to invalid node %d", s, i, sceneRoot->nodes[i]);
    }

    if (scene->numSkins > 0)
    {
        for (s32 s = 0; s < scene->numSkins; s++)
        {
            const ASkin* skin = &scene->skins[s];
            if (skin->numJoints > MAX_BONES)
                AX_WARN("skinned mesh has %d joints, max supported is %d", skin->numJoints, MAX_BONES);

            if (skin->skeleton < -1 || skin->skeleton >= scene->numNodes)
                AX_WARN("skin %d skeleton points to invalid node %d", s, skin->skeleton);

            for (s32 i = 0; i < skin->numJoints; i++)
            {
                CHECK_CONTINUE(skin->joints[i] < 0 || skin->joints[i] >= scene->numNodes, AX_WARN("skin %d joint %d points to invalid node %d", s, i, skin->joints[i]));

                if (skin->skeleton >= 0 && !NodeIsDescendantOf(scene, skin->joints[i], skin->skeleton))
                    AX_WARN("skin %d joint %d node %d (%s) is outside skeleton root %d (%s)", s, i, skin->joints[i], NodeName(scene, skin->joints[i]), skin->skeleton, NodeName(scene, skin->skeleton));
            }
        }

        if (scene->numNodes > ANIM_NODE_COUNT)
            AX_WARN("skinned scene has %d nodes, GPU animation hierarchy stores %d", scene->numNodes, ANIM_NODE_COUNT);
    }

    for (s32 a = 0; a < scene->numAnimations; a++)
    {
        const AAnimation* animation = &scene->animations[a];
        if (animation->numSamplers <= 0)
            AX_WARN("animation %d (%s) has no samplers", a, animation->name ? animation->name : "<unnamed>");

        for (s32 s = 0; s < animation->numSamplers; s++)
        {
            const AAnimSampler* sampler = &animation->samplers[s];
            if (sampler->count <= 0)
                AX_WARN("animation %d sampler %d has no keys", a, s);
            if (sampler->interpolation == ASamplerInterpolation_CubicSpline)
                AX_WARN("animation %d sampler %d uses cubic spline interpolation; runtime treats it as linear", a, s);
        }

        s32 numInvalidSampler = 0;
        s32 numInvalidNode    = 0;
        s32 numInvalidTarget[AAnimTargetPath_Weight+1] = {0};
        
        for (s32 c = 0; c < animation->numChannels; c++)
        {
            const AAnimChannel* channel = &animation->channels[c];
            numInvalidSampler += (channel->sampler < 0 || channel->sampler >= animation->numSamplers);
            numInvalidNode    += (channel->targetNode < 0 || channel->targetNode >= scene->numNodes);
            numInvalidTarget[channel->targetPath]++;
        }

        if (numInvalidSampler) AX_WARN("animation channels references invalid sampler %d", numInvalidSampler);
        if (numInvalidNode   ) AX_WARN("animation channels targets invalid node count: %d", numInvalidNode);
        if (numInvalidTarget[AAnimTargetPath_Weight])
            AX_WARN("animation channels unsupported target weight count: %d", numInvalidTarget[AAnimTargetPath_Weight]);
        if (numInvalidTarget[AAnimTargetPath_Scale])
            AX_WARN("animation channels unsupported target scale count: %d", numInvalidTarget[AAnimTargetPath_Scale]);
    }

    for (s32 i = 0; i < scene->numNodes; i++)
    {
        const ANode* node = &scene->nodes[i];
        if (node->parent >= i && node->parent != -1)
            AX_WARN("node %d parent %d is not before child after flatten", i, node->parent);

        for (s32 c = 0; c < node->numChildren; c++)
        {
            s32 child = node->children[c];
            if (child < 0 || child >= scene->numNodes)
                AX_WARN("node %d child index out of bounds %d", i, child);
            else if (scene->nodes[child].parent != i)
                AX_WARN("node %d child %d has parent %d", i, child, scene->nodes[child].parent);
        }
    }
}

static void SceneBundle_ReportImport(const SceneBundle* scene)
{
    AX_LOG("asset import report: nodes=%d meshes=%d materials=%d textures=%d images=%d skins=%d animations=%d scenes=%d",
           scene->numNodes, scene->numMeshes, scene->numMaterials, scene->numTextures,
           scene->numImages, scene->numSkins, scene->numAnimations, scene->numScenes);

    AX_LOG("asset import report: rootNode=%d (%s), defaultScene=%d, flattened=yes",
           scene->rootNode, NodeName(scene, scene->rootNode), scene->defaultSceneIndex);

    for (s32 s = 0; s < scene->numSkins; s++)
    {
        const ASkin* skin = &scene->skins[s];
        AX_LOG("asset import report: skin[%d] joints=%d skeleton=%d (%s)", s, skin->numJoints, skin->skeleton, NodeName(scene, skin->skeleton));
    }

    for (s32 a = 0; a < scene->numAnimations; a++)
    {
        const AAnimation* animation = &scene->animations[a];
        AX_LOG("asset import report: anim[%d] name=%s duration=%.3f samplers=%d channels=%d",
               a, animation->name ? animation->name : "<unnamed>", animation->duration,
               animation->numSamplers, animation->numChannels);
    }
}

void SceneBundle_Normalize(SceneBundle* scene)
{
    // Normalize once after parsing/import so cache and runtime see the same scene semantics.
    SceneBundle_FlattenNodes(scene);
    SceneBundle_ValidateNodeHierarchy(scene);
    SceneBundle_ReportImport(scene);
}
