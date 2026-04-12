
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

#include "Include/Common.h"
#include "Include/OS.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/FileSystem.h"
#include "Include/Bitset.h"
#include "Include/Algorithm.h"

#include "Include/ECS.h"
#include "Include/Camera.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/AssetManager.h"
#include "Include/BasisBinding.h"

#include "Math/Matrix.h"

#if defined(PLATFORM_APPLE)
#include "Shaders/msl/SkinnedFrag.msl.h"
#include "Shaders/msl/SkinnedVert.msl.h"
#define Shaders_SkinnedFrag_spv      Shaders_SkinnedFrag_msl
#define Shaders_SkinnedFrag_spv_size Shaders_SkinnedFrag_msl_size
#define Shaders_SkinnedVert_spv      Shaders_SkinnedVert_msl
#define Shaders_SkinnedVert_spv_size Shaders_SkinnedVert_msl_size
#elif defined(PLATFORM_WINDOWS)
// Shaders_SkinnedFrag_spv
#include "Shaders/SkinnedFrag.spv.h"
#include "Shaders/SkinnedVert.spv.h"
#endif

#define NUM_ANIMS (1024)

static Uint32 frames = 0;

SDL_Window*    g_SDLWindow;
SDL_GPUDevice* g_GPUDevice = NULL;
SceneBundle*   gPaladin;

WindowState g_WindowState;
RenderState g_RenderState;

m44* g_NodeTransforms;
Camera   g_Camera;

static s32 characterRootIndex;
static AnimationController AnimControllers[NUM_ANIMS];
AX_ALIGN(4) half3x4 OutMatrices[MaxBonePoses * NUM_ANIMS];

extern Graphics gGFX;
ECS ecs;

extern s32 ParseGLTF2(const char* path, SceneBundle* result, float scale);

static void DestroyPipeline()
{
    if (g_RenderState.vertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, g_RenderState.vertexBuffer);
    if (g_RenderState.pipeline)     SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.pipeline);
    
    SDL_zero(g_RenderState);
    g_GPUDevice = NULL;
}

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
void Quit(s32 rc)
{
    DestroyPipeline();
    rDestroy();
    exit(rc);
}

static void Render()
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
    
    /* Set up the bindings */
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    vertex_binding.buffer = g_RenderState.vertexBuffer;
    vertex_binding.offset = 0;
    index_binding.buffer  = g_RenderState.indexBuffer;
    index_binding.offset  = 0;
    
    UpdateGPUBuffer(g_RenderState.boneBuffer, OutMatrices, sizeof(OutMatrices));
    UpdateGPUBuffer(g_RenderState.entityBuffer, ecs.entities, sizeof(ecs.entities));
    
    /* Draw */
    SDL_GPUTexture* tex = g_RenderState.textures[gPaladin->materials[0].baseColorTexture.index].handle;
    m44 viewProj = M44Multiply(g_Camera.view, g_Camera.projection);
    FrustumPlanes frustumPlanes = CreateFrustumPlanes(viewProj);

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    SDL_BindGPUGraphicsPipeline(pass, g_RenderState.pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    SDL_GPUBuffer* buffers[2] = { g_RenderState.boneBuffer, g_RenderState.entityBuffer} ;
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
            // const M44 model = nodeTransforms[nodeIndex];
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
    
    /* Submit the command buffer! */
    SDL_SubmitGPUCommandBuffer(cmd);
}

