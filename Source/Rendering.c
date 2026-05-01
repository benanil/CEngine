
#include "Include/Rendering.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/AssetManager.h"
#include "Include/Random.h"
#include "Include/Camera.h"
#include "Include/Memory.h"
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
#endif

#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)

static AnimationController AnimControllers[NUM_ANIMS];
AX_ALIGN(4) half3x4 OutMatrices[MaxBonePoses * NUM_ANIMS]; // for cpu only

SceneBundle*        gPaladin;

WindowState         g_WindowState;
RenderState         g_RenderState;
SDL_GPUDevice*      g_GPUDevice = NULL;

static SDL_GPUComputePipeline* g_AnimComputePipeline = NULL;

extern SDL_Window*  g_SDLWindow; // main
extern Camera       g_Camera; // main
extern Graphics     gGFX;
extern ECS          ecs;
extern bool         g_UseGPUComputeAnimation;

#define ANIM_POSE_NUM_INT32 4
Pose        animPoses[MaxBonePoses * ANIM_NUM_FRAMES * MAX_ANIM_DURATION * MAX_ANIM_COUNT];
u32         animNodes[MaxBonePoses * MAX_SKIN_COUNT  * 2];
u32        animJoints[MaxBonePoses * MAX_SKIN_COUNT  * 2];
f16_3x4    outBoneMtx[MaxBonePoses * NUM_ANIMS];
m44   invBindMatrices[MaxBonePoses * MAX_SKIN_COUNT  * 2];
u32  animChildIndices[MaxBonePoses * MAX_SKIN_COUNT  * 2];

#define AnimIDX 0
int AnimationGetGPUData(AnimationController* ac)
{
    const AAnimation* animation = &ac->mPrefab->animations[AnimIDX];
    const ASkin*      skin      = &ac->mPrefab->skins[0];
    const int framePerSecond    = ANIM_NUM_FRAMES;
    int numFrames = (int)(animation->duration * framePerSecond);
    int numPose   = 0;

    // for (s32 i = 0; i < MaxBonePoses; i++)
    // {
    //     const float* pos = ac->mAnimPoseA[i].translation.m128_f32;
    //     const float* rot = ac->mAnimPoseA[i].rotation.m128_f32;
    //     printf("%f, %f, %f, %f\n", pos[0], pos[1], pos[2], pos[3]);
    //     printf("%f, %f, %f, %f\n", rot[0], rot[1], rot[2], rot[3]);
    // }

    for (int i = 0; i <= numFrames; i++)
    {
        float norm = 0.0f; // (float)i / (float)numFrames;
        AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, AnimIDX, norm);
        MemCopy(animPoses + numPose, ac->mAnimPoseA, MaxBonePoses * sizeof(Pose));
        numPose += MaxBonePoses;
    }

    for (int i = 0; i < ac->mNumJoints * 2; i++)
    {
        uint8_t  start = ac->mAnimNodes[i].childrenStartIndex;
        uint8_t  count = ac->mAnimNodes[i].numChildren;
        animNodes[i] = ((u32)count << 16) | (u32)start;
    }

    for (int i = 0; i < MaxBonePoses * 2; i++)
        animChildIndices[i] = (u32)ac->mChildIndices[i];

    for (int i = 0; i < skin->numJoints; i++)
        animJoints[i] = (u32)skin->joints[i];

    const m44* inv = (const m44*)skin->inverseBindMatrices;
    MemCopy(invBindMatrices, inv, sizeof(m44) * skin->numJoints);
    return numFrames;
}

