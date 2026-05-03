
#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/AssetManager.h"
#include "Include/Random.h"
#include "Include/Camera.h"
#include "Include/Memory.h"
#include "Math/Half.h"
#include "Include/ECS.h"

#if defined(PLATFORM_APPLE)
#include "Shaders/msl/SkinnedFrag.msl.h"
#include "Shaders/msl/SkinnedVert.msl.h"
#define Shaders_SkinnedFrag_spv      Shaders_SkinnedFrag_msl
#define Shaders_SkinnedFrag_spv_size Shaders_SkinnedFrag_msl_size
#define Shaders_SkinnedVert_spv      Shaders_SkinnedVert_msl
#define Shaders_SkinnedVert_spv_size Shaders_SkinnedVert_msl_size
#elif defined(PLATFORM_WINDOWS)
// Shaders_SkinnedFrag_spv
#include "Shaders/spv/SkinnedFrag.spv.h"
#include "Shaders/spv/SkinnedVert.spv.h"
#endif

#if defined(PLATFORM_APPLE)
#include "Shaders/msl/AnimationCompute.msl.h"
#define Shaders_AnimationCompute_spv      Shaders_AnimationCompute_msl
#define Shaders_AnimationCompute_spv_size Shaders_AnimationCompute_msl_size
#elif defined(PLATFORM_WINDOWS)
#include "Shaders/spv/AnimationCompute.spv.h"
#include "Shaders/spv/CullDrawArgsCompute.spv.h"
#endif

#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)

typedef struct GPUAnimationInstance_
{
    u32 animIdx;
    f32 timeOffset;
} GPUAnimationInstance;

typedef struct GPUAnimationData_
{
    u32 frameOffset;
    u32 numFrames;
    u32 rootNodeIndex;
    u32 numJoints;
    u32 numNodes;
    f32 duration;
} GPUAnimationData;


SceneBundle*        gPaladin;

WindowState         g_WindowState;
RenderState         g_RenderState;
SDL_GPUDevice*      g_GPUDevice = NULL;

u32 animPoses[MAX_BONES * MAX_GPU_ANIM_FRAMES * ANIM_POSE_NUM_INT32]; // 32mb
u32 animHierarchy[ANIM_NODE_COUNT + ANIM_CHILD_PACKED_COUNT];
u32 animJoints[MAX_BONES * MAX_SKIN_COUNT  * 2];
u32 invBindMatrices[MAX_BONES * MAX_SKIN_COUNT  * 2 * ANIM_MATRIX_NUM_INT32];
GPUAnimationInstance animInstances[MAX_ANIM_INSTANCES];
GPUAnimationData animData[MAX_ANIM_COUNT];
u32 numGPUAnimations;

static SDL_GPUComputePipeline* g_AnimComputePipeline = NULL;
static SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline = NULL;

extern SDL_Window*  g_SDLWindow; // main    
extern Camera       g_Camera; // main
extern Graphics     gGFX;
extern ECS          ecsSkinned;
extern ECS          ecsStatic;

static void StoreHalf4(u32* dst, const f32* src)
{
    f16 h[4];
    Float4ToHalf4(h, src);
    dst[0] = (u32)h[0] | ((u32)h[1] << 16);
    dst[1] = (u32)h[2] | ((u32)h[3] << 16);
}

