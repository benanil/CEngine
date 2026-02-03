
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_test_common.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_main.h>

#include "Include/Common.h"
#include "Include/OS.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/ECS.h"

#include "Include/FileSystem.h"

#include "Include/Camera.h"
#include "Include/Bitset.h"
#include "Include/Platform.h"
#include "Include/Graphics.h"
#include "Include/GLTFParser.h"
#include "Include/Animation.h"
#include "Include/AssetManager.h"
#include "Include/Algorithm.h"
#include "Include/BasisBinding.h"

#include "Math/Matrix.h"

#if defined(PLATFORM_APPLE)
#include "Shaders/msl/SkinnedFrag.msl.h"
#include "Shaders/msl/SkinnedVert.msl.h"
#define Shaders_SkinnedFrag_spv Shaders_SkinnedFrag_msl
#define Shaders_SkinnedFrag_spv_size Shaders_SkinnedFrag_msl_size
#define Shaders_SkinnedVert_spv Shaders_SkinnedVert_msl
#define Shaders_SkinnedVert_spv_size Shaders_SkinnedVert_msl_size
#elif defined(PLATFORM_WINDOWS)
// Shaders_SkinnedFrag_spv
#include "Shaders/SkinnedFrag.spv.h"
#include "Shaders/SkinnedVert.spv.h"
#endif


#define NUM_ANIMS (32)

static Uint32 frames = 0;

SDL_Window* sdlWindow;
SDL_GPUDevice* gpu_device = NULL;
WindowState window_state;

RenderState render_state;

Camera globalCamera;
SceneBundle* sceneBundle;
SceneBundle* foxScene;
Matrix4* nodeTransforms;

static int characterRootIndex;
static AnimationController animController[NUM_ANIMS];
AX_ALIGN(4) Matrix3x4f16 OutMatrices[MaxBonePoses * NUM_ANIMS];

ECS ecs;

static void DestroyPipeline()
{
    if (render_state.buf_vertex) SDL_ReleaseGPUBuffer(gpu_device, render_state.buf_vertex);
    if (render_state.pipeline)   SDL_ReleaseGPUGraphicsPipeline(gpu_device, render_state.pipeline);
    
    SDL_zero(render_state);
    gpu_device = NULL;
}

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
void Quit(int rc)
{
    DestroyPipeline();
    rDestroy();
    exit(rc);
}