void InitBuffers()
{
    /* Create anim buffers */
    const Uint32 readRasterBit   = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    const Uint32 writeComputeBit = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const Uint32 readCompute     = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;

    g_RenderState.entityBuffer   = CreateBuffer(ecs.entities   , sizeof(ecs.entities)   , readRasterBit, "CPInstancePositions");
    // g_RenderState.boneBufferGPU  = CreateBuffer(outBoneMtx     , sizeof(outBoneMtx)  , readRasterBit | writeComputeBit, "CPJointMatricesGPU");
    g_RenderState.boneBuffer     = CreateBuffer(OutMatrices    , sizeof(OutMatrices)    , readRasterBit | writeComputeBit, "CPJointMatrices");
    g_RenderState.animPoseBuffer = CreateBuffer(animPoses      , sizeof(animPoses)      , readCompute, "CPAnimPoses");
    g_RenderState.animNodeBuffer = CreateBuffer(animNodes      , sizeof(animNodes)      , readCompute, "CPanimNodeBuffer");
    g_RenderState.jointsBuffer   = CreateBuffer(animJoints     , sizeof(animJoints)     , readCompute, "CPjointsBuffer ");
    g_RenderState.invBindBuffer  = CreateBuffer(invBindMatrices, sizeof(invBindMatrices), readCompute, "CPinvBindBuffer");
    g_RenderState.childIndicesBuffer = CreateBuffer(animChildIndices, sizeof(animChildIndices), readCompute, "CPchildIndicesBuffer");

    /* Create skined */
    g_RenderState.vertexBuffer = CreateBuffer(gGFX.VertexBuffer, MAX_VERTEX * sizeof(ASkinedVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPVertexBuffer");
    g_RenderState.indexBuffer  = CreateBuffer(gGFX.IndexBuffer , MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
}

static void InitAnimationComputePipeline()
{
    g_AnimComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_AnimationCompute_spv,
        .code_size                     = sizeof(Shaders_AnimationCompute_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 5,
        .num_readwrite_storage_buffers = 1,
        .threadcount_x                 = 32,
        .threadcount_y                 = 1,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_AnimComputePipeline, "Animation Compute Pipeline")
}

void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd)
{
    struct {
        float timeSinceStartup;
        float animDuration;
        float numFrames;
        int   rootNodeIndex;
        int   numInstances;
    } params;
    params.timeSinceStartup = 0.0f; // (float)TimeSinceStartup();
    params.animDuration     = AnimControllers[0].mPrefab->animations[AnimIDX].duration;
    params.numFrames        = params.animDuration * ANIM_NUM_FRAMES;
    params.rootNodeIndex    = AnimControllers[0].mRootNodeIndex;
    params.numInstances     = NUM_ANIMS;
    
    SDL_GPUStorageBufferReadWriteBinding rw_binding = {
        .buffer = g_RenderState.boneBuffer,
        .cycle  = true,
    };

    SDL_GPUBuffer* ro_buffers[5] = {
        g_RenderState.animPoseBuffer,
        g_RenderState.animNodeBuffer,
        g_RenderState.childIndicesBuffer,
        g_RenderState.jointsBuffer,
        g_RenderState.invBindBuffer
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &rw_binding, 1);
    SDL_BindGPUComputePipeline(pass, g_AnimComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, 5);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

    SDL_DispatchGPUCompute(pass, (NUM_ANIMS + 31) / 32, 1, 1);
    SDL_EndGPUComputePass(pass);
}

// this is going to be in main or something
s32 InitScene()
{
    gPaladin = (SceneBundle*)AllocateTLSFGlobal(sizeof(SceneBundle));
    
    if (!LoadGLTFCached("Assets/Meshes/Paladin/Paladin.gltf", gPaladin, g_RenderState.textures))
    {
        AX_ERROR("gltf scene load failed");
        return 0;
    }
    
    for (s32 i = 0; i < NUM_ANIMS; i++)
    {
        half3x4* outMatrices = OutMatrices + (i * MaxBonePoses);
        AnimationController_Create(gPaladin, &AnimControllers[i], outMatrices);
    }
    AnimationGetGPUData(&AnimControllers[0]);
    // UpdateAnimations();
    InitBuffers();
    return 1;
}

void UpdateAnimations()
{
    const double timeSinceStartup = 0.0; // TimeSinceStartup();
    v128f camPos = VecLoad(&g_Camera.position.x);
    s64 frameCount = PlatformCtx.FrameCount;
    int i = 0;
    // #pragma omp parallel for schedule(static) num_threads(8)
    // #pragma omp parallel for schedule(static) num_threads(omp_get_num_procs() / 2)
    for (i = 0; i < NUM_ANIMS; i++)
    {
        float distSqr = Vec3DistSqrfV(camPos, ecs.entities[i].position);
    
        const float MedAnimDistSqr = 40 * 40;
        const float FarAnimDistSqr = 120 * 120;
    
        // Determine the update frequency based on distance
        s32 updateRate = 1; // Sample every nth frame
        if (distSqr > FarAnimDistSqr) updateRate = 8;
        if (distSqr > MedAnimDistSqr) updateRate = 4;

        // Logic: Only update if (CurrentFrame + EntityIndex) % UpdateRate == 0
        // This staggers the updates so the CPU doesn't spike all at once.
        bool shouldUpdate = (timeSinceStartup < 1.0) || ((frameCount + i) % updateRate == 0);

        if (shouldUpdate)
        {
            AnimationController* ac = &AnimControllers[i];
            const s32 numAnims = ac->mPrefab->numAnimations;
            const s32 animIdx = AnimIDX; // Clampi32(WangHash(i + 645) % numAnims, 1, numAnims);

            const float animRatio = 0.0f;

            AnimationController_PlayAnim(ac, animIdx, animRatio);
        }
    }
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
    
    if (g_UseGPUComputeAnimation)
        DispatchAnimationCompute(cmd);
    /* Set up the bindings */
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    vertex_binding.buffer = g_RenderState.vertexBuffer;
    vertex_binding.offset = 0;
    index_binding.buffer  = g_RenderState.indexBuffer;
    index_binding.offset  = 0;
    // UpdateGPUBuffer(g_RenderState.boneBuffer, OutMatrices, sizeof(OutMatrices));
    // UpdateGPUBuffer(g_RenderState.boneBuffer, outBoneMtx, sizeof(outBoneMtx));
    UpdateGPUBuffer(g_RenderState.entityBuffer, ecs.entities, sizeof(ecs.entities));
    /* Draw */
    SDL_GPUTexture* tex = g_RenderState.textures[gPaladin->materials[0].baseColorTexture.index].handle;
    m44 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    FrustumPlanes frustumPlanes = CreateFrustumPlanes(viewProj);
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    SDL_GPUBuffer* buffers[2] = { g_RenderState.boneBuffer, g_RenderState.entityBuffer};
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
    
    SDL_BindGPUFragmentSamplers(pass, 0, 
                                &(SDL_GPUTextureSamplerBinding){
        .texture = tex, 
        .sampler = g_RenderState.sampler
    }, 1);
    
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(m44));
    
    s32 numNodes = gPaladin->numNodes;
    const bool hasScene = gPaladin->numScenes > 0;
    AScene defaultScene;
    if (hasScene)
    {
        defaultScene = gPaladin->scenes[gPaladin->defaultSceneIndex];
        numNodes = defaultScene.numNodes;
    }

    s32 stackLen = 0;
    s32 nodeStack[256];

    if (hasScene)
    {
        for (s32 i = 0; i < defaultScene.numNodes; i++)
            nodeStack[stackLen++] = defaultScene.nodes[i];
    }
    else
    {
        nodeStack[stackLen++] = 0; // No scene defined, traverse all top-level nodes
    }

    while (stackLen > 0)
    {
        const s32 nodeIndex = nodeStack[--stackLen];
        const ANode* node = gPaladin->nodes + nodeIndex;
        const AMesh* mesh = gPaladin->meshes + node->index;
    
        if (node->type == 0 && node->index != -1)
            for (s32 j = 0; j < mesh->numPrimitives; ++j)
            {
                const APrimitive* primitive = &mesh->primitives[j];
                // const bool hasMaterial = sceneBundle->materials && primitive->material != UINT16_MAX;
                // const AMaterial material = sceneBundle->materials[primitive->material];
                // const Matrix4 model = nodeTransforms[nodeIndex];
                SDL_DrawGPUIndexedPrimitives(pass, primitive->numIndices, NUM_ANIMS, primitive->indexOffset, 0, 0);
            }
    
            for (s32 i = 0; i < node->numChildren; i++)    
            {
                nodeStack[stackLen++] = node->children[i];
            }
    }
    
    SDL_EndGPURenderPass(pass);
    
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
        .num_storage_buffers = 2,
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
}

static void DestroyPipeline()
{
    if (g_RenderState.vertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.vertexBuffer);
    if (g_RenderState.pipeline)     SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.pipeline);
    
    SDL_zero(g_RenderState);
    g_GPUDevice = NULL;
}

void Quit(s32 rc)
{
    DestroyPipeline();
    GraphicsDestroy();
    exit(rc);
}
