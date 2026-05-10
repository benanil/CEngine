
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
#include "Include/RenderSet.h"

#if defined(PLATFORM_APPLE)
#include "Shaders/msl/SkinnedFrag.msl.h"
#include "Shaders/msl/SkinnedVert.msl.h"
#include "Shaders/msl/AnimationCompute.msl.h"
#include "Shaders/msl/CullDrawArgsCompute.msl.h"
#include "Shaders/msl/LineDebugFrag.msl.h"
#include "Shaders/msl/LineDebugVert.msl.h"
#define Shaders_SkinnedFrag_spv      Shaders_SkinnedFrag_msl
#define Shaders_SkinnedFrag_spv_size Shaders_SkinnedFrag_msl_size
#define Shaders_SkinnedVert_spv      Shaders_SkinnedVert_msl
#define Shaders_SkinnedVert_spv_size Shaders_SkinnedVert_msl_size
#define Shaders_AnimationCompute_spv      Shaders_AnimationCompute_msl
#define Shaders_AnimationCompute_spv_size Shaders_AnimationCompute_msl_size
#elif defined(PLATFORM_WINDOWS)
// Shaders_SkinnedFrag_spv
#include "Shaders/spv/SkinnedFrag.spv.h"
#include "Shaders/spv/SkinnedVert.spv.h"
#include "Shaders/spv/SurfaceFrag.spv.h"
#include "Shaders/spv/SurfaceVert.spv.h"
#include "Shaders/spv/CullDrawArgsCompute.spv.h"
#include "Shaders/spv/AnimationCompute.spv.h"
#include "Shaders/spv/LineDebugVert.spv.h"
#include "Shaders/spv/LineDebugFrag.spv.h"
#endif

#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)


SceneBundle*        gPaladin;
SceneBundle*        gSponza;

WindowState         g_WindowState;
RenderState         g_RenderState;
SDL_GPUDevice*      g_GPUDevice = NULL;

static SDL_GPUComputePipeline* g_AnimComputePipeline = NULL;
static SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline = NULL;

extern SDL_Window*  g_SDLWindow; // main    
extern Camera       g_Camera; // main
extern Graphics     gGFX;
extern RenderSet    skinnedSet;
extern RenderSet    surfaceSet;

static void InitRenderSetBuffers(RenderSetBuffers* buffers, RenderSet* set)
{
    const Uint32 readRasterBit   = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    const Uint32 writeComputeBit = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const Uint32 readCompute     = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    const Uint32 indirectBit     = SDL_GPU_BUFFERUSAGE_INDIRECT;
    size_t groupBytes  = set->maxGroups   * sizeof(PrimitiveGroup);
    size_t entityBytes = set->maxEntities * sizeof(Entity);
    
    // indirect and culling
    buffers->primitiveGroup   = CreateBuffer(set->primitiveGroups, groupBytes, readCompute, "CPPrimitiveGroups");
    buffers->drawDenseIndices = CreateBuffer(NULL, set->maxEntities * sizeof(u32), readRasterBit | readCompute | writeComputeBit, "CPDrawDenseIndices");
    buffers->sparseToDense    = CreateBuffer(set->sparseID, set->maxEntities * sizeof(u32), readCompute, "CPSparseToDense");
    buffers->drawArgs         = CreateBuffer(NULL, set->maxGroups * sizeof(SDL_GPUIndexedIndirectDrawCommand), indirectBit | writeComputeBit, "CPDrawArgs");
    buffers->denseToPrimitive = CreateBuffer(set->denseToPrimitiveIndex, set->maxEntities * sizeof(u32), readCompute, "CPDenseToPrimitive");
    buffers->entity           = CreateBuffer(set->entities, entityBytes, readRasterBit | readCompute, "CPEntities");
    buffers->visibleDenseIndices = CreateBuffer(NULL, set->maxEntities * sizeof(u32), readCompute | writeComputeBit, "CPVisibleDenseIndices");
    buffers->visibilityMask      = CreateBuffer(NULL, set->maxEntities * sizeof(u32), readCompute | writeComputeBit, "CPVisibilityMask");
    buffers->visibleCount        = CreateBuffer(NULL, sizeof(u32), readCompute | writeComputeBit, "CPVisibleCount");
    buffers->dispatchArgs        = CreateBuffer(NULL, sizeof(u32) * 3, indirectBit | writeComputeBit, "CPDispatchArgs");
}