static void Render()
{
    /* Acquire the swapchain texture */
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (!cmd) {
        SDL_Log("Failed to acquire command buffer :%s", SDL_GetError());
        Quit(2);
    }
    
    SDL_GPUTexture* swapchainTexture;
    Uint32 drawablew, drawableh;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, sdlWindow, &swapchainTexture, &drawablew, &drawableh)) {
        SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
        Quit(2);
    }
    
    if (swapchainTexture == NULL) {
        /* Swapchain is unavailable, cancel work */
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    
    WindowState* winstate = &window_state;
    /* Resize the depth buffer if the window size changed */
    if (winstate->prev_drawablew != drawablew || winstate->prev_drawableh != drawableh) {
        SDL_ReleaseGPUTexture(gpu_device, winstate->tex_depth);
        SDL_ReleaseGPUTexture(gpu_device, winstate->tex_msaa);
        SDL_ReleaseGPUTexture(gpu_device, winstate->tex_resolve);
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
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;
        color_target.texture = swapchainTexture;
    }
    
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_zero(depth_target);
    depth_target.clear_depth = 1.0f;
    depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op         = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.texture          = winstate->tex_depth;
    depth_target.cycle            = true;
    
    /* Set up the bindings */
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    vertex_binding.buffer = render_state.buf_vertex;
    vertex_binding.offset = 0;
    index_binding.buffer = render_state.buf_index;
    index_binding.offset = 0;
    
    UpdateGPUBuffer(render_state.buf_bones, OutMatrices, sizeof(OutMatrices));
    UpdateGPUBuffer(render_state.buf_positions, ecs.EntityPositions, sizeof(ecs.EntityPositions));
    UpdateGPUBuffer(render_state.buf_rotations, ecs.EntityRotations, sizeof(ecs.EntityRotations));
    
    /* Draw */
    SDL_GPUTexture* tex = render_state.textures[sceneBundle->materials[0].baseColorTexture.index].handle;
    Matrix4 viewProj = Matrix4Multiply(globalCamera.view, globalCamera.projection);
    
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    SDL_BindGPUGraphicsPipeline(pass, render_state.pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    
    SDL_GPUBuffer* buffers[3] = {render_state.buf_bones, render_state.buf_positions, render_state.buf_rotations };
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, SDL_arraysize(buffers));
    
    SDL_BindGPUFragmentSamplers(pass, 0,
                                &(SDL_GPUTextureSamplerBinding){
                                    .texture = tex, 
                                    .sampler = render_state.sampler
                                }, 1);
    
    SDL_PushGPUVertexUniformData(cmd, 0, &viewProj, sizeof(Matrix4));
    
    int numNodes  = sceneBundle->numNodes;
    const bool hasScene = sceneBundle->numScenes > 0;
    AScene defaultScene;
    if (hasScene)
    {
        defaultScene = sceneBundle->scenes[sceneBundle->defaultSceneIndex];
        numNodes = defaultScene.numNodes;
    }
    
    int stackLen = 1;
    int nodeStack[256];
    nodeStack[0] = hasScene ? defaultScene.nodes[0] : 0;
    
    while (stackLen > 0)
    {
        const int nodeIndex = nodeStack[--stackLen];
        const ANode* node = &sceneBundle->nodes[nodeIndex];
        const AMesh* mesh = sceneBundle->meshes + node->index;
    
        if (node->type == 0 && node->index != -1)
            for (int j = 0; j < mesh->numPrimitives; ++j)
            {
                const APrimitive* primitive = &mesh->primitives[j];
                // const bool hasMaterial = sceneBundle->materials && primitive->material != UINT16_MAX;
                // const AMaterial material = sceneBundle->materials[primitive->material];
                // const Matrix4 model = nodeTransforms[nodeIndex];
                SDL_DrawGPUIndexedPrimitives(pass, primitive->numIndices, NUM_ANIMS, primitive->indexOffset, 0, 0);
            }
    
            for (int i = 0; i < node->numChildren; i++)
            {
                nodeStack[stackLen++] = node->children[i];
            }
    }
    
    SDL_EndGPURenderPass(pass);
    
    /* Blit MSAA resolve target to swapchain, if needed */
    if (render_state.sample_count > SDL_GPU_SAMPLECOUNT_1) 
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
    // foxScene = AllocateTLSFGlobal(sizeof(SceneBundle));
    // // if (!LoadSceneBundleBinary("Assets/Meshes/Paladin/Paladin.abm", sceneBundle))
    // if (!ParseGLTF("Assets/Meshes/Fox.glb", foxScene, 1.0f))
    // {
    //     AX_ERROR("glb scene load failed2");
    //     return;
    // }

    sceneBundle = AllocateTLSFGlobal(sizeof(SceneBundle));
    // if (!LoadSceneBundleBinary("Assets/Meshes/Paladin/Paladin.abm", sceneBundle))
    if (!ParseGLTF("Assets/Meshes/Paladin/Paladin.gltf", sceneBundle, 1.0f))
    {
        AX_ERROR("gltf scene load failed2");
        return;
    }
    
    CreateVerticesIndicesSkined(sceneBundle);
    render_state.buf_vertex = CreateBuffer(sceneBundle->allVertices, sceneBundle->totalVertices * sizeof(ASkinedVertex), SDL_GPU_BUFFERUSAGE_VERTEX, "CPVertexBuffer");
    render_state.buf_index  = CreateBuffer(sceneBundle->allIndices, sceneBundle->totalIndices * sizeof(int), SDL_GPU_BUFFERUSAGE_INDEX, "CPIndexBuffer");
    
    nodeTransforms     = AllocateTLSFGlobal(sizeof(Matrix4) * sceneBundle->numNodes);
    characterRootIndex = Prefab_FindAnimRootNodeIndex(sceneBundle);
    
    for (int i = 0; i < NUM_ANIMS; i++)
    {
        Matrix3x4f16* outMatrices = OutMatrices + (i * MaxBonePoses);
        AnimationController_Create(sceneBundle, &animController[i], outMatrices);
    }
        
    render_state.buf_bones = CreateBuffer(OutMatrices, sizeof(OutMatrices), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "CPJointMatrices");
    render_state.buf_positions = CreateBuffer(ecs.EntityPositions, sizeof(ecs.EntityPositions), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "CPInstancePositions");
    render_state.buf_rotations = CreateBuffer(ecs.EntityRotations, sizeof(ecs.EntityRotations), SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, "CPInstanceRotations");
    
    BasisuSetup();
    int imgRes = LoadSceneImages("Assets/Meshes/Paladin/PaladinTest.bdc", render_state.textures, sceneBundle->numImages, gpu_device);
    
    render_state.sampler = SDL_CreateGPUSampler(gpu_device, &(SDL_GPUSamplerCreateInfo){
        .min_filter      = SDL_GPU_FILTER_LINEAR,
        .mag_filter      = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode     = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .min_lod = 0.0f,
        .max_lod = 8.0f
    });
}