int AnimationGetGPUData(AnimationController* ac, int animIdx, int frameOffset)
{
    const AAnimation* animation = &ac->mPrefab->animations[animIdx];
    const ASkin*      skin      = &ac->mPrefab->skins[0];
    const int framePerSecond    = ANIM_NUM_FRAMES;
    int numFrames = (int)(animation->duration * framePerSecond);
    int numPose   = frameOffset * MAX_BONES;
    MemsetZero(animHierarchy, sizeof(animHierarchy));

    for (int i = 0; i < numFrames; i++)
    {
        float norm = (float)i / (float)numFrames;
        AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, animIdx, norm);
        for (int poseIdx = 0; poseIdx < MAX_BONES; poseIdx++)
        {
            u32* outPose = animPoses + ((numPose + poseIdx) * ANIM_POSE_NUM_INT32);
            StoreHalf4(outPose, ac->mAnimPoseA[poseIdx].translation.m128_f32);
            StoreHalf4(outPose + 2, ac->mAnimPoseA[poseIdx].rotation.m128_f32);
        }
        numPose += MAX_BONES;
    }

    animData[animIdx] = (GPUAnimationData){
        .frameOffset   = (u32)frameOffset,
        .numFrames     = (u32)numFrames,
        .rootNodeIndex = (u32)ac->mRootNodeIndex,
        .numJoints     = (u32)skin->numJoints,
        .numNodes      = (u32)ac->mPrefab->numNodes,
        .duration      = animation->duration
    };

    for (int i = 0; i < ANIM_NODE_COUNT; i++)
    {
        const ANode* node = i < ac->mPrefab->numNodes ? &ac->mPrefab->nodes[i] : NULL;
        u32 parent = node && node->parent >= 0 ? (u32)node->parent : 0xFFFFu;
        animHierarchy[i] = parent;
    }

    for (int i = 0; i < skin->numJoints; i++)
        animJoints[i] = (u32)skin->joints[i];

    const m44* inv = (const m44*)skin->inverseBindMatrices;
    for (int i = 0; i < skin->numJoints; i++)
    {
        u32* outMtx = invBindMatrices + (i * ANIM_MATRIX_NUM_INT32);
        StoreHalf4(outMtx + 0, inv[i].m[0]);
        StoreHalf4(outMtx + 2, inv[i].m[1]);
        StoreHalf4(outMtx + 4, inv[i].m[2]);
        StoreHalf4(outMtx + 6, inv[i].m[3]);
    }
    return numFrames;
}


static void InitAnimationComputePipeline()
{
    g_AnimComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_AnimationCompute_spv,
        .code_size                     = sizeof(Shaders_AnimationCompute_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 6,
        .num_readwrite_storage_buffers = 1,
        .threadcount_x                 = 32,
        .threadcount_y                 = 1,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_AnimComputePipeline, "Animation Compute Pipeline")
}

static void InitCullDrawArgsComputePipeline()
{
    g_CullDrawArgsComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_CullDrawArgsCompute_spv,
        .code_size                     = sizeof(Shaders_CullDrawArgsCompute_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 2,
        .num_readwrite_storage_buffers = 4,
        .threadcount_x                 = 64,
        .threadcount_y                 = 1,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_CullDrawArgsComputePipeline, "Cull Draw Args Compute Pipeline")
}

void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd)
{
    struct {
        float timeSinceStartup;
        int   numInstances;
    } params;
    params.timeSinceStartup = (float)TimeSinceStartup();
    params.numInstances     = MAX_ANIM_INSTANCES;
    
    SDL_GPUStorageBufferReadWriteBinding rw_binding = { g_RenderState.boneBuffer };

    SDL_GPUBuffer* ro_buffers[6] = {
        g_RenderState.animPoseBuffer,
        g_RenderState.animHierarchyBuffer,
        g_RenderState.animDataBuffer,
        g_RenderState.jointsBuffer,
        g_RenderState.invBindBuffer,
        g_RenderState.animInstanceBuffer
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw_binding, 1);
    SDL_BindGPUComputePipeline(pass, g_AnimComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

    SDL_DispatchGPUCompute(pass, (MAX_ANIM_INSTANCES + 31) / 32, 1, 1);
    SDL_EndGPUComputePass(pass);
}

static void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, ECS* renderSet,
                                        SDL_GPUBuffer* entityBuffer,
                                        SDL_GPUBuffer* primitiveGroupBuffer,
                                        SDL_GPUBuffer* drawDenseIndicesBuffer,
                                        SDL_GPUBuffer* drawArgsBuffer,
                                        SDL_GPUBuffer* denseToPrimitiveBuffer,
                                        SDL_GPUBuffer* numVisibleBuffer,
                                        FrustumPlanes frustumPlanes)
{
    if (renderSet->numGroups == 0) return;

    struct {
        v128f planes[6];
        u32 numEntities;
        u32 numPrimitiveGroups;
        u32 mode;
        u32 pad0;
    } params;
    MemCopy(params.planes, frustumPlanes.planes, sizeof(params.planes));
    params.numEntities = renderSet->numEntities;
    params.numPrimitiveGroups = renderSet->numGroups;
    params.mode = 0;
    params.pad0 = 0;

    SDL_GPUBuffer* ro_buffers[2] = { entityBuffer, primitiveGroupBuffer };
    SDL_GPUStorageBufferReadWriteBinding rw_bindings[4] = {
        { drawDenseIndicesBuffer },
        { drawArgsBuffer },
        { denseToPrimitiveBuffer },
        { numVisibleBuffer }
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_CullDrawArgsComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (renderSet->numGroups + 63) / 64, 1, 1);

    if (renderSet->numEntities > 0)
    {
        params.mode = 1;
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (renderSet->numEntities + 63) / 64, 1, 1);
    }
    SDL_EndGPUComputePass(pass);
}

