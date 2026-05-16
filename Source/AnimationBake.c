#include "Include/AssetManager.h"
#include "Include/Animation.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Math/Matrix.h"

#define IS_POISON_PTR(ptr) ((uintptr_t)(ptr) == (uintptr_t)0xCDCDCDCDCDCDCDCDull)

// Mark animation-root descendants that need import-time root-scale compensation.
static void MarkScaledNodes(const SceneBundle* gltf, s32 rootIndex, u8 scaledNodes[MAX_BONES * 2])
{
    s32 stack[MAX_BONES * 2];
    s32 stackIndex = 0;

    if (rootIndex < 0 || rootIndex >= gltf->numNodes)
        return;

    for (s32 i = 0; i < gltf->nodes[rootIndex].numChildren && stackIndex < ARRAY_SIZE(stack); i++)
        stack[stackIndex++] = gltf->nodes[rootIndex].children[i];

    while (stackIndex > 0)
    {
        s32 nodeIndex = stack[--stackIndex];
        if (nodeIndex < 0 || nodeIndex >= gltf->numNodes)
            continue;

        const ANode* node = &gltf->nodes[nodeIndex];
        scaledNodes[nodeIndex] = 1;

        for (s32 i = 0; i < node->numChildren && stackIndex < ARRAY_SIZE(stack); i++)
            stack[stackIndex++] = node->children[i];
    }
}

static bool ShouldScaleTranslationSampler(const AAnimation* animation, const u8 scaledNodes[MAX_BONES * 2], s32 samplerIndex)
{
    for (s32 c = 0; c < animation->numChannels; c++)
    {
        const AAnimChannel* channel = &animation->channels[c];
        if (channel->sampler == samplerIndex &&
            channel->targetPath == AAnimTargetPath_Translation &&
            channel->targetNode >= 0 && channel->targetNode < (MAX_BONES * 2) &&
            scaledNodes[channel->targetNode])
            return true;
    }
    return false;
}