static void InitPipeline()
{
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc;
    SDL_GPUColorTargetDescription color_target_desc;
    SDL_GPUVertexAttribute vertex_attributes[6];
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    
    /* Create shaders */
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(gpu_device, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(gpu_device),
        .code                = Shaders_SkinnedVert_spv,
        .code_size           = sizeof(Shaders_SkinnedVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 3,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX
    });
    
    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(gpu_device, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(gpu_device),
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
    
    color_target_desc.format = SDL_GetGPUSwapchainTextureFormat(gpu_device, sdlWindow);
    
    pipelinedesc.target_info.num_color_targets = 1;
    pipelinedesc.target_info.color_target_descriptions = &color_target_desc;
    pipelinedesc.target_info.depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    pipelinedesc.target_info.has_depth_stencil_target  = true;
    
    pipelinedesc.depth_stencil_state.enable_depth_test  = true;
    pipelinedesc.depth_stencil_state.enable_depth_write = true;
    pipelinedesc.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    
    pipelinedesc.multisample_state.sample_count = render_state.sample_count;
    
    pipelinedesc.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    
    pipelinedesc.vertex_shader   = vertex_shader;
    pipelinedesc.fragment_shader = fragment_shader;
    
    vertex_buffer_desc.slot               = 0;
    vertex_buffer_desc.input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_desc.instance_step_rate = 0;
    vertex_buffer_desc.pitch              = sizeof(ASkinedVertex);
    
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format      = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].location    = 0;
    vertex_attributes[0].offset      = 0;
    
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format      = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    vertex_attributes[1].location    = 1;
    vertex_attributes[1].offset      = sizeof(float) * 3;
    
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
    
    render_state.pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &pipelinedesc);
    CHECK_CREATE(render_state.pipeline, "Render Pipeline")
    
    /* These are reference-counted; once the pipeline is created, you don't need to keep these. */
    SDL_ReleaseGPUShader(gpu_device, vertex_shader);
    SDL_ReleaseGPUShader(gpu_device, fragment_shader);
}

static int done = 0;

void loop(void)
{
    SDL_Event event;
    int i;
    
    /* Check for events */
    while (SDL_PollEvent(&event) && !done)
    {
        if (event.type == SDL_EVENT_QUIT)
            done = true;
        
        EventCallback(&event);
    }
    
    PlatformUpdate();
    CameraUpdate(&globalCamera, 0.01f);
    
    const double timeSinceStartup = TimeSinceStartup();
    
    #pragma omp parallel for schedule(static)
    for (i = 0; i < NUM_ANIMS; i++)
    {
        AnimationController* ac = &animController[i];
    
        const int numAnims = ac->mPrefab->numAnimations;
        const int animIdx  = Clampi32(WangHash(i + 645) % numAnims, 1, numAnims);
    
        const double animDuration = (double)ac->mPrefab->animations[animIdx].duration;
        const float animRatio = (float)Fract((timeSinceStartup + (i * 0.1)) / animDuration);
    
        AnimationController_PlayAnim(animController + i, animIdx, animRatio);
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
}

int main(int argc, char* argv[])
{
    int msaa = 0;
    done = 0;
    
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
        return 0;
    
    SDL_Window* window = SDL_CreateWindow("SDL Minimal Sample", 1920, 1080, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    sdlWindow = window;
    
    if (!window)
        return 0;
    
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    
    ECS_Init(ecs.EntityPositions, ecs.EntityRotations);

    rInit(msaa);
    InitPipeline(msaa);
    InitScene();

    PlatformInit();
    CameraInit(&globalCamera, 1920, 1080);

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