void InitAnimationInstances(void)
{
    for (u32 i = 0; i < MAX_ANIM_INSTANCES; i++)
    {
        u32 hash = WangHash(i + 645u);
        u32 animIdx = hash % numGPUAnimations;
        f32 duration = animData[animIdx].duration;

        animInstances[i] = (GPUAnimationInstance){
            .animIdx = animIdx,
            .timeOffset = NextFloat01(hash) * duration,
        };
    }
}

void InitAnimationFrames(AnimationController* ac)
{
    int frameOffset = 0;
    numGPUAnimations = (u32)MMIN(gPaladin->numAnimations, MAX_ANIM_COUNT);
    for (u32 animIdx = 0; animIdx < numGPUAnimations; animIdx++)
    {
        int numFrames = (int)(gPaladin->animations[animIdx].duration * ANIM_NUM_FRAMES);
        if (frameOffset + numFrames > MAX_GPU_ANIM_FRAMES)
        {
            numGPUAnimations = animIdx;
            break;
        }
        numFrames = AnimationGetGPUData(ac, (int)animIdx, frameOffset);
        frameOffset += numFrames;
    }
    AX_LOG("num animation: %d", numGPUAnimations);
}

void InitBuffers()
{
    /* Create anim buffers */
    const Uint32 readRasterBit   = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    const Uint32 writeComputeBit = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const Uint32 readCompute     = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    const Uint32 indirectBit     = SDL_GPU_BUFFERUSAGE_INDIRECT;
    const size_t maxBoneMatrices = sizeof(half3x4) * MAX_BONES * MAX_ANIM_INSTANCES;
    const size_t skinnedEntityBytes = ecsSkinned.maxEntities * sizeof(Entity);
    const size_t skinnedGroupBytes  = ecsSkinned.maxGroups * sizeof(PrimitiveGroup);
    const size_t staticEntityBytes  = ecsStatic.maxEntities * sizeof(Entity);
    const size_t staticGroupBytes   = ecsStatic.maxGroups * sizeof(PrimitiveGroup);
    
    // indirect and culling
    g_RenderState.skinnedDrawDenseIndicesBuffer  = CreateBuffer(NULL, ecsSkinned.maxEntities * sizeof(u32), readRasterBit | writeComputeBit, "CPSkinnedDrawDenseIndices");
    g_RenderState.skinnedDrawArgsBuffer          = CreateBuffer(NULL, ecsSkinned.maxGroups * sizeof(SDL_GPUIndexedIndirectDrawCommand), indirectBit | writeComputeBit, "CPSkinnedDrawArgs");
    g_RenderState.skinnedDenseToPrimitiveBuffer  = CreateBuffer(NULL, ecsSkinned.maxEntities * sizeof(u32), writeComputeBit, "CPSkinnedDenseToPrimitive");
    g_RenderState.skinnedNumVisibleBuffer        = CreateBuffer(NULL, ecsSkinned.maxGroups * sizeof(u32), writeComputeBit, "CPSkinnedNumVisible");
    g_RenderState.staticEntityBuffer             = CreateBuffer(ecsStatic.entities, staticEntityBytes, readRasterBit | readCompute, "CPStaticEntities");
    g_RenderState.staticPrimitiveGroupBuffer     = CreateBuffer(ecsStatic.primitiveGroups, staticGroupBytes, readCompute, "CPStaticPrimitiveGroups");
    g_RenderState.staticDrawDenseIndicesBuffer   = CreateBuffer(NULL, ecsStatic.maxEntities * sizeof(u32), readRasterBit | writeComputeBit, "CPStaticDrawDenseIndices");
    g_RenderState.staticDrawArgsBuffer           = CreateBuffer(NULL, ecsStatic.maxGroups * sizeof(SDL_GPUIndexedIndirectDrawCommand), indirectBit | writeComputeBit, "CPStaticDrawArgs");
    g_RenderState.staticDenseToPrimitiveBuffer   = CreateBuffer(NULL, ecsStatic.maxEntities * sizeof(u32), writeComputeBit, "CPStaticDenseToPrimitive");
    g_RenderState.staticNumVisibleBuffer         = CreateBuffer(NULL, ecsStatic.maxGroups * sizeof(u32), writeComputeBit, "CPStaticNumVisible");

    g_RenderState.boneBuffer = CreateBuffer(NULL, maxBoneMatrices , readRasterBit | writeComputeBit, "CPJointMatrices");
    g_RenderState.entityBuffer        = CreateBuffer(ecsSkinned.entities, skinnedEntityBytes, readRasterBit | readCompute, "CPSkinnedEntities");
    g_RenderState.skinnedPrimitiveGroupBuffer = CreateBuffer(ecsSkinned.primitiveGroups, skinnedGroupBytes, readCompute, "CPSkinnedPrimitiveGroups");
    g_RenderState.animPoseBuffer      = CreateBuffer(animPoses      , sizeof(animPoses)      , readCompute, "CPAnimPoses");
    g_RenderState.animHierarchyBuffer = CreateBuffer(animHierarchy  , sizeof(animHierarchy)  , readCompute, "CPAnimHierarchy");
    g_RenderState.animDataBuffer      = CreateBuffer(animData       , sizeof(animData)       , readCompute, "CPAnimationData");
    g_RenderState.jointsBuffer        = CreateBuffer(animJoints     , sizeof(animJoints)     , readCompute, "CPjointsBuffer ");
    g_RenderState.invBindBuffer       = CreateBuffer(invBindMatrices, sizeof(invBindMatrices), readCompute, "CPinvBindBuffer");
    g_RenderState.animInstanceBuffer = CreateBuffer(animInstances, sizeof(animInstances), readCompute, "CPAnimationInstances");

    /* Create skined */
    g_RenderState.vertexBuffer = CreateBuffer(gGFX.VertexBuffer, MAX_VERTEX * sizeof(ASkinedVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPVertexBuffer");
    g_RenderState.indexBuffer  = CreateBuffer(gGFX.IndexBuffer , MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");

}

static void RenderScene(SDL_GPUCommandBuffer* cmd, 
                        SDL_GPUColorTargetInfo* color_target, 
                        SDL_GPUDepthStencilTargetInfo* depth_target)
{
    SDL_GPUTexture* tex = g_RenderState.textures[gPaladin->materials[0].baseColorTexture.index].handle;
    m44 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    struct {
        m44 viewProj;
        u32 visibleOffset;
        u32 pad[3];
    } vertexParams;
    vertexParams.viewProj = viewProj;
    vertexParams.visibleOffset = 0;
    vertexParams.pad[0] = vertexParams.pad[1] = vertexParams.pad[2] = 0;

    /* Set up the bindings */
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    vertex_binding.buffer = g_RenderState.vertexBuffer;
    vertex_binding.offset = 0;
    index_binding.buffer  = g_RenderState.indexBuffer;
    index_binding.offset  = 0;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, color_target, 1, depth_target);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    SDL_GPUBuffer* buffers[3] = { g_RenderState.boneBuffer, g_RenderState.entityBuffer, g_RenderState.skinnedDrawDenseIndicesBuffer };
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
    
    SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
        .texture = tex, 
        .sampler = g_RenderState.sampler
    }, 1);
    
    for (u32 i = 0; i < ecsSkinned.numGroups; i++)
    {
        const PrimitiveGroup* group = ecsSkinned.primitiveGroups + i;
        if (!group->valid || group->numEntities == 0) continue;

        vertexParams.visibleOffset = group->entityOffset;
        SDL_PushGPUVertexUniformData(cmd, 0, &vertexParams, sizeof(vertexParams));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass,
                                             g_RenderState.skinnedDrawArgsBuffer,
                                             i * sizeof(SDL_GPUIndexedIndirectDrawCommand),
                                             1);
    }
    SDL_EndGPURenderPass(pass);
}