static void InitScene()
{
    gPaladin = AllocateTLSFGlobal(sizeof(SceneBundle));
    
    // if (!LoadSceneBundleBinary("Assets/Meshes/Paladin/Paladin.abm", sceneBundle))
    // if (!ParseGLTF2("Assets/Meshes/Paladin/Paladin.gltf", sceneBundle, 1.0f))
    if (!ParseGLTF2("Assets/Meshes/Paladin2/Paladin.glb", gPaladin, 1.0f))
    {
        AX_ERROR("gltf scene load failed2");
        return;
    }
    
    CreateVerticesIndices(gPaladin);
    g_RenderState.vertexBuffer = CreateBuffer(gGFX.VertexBuffer, MAX_VERTEX * sizeof(ASkinedVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPVertexBuffer");
    g_RenderState.indexBuffer  = CreateBuffer(gGFX.IndexBuffer , MAX_INDEX * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    
    g_NodeTransforms   = AllocateTLSFGlobal(sizeof(m44) * gPaladin->numNodes);
    characterRootIndex = Prefab_FindAnimRootNodeIndex(gPaladin);
    
    for (s32 i = 0; i < NUM_ANIMS; i++)
    {
        half3x4* outMatrices = OutMatrices + (i * MaxBonePoses);
        AnimationController_Create(gPaladin, &AnimControllers[i], outMatrices);
    }
        
    g_RenderState.boneBuffer   = CreateBuffer(OutMatrices , sizeof(OutMatrices) , SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "CPJointMatrices");
    g_RenderState.entityBuffer = CreateBuffer(ecs.entities, sizeof(ecs.entities), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "CPInstancePositions");
    
    BasisuSetup();
    // SaveSceneImages(g_SceneBundle, "Assets/Meshes/Paladin2/Paladin.bdc");
    // "Assets/Meshes/Paladin/PaladinTest.bdc"
    s32 imgRes = LoadSceneImages("Assets/Meshes/Paladin2/Paladin.bdc", g_RenderState.textures, gPaladin->numImages);
    
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

static void InitPipeline()
{
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc;
    SDL_GPUColorTargetDescription     color_target_desc;
    SDL_GPUVertexAttribute            vertex_attributes[6];
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
    vertex_attributes[2].format      = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    vertex_attributes[2].location    = 2;
    vertex_attributes[2].offset      = vertex_attributes[1].offset + sizeof(int);
    
    vertex_attributes[3].buffer_slot = 0;
    vertex_attributes[3].format      = SDL_GPU_VERTEXELEMENTFORMAT_HALF2;
    vertex_attributes[3].location    = 3;
    vertex_attributes[3].offset      = vertex_attributes[2].offset + sizeof(int);
    
    vertex_attributes[4].buffer_slot = 0;
    vertex_attributes[4].format      = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
    vertex_attributes[4].location    = 4;
    vertex_attributes[4].offset      = vertex_attributes[3].offset + sizeof(int);
    
    vertex_attributes[5].buffer_slot = 0;
    vertex_attributes[5].format      = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    vertex_attributes[5].location    = 5;
    vertex_attributes[5].offset      = vertex_attributes[4].offset + sizeof(int);
    
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

static s32 done = 0;

void loop(void)
{
    SDL_Event event;
    s32 i;

    while (SDL_PollEvent(&event) && !done)
    {
        if (event.type == SDL_EVENT_QUIT)
            done = true;
        
        EventCallback(&event);
    }
    
    SetPressedAndReleasedKeys();
    PlatformUpdate();
    CameraUpdate(&g_Camera, PlatformCtx.DeltaTime);
    const double timeSinceStartup = TimeSinceStartup();
    v128f camPos = VecLoad(&g_Camera.position.x);
    s64 frameCount = PlatformCtx.FrameCount;
    // #pragma omp parallel for schedule(static) num_threads(8)
    #pragma omp parallel for schedule(static) num_threads(omp_get_num_procs() / 2)
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
            const s32 animIdx  = Clampi32(WangHash(i + 645) % numAnims, 1, numAnims);

            const double animDuration = (double)ac->mPrefab->animations[animIdx].duration;
            const float animRatio = (float)Fract((timeSinceStartup + (i * 0.1)) / animDuration);

            AnimationController_PlayAnim(ac, animIdx, animRatio);
        }
    }
    
    s64 now = TimeNow();
    static s64 lastUpdate = 0;
    s64 diff = TimeToMilliseconds(now - lastUpdate);

    if (diff >= 256) 
    {
        lastUpdate = now;
        s64 elapsedUS = TimeToMicroseconds(now - PlatformCtx.LastTime);
        static char usBuffer[128]; 
        IntToString(usBuffer, elapsedUS, 0); 
        SDL_SetWindowTitle(g_SDLWindow, usBuffer);
        // SDL_Log(usBuffer);
    }

    if (!done)
    {
    	Render();
    }
#ifdef __EMSCRIPTEN__
    else {
        emscripten_cancel_main_loop();
    }
#endif
    RecordLastKeys();
    PlatformCtx.FrameCount++;
}

s32 main(s32 argc, char* argv[])
{
    s32 msaa = 0;
    done = 0;
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
        return 0;
    
    SDL_Window* window = SDL_CreateWindow("SDL Minimal Sample", 1920, 1080, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    g_SDLWindow = window;
    
    if (!window)
        return 0;
    
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    
    ECS_Init(&ecs);

    rInit(msaa);
    InitPipeline(msaa);
    InitScene();

    PlatformInit();
    CameraInit(&g_Camera, 1920, 1080);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(loop, 0, 1);
#else
    while (!done) {
        loop();
    }
#endif

#if !defined(__ANDROID__)
    Quit(0);
#endif
    return 0;
}