void InitBuffers()
{
    InitRenderSetBuffers(&g_RenderState.skinnedBuffers, &skinnedSet);
    InitRenderSetBuffers(&g_RenderState.surfaceBuffers, &surfaceSet);

    AnimInitBuffers();
    // TODO: buffers per render set
    g_RenderState.skinnedVertexBuffer = CreateBuffer(gGFX.SkinnedVertexBuffer, MAX_VERTEX * sizeof(ASkinedVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPSkinnedVertexBuffer");
    g_RenderState.surfaceVertexBuffer = CreateBuffer(gGFX.SurfaceVertexBuffer, MAX_VERTEX * sizeof(AVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPSurfaceVertexBuffer");
    g_RenderState.indexBuffer         = CreateBuffer(gGFX.IndexBuffer, MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    
    g_RenderState.lineBuffer = CreateBuffer(NULL, sizeof(ALineVertex) * MAX_LINE_COUNT, SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, "CPLineVertexBuffer"); 
    g_RenderState.lineDrawArgsBuffer = CreateBuffer(NULL, sizeof(u32) * 8, SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE, "CPLinedrawArgsBuffer");
}

static void InitAnimationComputePipeline()
{
    g_AnimComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_AnimationCompute_spv,
        .code_size                     = sizeof(Shaders_AnimationCompute_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 7,
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
        .num_readonly_storage_buffers  = 3,
        .num_readwrite_storage_buffers = 8,
        .threadcount_x                 = 64,
        .threadcount_y                 = 1,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_CullDrawArgsComputePipeline, "Cull Draw Args Compute Pipeline")
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

static void InitSkinedPipeline();
static void InitSurfacePipeline();
static void InitLinePipeline();

void RendererInit()
{
    InitSamplers();
    InitSkinedPipeline();
    InitSurfacePipeline();
    InitLinePipeline();
    InitAnimationComputePipeline();
    InitCullDrawArgsComputePipeline();
}

static void DispatchCullDrawArgsCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet,
                                        RenderSetBuffers* buffers,
                                        FrustumPlanes frustumPlanes,
                                        bool enableVisibilityOutput)
{
    if (renderSet->numGroups == 0) return;
    CHECK_CREATE(g_CullDrawArgsComputePipeline, "Cull Draw Args Compute Pipeline")

    struct {
        FrustumPlanes planes;
        u32 numEntities;
        u32 numPrimitiveGroups;
        u32 mode;
        u32 enableVisibilityOutput;
    } params;
    MemCopy(&params.planes, frustumPlanes.planes, sizeof(FrustumPlanes));
    params.numEntities = renderSet->numEntities;
    params.numPrimitiveGroups = renderSet->numGroups;
    params.mode = 0;
    params.enableVisibilityOutput = enableVisibilityOutput ? 1u : 0u;

    SDL_GPUBuffer* ro_buffers[3] = {
        buffers->entity, 
        buffers->primitiveGroup, 
        buffers->denseToPrimitive
    };
    SDL_GPUStorageBufferReadWriteBinding rw_bindings[8] = {
        { buffers->drawDenseIndices },
        { buffers->drawArgs },
        { g_RenderState.lineBuffer },
        { g_RenderState.lineDrawArgsBuffer },
        { buffers->visibleDenseIndices },
        { buffers->visibilityMask },
        { buffers->visibleCount },
        { buffers->dispatchArgs }
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_CullDrawArgsComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    // clear draw args and numVisibleInPrimitive
    u32 resetCount = renderSet->numGroups;
    if (enableVisibilityOutput && renderSet->numEntities > resetCount) resetCount = renderSet->numEntities;
    SDL_DispatchGPUCompute(pass, (resetCount + 63) / 64, 1, 1);

    if (renderSet->numEntities > 0)
    {
        params.mode = 1;
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        // actual culling
        SDL_DispatchGPUCompute(pass, (renderSet->numEntities + 63) / 64, 1, 1);
    }
    SDL_EndGPUComputePass(pass);
}

void DispatchAnimationCompute(SDL_GPUCommandBuffer* cmd, RenderSet* renderSet)
{
    if (renderSet->numEntities == 0) return;
    CHECK_CREATE(g_AnimComputePipeline, "Animation Compute Pipeline")

    struct {
        float timeSinceStartup;
        int   numInstances;
    } params;
    params.timeSinceStartup = (float)TimeSinceStartup();
    params.numInstances     = (int)renderSet->numEntities;
    
    SDL_GPUStorageBufferReadWriteBinding rw_bindings[1] = {
        { g_RenderState.boneBuffer }
    };

    SDL_GPUBuffer* ro_buffers[7] = {
        g_RenderState.animPoseBuffer,
        g_RenderState.animHierarchyBuffer,
        g_RenderState.animDataBuffer,
        g_RenderState.jointsBuffer,
        g_RenderState.invBindBuffer,
        g_RenderState.animInstanceBuffer,
        g_RenderState.skinnedBuffers.visibleDenseIndices
    };

    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, NULL, 0, rw_bindings, SDL_arraysize(rw_bindings));
    SDL_BindGPUComputePipeline(pass, g_AnimComputePipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, ro_buffers, SDL_arraysize(ro_buffers));
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUComputeIndirect(pass, g_RenderState.skinnedBuffers.dispatchArgs, 0);
 
    SDL_EndGPUComputePass(pass);
}

static void RenderScene(SDL_GPUCommandBuffer* cmd, 
                        SDL_GPUColorTargetInfo* color_target, 
                        SDL_GPUDepthStencilTargetInfo* depth_target,
                        mat4x4 viewProj)
{
    SDL_GPUTexture* tex = g_RenderState.textures[gPaladin->materials[0].baseColorTexture.index].handle;

    /* Set up the bindings */
    SDL_GPUBufferBinding vertex_binding = { g_RenderState.skinnedVertexBuffer, 0 };
    SDL_GPUBufferBinding index_binding  = { g_RenderState.indexBuffer , 0 };

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, color_target, 1, depth_target);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.skinnedPipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    SDL_GPUBuffer* buffers[4] = {
        g_RenderState.boneBuffer,
        g_RenderState.skinnedBuffers.entity,
        g_RenderState.skinnedBuffers.primitiveGroup,
        g_RenderState.skinnedBuffers.drawDenseIndices
    };
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
    
    SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
        .texture = tex, 
        .sampler = g_RenderState.sampler
    }, 1);
    
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
    SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.skinnedBuffers.drawArgs, 0, skinnedSet.numGroups);

    if (surfaceSet.numGroups > 0)
    {
        tex = g_RenderState.textures[15].handle;

        SDL_BindGPUGraphicsPipeline(pass, g_RenderState.surfacePipeline);
        vertex_binding.buffer = g_RenderState.surfaceVertexBuffer;
        vertex_binding.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUBuffer* surfaceBuffers[3] = {
            g_RenderState.surfaceBuffers.entity,
            g_RenderState.surfaceBuffers.primitiveGroup,
            g_RenderState.surfaceBuffers.drawDenseIndices
        };
        SDL_BindGPUVertexStorageBuffers(pass, 0, surfaceBuffers, SDL_arraysize(surfaceBuffers));
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){
            .texture = tex,
            .sampler = g_RenderState.sampler
        }, 1);
        SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
        SDL_DrawGPUIndexedPrimitivesIndirect(pass, g_RenderState.surfaceBuffers.drawArgs, 0, surfaceSet.numGroups);
    }
    
    // debug lines
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.linePipeline);
    vertex_binding.buffer = g_RenderState.lineBuffer;
    vertex_binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(viewProj));
    SDL_DrawGPUPrimitivesIndirect(pass, g_RenderState.lineDrawArgsBuffer, sizeof(int) * 4, 1);

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
        color_target.load_op               = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op              = SDL_GPU_STOREOP_RESOLVE;
        color_target.texture               = winstate->tex_msaa;
        color_target.resolve_texture       = winstate->tex_resolve;
        color_target.cycle                 = true;
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
    
    if (skinnedSet.numEntities > 0)
    {
        UpdateGPUBuffer(g_RenderState.skinnedBuffers.entity, skinnedSet.entities, skinnedSet.numEntities * sizeof(Entity), 0ull);
        UpdateGPUBuffer(g_RenderState.skinnedBuffers.sparseToDense, skinnedSet.sparseID, skinnedSet.numEntities * sizeof(u32), 0ull);
    }
    if (surfaceSet.numEntities > 0)
    {
        UpdateGPUBuffer(g_RenderState.surfaceBuffers.entity, surfaceSet.entities, surfaceSet.numEntities * sizeof(Entity), 0ull);
        UpdateGPUBuffer(g_RenderState.surfaceBuffers.sparseToDense, surfaceSet.sparseID, surfaceSet.numEntities * sizeof(u32), 0ull);
    }
    
    mat4x4 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    FrustumPlanes frustumPlanes = CreateFrustumPlanes(viewProj);
    DispatchCullDrawArgsCompute(cmd, &skinnedSet, &g_RenderState.skinnedBuffers, frustumPlanes, true);
    DispatchAnimationCompute(cmd, &skinnedSet);
    DispatchCullDrawArgsCompute(cmd, &surfaceSet, &g_RenderState.surfaceBuffers, frustumPlanes, false);
    RenderScene(cmd, &color_target, &depth_target, viewProj);
    
    /* Blit MSAA resolve target to swapchain, if needed */
    if (g_RenderState.sample_count > SDL_GPU_SAMPLECOUNT_1) 
    {
        SDL_GPUBlitInfo blit_info;
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

#define VFORMAT_FLOAT3 SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3
#define VFORMAT_UINT   SDL_GPU_VERTEXELEMENTFORMAT_UINT  
#define VFORMAT_HALF2  SDL_GPU_VERTEXELEMENTFORMAT_HALF2 
#define VFORMAT_HALF4  SDL_GPU_VERTEXELEMENTFORMAT_HALF4 
#define VFORMAT_UBYTE4 SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4

static void InitLinePipeline()
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_LineDebugVert_spv,
        .code_size           = sizeof(Shaders_LineDebugVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    }); CHECK_CREATE(vertex_shader  , "Vertex Shader")

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_LineDebugFrag_spv,
        .code_size           = sizeof(Shaders_LineDebugFrag_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    }); CHECK_CREATE(fragment_shader, "Fragment Shader")

    const SDL_GPUVertexAttribute vertex_attributes[2] = {
        { .location = 0, .buffer_slot = 0, .format = VFORMAT_FLOAT3, .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_UINT  , .offset = sizeof(f32) * 3 }
    };

    SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo)
        {
            .num_color_targets          = 1,
            .color_target_descriptions  = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM,
            .has_depth_stencil_target   = true
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = g_RenderState.sample_count },
        .depth_stencil_state = (SDL_GPUDepthStencilState)
        {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){
                0, sizeof(ALineVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 
            },
            .num_vertex_buffers    = 1, // above array count
            .vertex_attributes     = vertex_attributes,
            .num_vertex_attributes = ARRAY_SIZE(vertex_attributes)
        }
    };

    g_RenderState.linePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.linePipeline, "Render Pipeline")
    
    /* These are reference-counted; once the pipeline is created, you don't need to keep these. */
    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitSkinedPipeline()
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedVert_spv,
        .code_size           = sizeof(Shaders_SkinnedVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 4,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedFrag_spv,
        .code_size           = sizeof(Shaders_SkinnedFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });

    CHECK_CREATE(vertex_shader  , "Vertex Shader")
    CHECK_CREATE(fragment_shader, "Fragment Shader")

    const SDL_GPUVertexAttribute vertex_attributes[5] = {
        { .location = 0, .buffer_slot = 0, .format = VFORMAT_HALF4 , .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_UINT  , .offset = offsetof(ASkinedVertex, octTbn)   },
        { .location = 2, .buffer_slot = 0, .format = VFORMAT_HALF2 , .offset = offsetof(ASkinedVertex, texCoord) },
        { .location = 3, .buffer_slot = 0, .format = VFORMAT_UBYTE4, .offset = offsetof(ASkinedVertex, joints)   },
        { .location = 4, .buffer_slot = 0, .format = VFORMAT_UINT  , .offset = offsetof(ASkinedVertex, weights)  }
    };

    SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo)
        {
            .num_color_targets          = 1,
            .color_target_descriptions  = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM,
            .has_depth_stencil_target   = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState)
        {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = g_RenderState.sample_count },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){
                0, sizeof(ASkinedVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 
            },
            .num_vertex_buffers    = 1,
            .vertex_attributes     = vertex_attributes,
            .num_vertex_attributes = ARRAY_SIZE(vertex_attributes)
        }
    };

    g_RenderState.skinnedPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.skinnedPipeline, "Render Pipeline")
    
    /* These are reference-counted; once the pipeline is created, you don't need to keep these. */
    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitSurfacePipeline()
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceVert_spv,
        .code_size           = sizeof(Shaders_SurfaceVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 3,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceFrag_spv,
        .code_size           = sizeof(Shaders_SurfaceFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });

    CHECK_CREATE(vertex_shader, "Surface Vertex Shader")
    CHECK_CREATE(fragment_shader, "Surface Fragment Shader")

    const SDL_GPUVertexAttribute vertex_attributes[3] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(AVertex, position) },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,   .offset = offsetof(AVertex, octTbn) },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_HALF2,  .offset = offsetof(AVertex, texCoord) }
    };

    SDL_GPUTextureFormat colorFormat = SDL_GetGPUSwapchainTextureFormat(g_GPUDevice, g_SDLWindow);
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo)
        {
            .num_color_targets          = 1,
            .color_target_descriptions  = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format       = SDL_GPU_TEXTUREFORMAT_D24_UNORM,
            .has_depth_stencil_target   = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState)
        {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = g_RenderState.sample_count },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){
                0, sizeof(AVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0
            },
            .num_vertex_buffers    = 1,
            .vertex_attributes     = vertex_attributes,
            .num_vertex_attributes = ARRAY_SIZE(vertex_attributes)
        }
    };

    g_RenderState.surfacePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.surfacePipeline, "Surface Render Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void DestroyRenderSetBuffers(RenderSetBuffers* buffers)
{
    if (buffers->entity)           SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->entity);
    if (buffers->primitiveGroup)   SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->primitiveGroup);
    if (buffers->drawDenseIndices) SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->drawDenseIndices);
    if (buffers->drawArgs)         SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->drawArgs);
    if (buffers->denseToPrimitive) SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->denseToPrimitive);
    if (buffers->sparseToDense)    SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->sparseToDense);
    if (buffers->visibleDenseIndices) SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibleDenseIndices);
    if (buffers->visibilityMask)      SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibilityMask);
    if (buffers->visibleCount)        SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->visibleCount);
    if (buffers->dispatchArgs)        SDL_ReleaseGPUBuffer(g_GPUDevice, buffers->dispatchArgs);
}

static void DestroyPipeline()
{
    DestroyRenderSetBuffers(&g_RenderState.skinnedBuffers);
    DestroyRenderSetBuffers(&g_RenderState.surfaceBuffers);
    if (g_RenderState.skinnedVertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.skinnedVertexBuffer);
    if (g_RenderState.surfaceVertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.surfaceVertexBuffer);
    if (g_RenderState.indexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.indexBuffer);
    if (g_RenderState.skinnedPipeline)     SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedPipeline);
    if (g_RenderState.surfacePipeline)     SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfacePipeline);
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