void Render()
{
    /* Acquire the swapchain texture */
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g_GPUDevice);
    if (!cmd) {
        AX_WARN("Failed to acquire command buffer :%s", SDL_GetError());
        Quit(2);
    }
    
    SDL_GPUTexture* swapchainTexture;
    Uint32 drawablew, drawableh;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, g_SDLWindow, &swapchainTexture, &drawablew, &drawableh)) {
        AX_WARN("Failed to acquire swapchain texture: %s", SDL_GetError());
        Quit(2);
    }
    
    if (swapchainTexture == NULL) {
        AX_WARN("Failed to acquire swapchain texture");
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    
    WindowState* winstate = &g_WindowState;
    /* Resize the depth buffer if the window size changed */
    if (winstate->prev_drawablew != drawablew || winstate->prev_drawableh != drawableh) {
        SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_depth);
        SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_msaa);
        SDL_ReleaseGPUTexture(g_GPUDevice, winstate->tex_resolve);
        winstate->tex_depth   = CreateDepthTexture(drawablew, drawableh);
        winstate->tex_msaa    = CreateMSAATexture(drawablew, drawableh);
        winstate->tex_resolve = CreateResolveTexture(drawablew, drawableh);
    }
    winstate->prev_drawablew = drawablew;
    winstate->prev_drawableh = drawableh;
    
    /* Set up the pass */
    SDL_GPUColorTargetInfo color_target;
    SDL_zero(color_target);
    color_target.clear_color.a = 1.0f;
    if (winstate->tex_msaa) {
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_RESOLVE;
        color_target.texture = winstate->tex_msaa;
        color_target.resolve_texture = winstate->tex_resolve;
        color_target.cycle = true;
        color_target.cycle_resolve_texture = true;
    }
    else {
        color_target.load_op  = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;
        color_target.texture  = swapchainTexture;
    }
    
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_zero(depth_target);
    depth_target.clear_depth      = 1.0f;
    depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op         = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.texture          = winstate->tex_depth;
    depth_target.cycle            = true;
    
    UpdateGPUBuffer(g_RenderState.entityBuffer, ecsSkinned.entities, ecsSkinned.maxEntities * sizeof(Entity));
    UpdateGPUBuffer(g_RenderState.staticEntityBuffer, ecsStatic.entities, ecsStatic.maxEntities * sizeof(Entity));
    
    DispatchAnimationCompute(cmd);

    m44 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    FrustumPlanes frustumPlanes = CreateFrustumPlanes(viewProj);
    DispatchCullDrawArgsCompute(cmd, &ecsSkinned,
                                g_RenderState.entityBuffer,
                                g_RenderState.skinnedPrimitiveGroupBuffer,
                                g_RenderState.skinnedDrawDenseIndicesBuffer,
                                g_RenderState.skinnedDrawArgsBuffer,
                                g_RenderState.skinnedDenseToPrimitiveBuffer,
                                g_RenderState.skinnedNumVisibleBuffer,
                                frustumPlanes);
    DispatchCullDrawArgsCompute(cmd, &ecsStatic,
                                g_RenderState.staticEntityBuffer,
                                g_RenderState.staticPrimitiveGroupBuffer,
                                g_RenderState.staticDrawDenseIndicesBuffer,
                                g_RenderState.staticDrawArgsBuffer,
                                g_RenderState.staticDenseToPrimitiveBuffer,
                                g_RenderState.staticNumVisibleBuffer,
                                frustumPlanes);
    
    RenderScene(cmd, &color_target, &depth_target);
    
    /* Blit MSAA resolve target to swapchain, if needed */
    if (g_RenderState.sample_count > SDL_GPU_SAMPLECOUNT_1) 
    {
        SDL_GPUBlitInfo blit_info;
        SDL_zero(blit_info);
        blit_info.source.texture = winstate->tex_resolve;
        blit_info.source.w = drawablew;
        blit_info.source.h = drawableh;
        
        blit_info.destination.texture = swapchainTexture;
        blit_info.destination.w = drawablew;
        blit_info.destination.h = drawableh;
        
        blit_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit_info.filter  = SDL_GPU_FILTER_LINEAR;
        
        SDL_BlitGPUTexture(cmd, &blit_info);
    }
    
    SDL_SubmitGPUCommandBuffer(cmd);
}