void BakeGLTFAnimations(SceneBundle* gltf)
{
    if (gltf == NULL || gltf->numSkins <= 0 || gltf->skins == NULL || IS_POISON_PTR(gltf->skins) ||
        gltf->nodes == NULL || IS_POISON_PTR(gltf->nodes) || gltf->numNodes <= 0)
        return;

    s32 rootIndex = Prefab_FindAnimRootNodeIndex(gltf);
    if (rootIndex < 0 || rootIndex >= gltf->numNodes)
    {
        AX_WARN("animation root index out of bounds %d", rootIndex);
        return;
    }

    f32 rootScale = gltf->nodes[rootIndex].scale[1];
    v128f rootScaleMul = VecSetR(rootScale, rootScale, rootScale, 1.0f);
    u8 scaledNodes[MAX_BONES * 2] = {0};
    AX_LOG("animation bake: root=%d rootScale=%f skins=%d animations=%d", rootIndex, rootScale, gltf->numSkins, gltf->numAnimations);

    MarkScaledNodes(gltf, rootIndex, scaledNodes);

    // Import-time root-scale compensation keeps runtime animation shaders free of asset-specific scale hacks.
    for (s32 i = 0; i < gltf->numNodes; i++)
    {
        if (!scaledNodes[i]) continue;
        v128f translation = VecMulf(VecLoad(gltf->nodes[i].translation), rootScale);
        VecStore(gltf->nodes[i].translation, translation);
    }

    gltf->nodes[rootIndex].scale[0] = 1.0f;
    gltf->nodes[rootIndex].scale[1] = 1.0f;
    gltf->nodes[rootIndex].scale[2] = 1.0f;

    for (s32 s = 0; s < gltf->numSkins; s++)
    {
        ASkin* skin = &gltf->skins[s];
        if (skin->numJoints <= 0 || skin->joints == NULL || skin->inverseBindMatrices == NULL)
        {
            AX_WARN("skin %d invalid for animation bake joints=%d", s, skin->numJoints);
            continue;
        }
        if (skin->numJoints > MAX_BONES)
            AX_WARN("skin %d has %d joints, max GPU bone count is %d", s, skin->numJoints, MAX_BONES);

        mat4x4* inverseBindMatrices = AllocateTLSFGlobal(skin->numJoints * sizeof(mat4x4));
        SmallMemCpy(inverseBindMatrices, skin->inverseBindMatrices, sizeof(mat4x4) * skin->numJoints);
        skin->inverseBindMatrices = (f32*)inverseBindMatrices;

        for (s32 i = 0; i < skin->numJoints; i++)
        {
            if (skin->joints[i] == rootIndex) continue;
            mat4x4 inv = inverseBindMatrices[i];
            inv.r[0] = VecNorm(inv.r[0]);
            inv.r[1] = VecNorm(inv.r[1]);
            inv.r[2] = VecNorm(inv.r[2]);
            inv.r[3] = VecMul(inv.r[3], rootScaleMul);
            inverseBindMatrices[i] = inv;
        }
    }

    if (gltf->numAnimations <= 0)
        return;

    s32 totalSamplerInput = 0;
    for (s32 a = 0; a < gltf->numAnimations; a++)
        for (s32 s = 0; s < gltf->animations[a].numSamplers; s++)
            totalSamplerInput += gltf->animations[a].samplers[s].count;

    f32* currSampler = (f32*)AllocZeroTLSFGlobal(totalSamplerInput, 4);
    v128f* currOutput = (v128f*)AllocZeroTLSFGlobal(totalSamplerInput, sizeof(v128f));

    for (s32 a = 0; a < gltf->numAnimations; a++)
    {
        s32 numInvalidComponents = 0;
        s32 numNonFloatsIn = 0;
        s32 numNonFloatOut = 0;
        s32 numCubic = 0;

        for (s32 s = 0; s < gltf->animations[a].numSamplers; s++)
        {
            const bool scaleTranslation = ShouldScaleTranslationSampler(&gltf->animations[a], scaledNodes, s);
            AAnimSampler* sampler = &gltf->animations[a].samplers[s];
            if (sampler->count <= 0)
            {
                AX_WARN("animation %d sampler %d has no keys", a, s);
                continue;
            }

            SmallMemCpy(currSampler, sampler->input, sampler->count * sizeof(f32));
            sampler->input = currSampler;
            currSampler += sampler->count;

            numCubic += (sampler->interpolation == ASamplerInterpolation_CubicSpline);
            numNonFloatsIn += (sampler->inputType != AComponentType_FLOAT);
            numNonFloatOut += (sampler->outputType != AComponentType_FLOAT);
            numInvalidComponents += (sampler->numComponent != 4 && sampler->numComponent != 3);

            for (s32 i = 0; i < sampler->count; i++)
            {
                SmallMemCpy(currOutput + i, sampler->output + (i * sampler->numComponent), sizeof(f32) * sampler->numComponent);
                currOutput[i] = VecLoad(sampler->output + (i * sampler->numComponent));
                if (sampler->numComponent == 3) currOutput[i] = VecSetW(currOutput[i], 0.0f);
                if (scaleTranslation)
                    currOutput[i] = VecMulf(currOutput[i], rootScale);
            }

            sampler->output = (f32*)currOutput;
            currOutput += sampler->count;
        }

        if (numCubic)       AX_WARN("sampler cubic spline not supported numCubic: %d", numCubic);
        if (numNonFloatsIn) AX_WARN("unsupported sampler input type: %d", numNonFloatsIn);
        if (numNonFloatOut) AX_WARN("unsupported sampler output type: %d", numNonFloatOut);
        if (numInvalidComponents) AX_WARN("anim sampler num components has to be 4 or 3 numInvalid: %d", numInvalidComponents);
    }
}