static void InitSamplers()
{
    g_RenderState.sampler = SDL_CreateGPUSampler(g_GPUDevice, &(SDL_GPUSamplerCreateInfo){
        .min_filter      = SDL_GPU_FILTER_LINEAR,
        .mag_filter      = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode     = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .min_lod         = 0.0f,
        .max_lod         = 8.0f
    });
}

static void InitSkinedPipeline()
{
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc;
    SDL_GPUColorTargetDescription     color_target_desc;
    SDL_GPUVertexAttribute            vertex_attributes[5];
    SDL_GPUVertexBufferDescription    vertex_buffer_desc;

    /* Create shaders */
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedVert_spv,
        .code_size           = sizeof(Shaders_SkinnedVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 3,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX
    });
    
    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedFrag_spv,
        .code_size           = sizeof(Shaders_SkinnedFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT
    });
    
    CHECK_CREATE(vertex_shader  , "Vertex Shader")
    CHECK_CREATE(fragment_shader, "Fragment Shader")

    /* Set up the graphics pipeline */
    SDL_zero(pipelinedesc);
    SDL_zero(color_target_desc);
    
    color_target_desc.format = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    
    pipelinedesc.target_info.num_color_targets = 1;
    pipelinedesc.target_info.color_target_descriptions  = &color_target_desc;
    pipelinedesc.target_info.depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    pipelinedesc.target_info.has_depth_stencil_target   = true;
    
    pipelinedesc.depth_stencil_state.enable_depth_test  = true;
    pipelinedesc.depth_stencil_state.enable_depth_write = true;
    pipelinedesc.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    
    pipelinedesc.multisample_state.sample_count = g_RenderState.sample_count;
    
    pipelinedesc.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    
    pipelinedesc.vertex_shader   = vertex_shader;
    pipelinedesc.fragment_shader = fragment_shader;
    
    vertex_buffer_desc.slot               = 0;
    vertex_buffer_desc.input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_desc.instance_step_rate = 0;
    vertex_buffer_desc.pitch              = sizeof(ASkinedVertex);
    
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format      = SDL_GPU_VERTEXELEMENTFORMAT_HALF4;
    vertex_attributes[0].location    = 0;
    vertex_attributes[0].offset      = 0;
    
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format      = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    vertex_attributes[1].location    = 1;
    vertex_attributes[1].offset      = sizeof(int) * 2;
    
    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format      = SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
    vertex_attributes[2].location    = 2;
    vertex_attributes[2].offset      = vertex_attributes[1].offset + sizeof(int);
    
    vertex_attributes[3].buffer_slot = 0;
    vertex_attributes[3].format      = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
    vertex_attributes[3].location    = 3;
    vertex_attributes[3].offset      = vertex_attributes[2].offset + sizeof(int);
    
    vertex_attributes[4].buffer_slot = 0;
    vertex_attributes[4].format      = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    vertex_attributes[4].location    = 4;
    vertex_attributes[4].offset      = vertex_attributes[3].offset + sizeof(int);
    
    pipelinedesc.vertex_input_state.num_vertex_buffers = 1;
    pipelinedesc.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_desc;
    pipelinedesc.vertex_input_state.num_vertex_attributes = SDL_arraysize(vertex_attributes);
    pipelinedesc.vertex_input_state.vertex_attributes = (SDL_GPUVertexAttribute*)&vertex_attributes;
    
    pipelinedesc.props = 0;
    
    g_RenderState.pipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.pipeline, "Render Pipeline")
    
    /* These are reference-counted; once the pipeline is created, you don't need to keep these. */
    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

void RendererInit()
{
    InitSamplers();
    InitSkinedPipeline();
    InitAnimationComputePipeline();
    InitCullDrawArgsComputePipeline();
}

static void DestroyPipeline()
{
    if (g_RenderState.vertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.vertexBuffer);
    if (g_RenderState.indexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.indexBuffer);
    if (g_RenderState.entityBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.entityBuffer);
    if (g_RenderState.skinnedPrimitiveGroupBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedPrimitiveGroupBuffer);
    if (g_RenderState.skinnedDrawDenseIndicesBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedDrawDenseIndicesBuffer);
    if (g_RenderState.skinnedDrawArgsBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedDrawArgsBuffer);
    if (g_RenderState.skinnedDenseToPrimitiveBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedDenseToPrimitiveBuffer);
    if (g_RenderState.skinnedNumVisibleBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedNumVisibleBuffer);
    if (g_RenderState.staticEntityBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticEntityBuffer);
    if (g_RenderState.staticPrimitiveGroupBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticPrimitiveGroupBuffer);
    if (g_RenderState.staticDrawDenseIndicesBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticDrawDenseIndicesBuffer);
    if (g_RenderState.staticDrawArgsBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticDrawArgsBuffer);
    if (g_RenderState.staticDenseToPrimitiveBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticDenseToPrimitiveBuffer);
    if (g_RenderState.staticNumVisibleBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.staticNumVisibleBuffer);
    if (g_RenderState.pipeline)     SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.pipeline);
    if (g_AnimComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_AnimComputePipeline);
    if (g_CullDrawArgsComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_CullDrawArgsComputePipeline);
    
    SDL_zero(g_RenderState);
    g_GPUDevice = NULL;
}

void Quit(s32 rc)
{
    DestroyPipeline();
    GraphicsDestroy();
    exit(rc);
}
