
#include <stdlib.h>

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

/* Regenerate the shaders with testgpu/build-shaders.sh */
#include "Shaders/SkinnedFrag.spv.h"
#include "Shaders/SkinnedVert.spv.h"

#define TESTGPU_SUPPORTED_FORMATS (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB)

#define CHECK_CREATE(var, thing) { if (!(var)) { SDL_Log("Failed to create %s: %s", thing, SDL_GetError()); quit(2); } }

static Uint32 frames = 0;

typedef struct RenderState
{
	SDL_GPUBuffer* buf_vertex;
	SDL_GPUBuffer* buf_index;
	SDL_GPUBuffer* buf_bones;
    SDL_GPUSampler* sampler;
    Texture textures[8];
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUSampleCount sample_count;
} RenderState;

typedef struct WindowState
{
	int angle_x, angle_y, angle_z;
	SDL_GPUTexture* tex_depth, * tex_msaa, * tex_resolve;
	Uint32 prev_drawablew, prev_drawableh;
} WindowState;

typedef struct
{
	int num_windows;
	SDL_Window* windows[4];
} CommonState;

static SDL_GPUDevice* gpu_device = NULL;
static RenderState render_state;
static WindowState* window_states = NULL;
static CommonState state;

Camera globalCamera;
SceneBundle* sceneBundle;

#define NUM_ANIMS (32)
Matrix4* nodeTransforms;
static int characterRootIndex;
static AnimationController animController[NUM_ANIMS];
AX_ALIGN(4) Matrix3x4f16 OutMatrices[MaxBonePoses * NUM_ANIMS];

static void shutdownGPU(void)
{
	if (window_states) 
    {
        for (int i = 0; i < state.num_windows; i++)
        {
			WindowState* winstate = &window_states[i];
			SDL_ReleaseGPUTexture(gpu_device, winstate->tex_depth);
			SDL_ReleaseGPUTexture(gpu_device, winstate->tex_msaa);
			SDL_ReleaseGPUTexture(gpu_device, winstate->tex_resolve);
			SDL_ReleaseWindowFromGPUDevice(gpu_device, state.windows[i]);
		}
		SDL_free(window_states);
		window_states = NULL;
	}

	SDL_ReleaseGPUBuffer(gpu_device, render_state.buf_vertex);
	SDL_ReleaseGPUGraphicsPipeline(gpu_device, render_state.pipeline);
	SDL_DestroyGPUDevice(gpu_device);

	SDL_zero(render_state);
	gpu_device = NULL;
}


/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	shutdownGPU();
	exit(rc);
}

static SDL_GPUTexture* CreateDepthTexture(Uint32 drawablew, Uint32 drawableh)
{
	SDL_GPUTextureCreateInfo createinfo;
	SDL_GPUTexture* result;

	createinfo.type = SDL_GPU_TEXTURETYPE_2D;
	createinfo.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	createinfo.width = drawablew;
	createinfo.height = drawableh;
	createinfo.layer_count_or_depth = 1;
	createinfo.num_levels = 1;
	createinfo.sample_count = render_state.sample_count;
	createinfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	createinfo.props = 0;

	result = SDL_CreateGPUTexture(gpu_device, &createinfo);
	CHECK_CREATE(result, "Depth Texture")
	return result;
}

static SDL_GPUTexture* CreateMSAATexture(Uint32 drawablew, Uint32 drawableh)
{
	SDL_GPUTextureCreateInfo createinfo;
	SDL_GPUTexture* result;

	if (render_state.sample_count == SDL_GPU_SAMPLECOUNT_1) {
		return NULL;
	}

	createinfo.type = SDL_GPU_TEXTURETYPE_2D;
	createinfo.format = SDL_GetGPUSwapchainTextureFormat(gpu_device, state.windows[0]);
	createinfo.width = drawablew;
	createinfo.height = drawableh;
	createinfo.layer_count_or_depth = 1;
	createinfo.num_levels = 1;
	createinfo.sample_count = render_state.sample_count;
	createinfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	createinfo.props = 0;

	result = SDL_CreateGPUTexture(gpu_device, &createinfo);
	CHECK_CREATE(result, "MSAA Texture")
	return result;
}

static SDL_GPUTexture* CreateResolveTexture(Uint32 drawablew, Uint32 drawableh)
{
	SDL_GPUTextureCreateInfo createinfo;
	SDL_GPUTexture* result;

	if (render_state.sample_count == SDL_GPU_SAMPLECOUNT_1) {
		return NULL;
	}

	createinfo.type = SDL_GPU_TEXTURETYPE_2D;
	createinfo.format = SDL_GetGPUSwapchainTextureFormat(gpu_device, state.windows[0]);
	createinfo.width = drawablew;
	createinfo.height = drawableh;
	createinfo.layer_count_or_depth = 1;
	createinfo.num_levels = 1;
	createinfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	createinfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	createinfo.props = 0;

	result = SDL_CreateGPUTexture(gpu_device, &createinfo);
	CHECK_CREATE(result, "Resolve Texture")
	return result;
}

static void UploadBoneBuffer()
{
    SDL_GPUTransferBufferCreateInfo transferBufferCreateInfo;
    transferBufferCreateInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferBufferCreateInfo.size  = sizeof(OutMatrices);
    transferBufferCreateInfo.props = 0;

    SDL_GPUTransferBuffer* boneTransferBuffer;
    SDL_GPUCopyPass* copyPass;
    Uint8* boneTransferPtr;
    SDL_GPUCommandBuffer* uploadCmdBuf;

    uploadCmdBuf = SDL_AcquireGPUCommandBuffer(gpu_device);
    copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
    
    boneTransferBuffer = SDL_CreateGPUTransferBuffer(gpu_device, &transferBufferCreateInfo);
    boneTransferPtr = (Uint8*)SDL_MapGPUTransferBuffer(gpu_device, boneTransferBuffer, true);
    MemCpy(boneTransferPtr, OutMatrices, sizeof(OutMatrices));
    SDL_UnmapGPUTransferBuffer(gpu_device, boneTransferBuffer);

    SDL_UploadToGPUBuffer(copyPass,
                          &(SDL_GPUTransferBufferLocation){ .transfer_buffer = boneTransferBuffer, .offset = 0}, 
                          &(SDL_GPUBufferRegion){.buffer=render_state.buf_bones, .offset=0, .size = sizeof(OutMatrices)}, true);
    
    
    SDL_EndGPUCopyPass(copyPass);
    
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(uploadCmdBuf);
    SDL_WaitForGPUFences(gpu_device, true, &fence, 1);
    SDL_ReleaseGPUFence(gpu_device, fence);
    
    SDL_ReleaseGPUTransferBuffer(gpu_device, boneTransferBuffer);
}

static void Render(SDL_Window* window, const int windownum)
{
	WindowState* winstate = &window_states[windownum];
	SDL_GPUTexture* swapchainTexture;
	SDL_GPUColorTargetInfo color_target;
	SDL_GPUDepthStencilTargetInfo depth_target;
	SDL_GPUCommandBuffer* cmd;
	SDL_GPURenderPass* pass;
	SDL_GPUBufferBinding vertex_binding;
	SDL_GPUBufferBinding index_binding;

	SDL_GPUBlitInfo blit_info;
	Uint32 drawablew, drawableh;

	/* Acquire the swapchain texture */
	cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
	if (!cmd) {
		SDL_Log("Failed to acquire command buffer :%s", SDL_GetError());
		quit(2);
	}
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, state.windows[windownum], &swapchainTexture, &drawablew, &drawableh)) {
		SDL_Log("Failed to acquire swapchain texture: %s", SDL_GetError());
		quit(2);
	}

	if (swapchainTexture == NULL) {
		/* Swapchain is unavailable, cancel work */
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}

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
    
    CameraUpdate(&globalCamera, 0.01f);

    UploadBoneBuffer();

	/* Set up the pass */
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

	SDL_zero(depth_target);
	depth_target.clear_depth = 1.0f;
	depth_target.load_op          = SDL_GPU_LOADOP_CLEAR;
	depth_target.store_op         = SDL_GPU_STOREOP_DONT_CARE;
	depth_target.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
	depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth_target.texture          = winstate->tex_depth;
	depth_target.cycle            = true;

	/* Set up the bindings */
	vertex_binding.buffer = render_state.buf_vertex;
	vertex_binding.offset = 0;
    index_binding.buffer = render_state.buf_index;
    index_binding.offset = 0;

	/* Draw */
    SDL_GPUTexture* tex = render_state.textures[sceneBundle->materials[0].baseColorTexture.index].handle;
    Matrix4 viewProj = Matrix4Multiply(globalCamera.view, globalCamera.projection);
	pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
	SDL_BindGPUGraphicsPipeline(pass, render_state.pipeline);
	SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &render_state.buf_bones, 1);
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
                SDL_DrawGPUIndexedPrimitives(pass, primitive->numIndices, 1, primitive->indexOffset, 0, 0);
            }
    
            for (int i = 0; i < node->numChildren; i++)
            {
                nodeStack[stackLen++] = node->children[i];
            }
    }
    
    SDL_EndGPURenderPass(pass);

	/* Blit MSAA resolve target to swapchain, if needed */
	if (render_state.sample_count > SDL_GPU_SAMPLECOUNT_1) {
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
	++frames;
}

SDL_GPUBuffer* CreateBuffer(void* buffer, size_t bufferSize, SDL_GPUBufferUsageFlags bufferUsage, const char* debugName)
{
	SDL_GPUBufferCreateInfo buffer_desc;
	SDL_GPUTransferBufferCreateInfo transfer_buffer_desc;
	SDL_GPUTransferBuffer* buf_transfer;
	SDL_GPUCopyPass* copy_pass;
	SDL_GPUTransferBufferLocation buf_location;
	SDL_GPUBufferRegion dst_region;
	SDL_GPUCommandBuffer* cmd;
    void* map;

    /* Create buffers */
	buffer_desc.usage = bufferUsage;
	buffer_desc.size = bufferSize;
	buffer_desc.props = SDL_CreateProperties();
	SDL_SetStringProperty(buffer_desc.props, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, debugName);
    SDL_GPUBuffer* gpu_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer_desc);

	CHECK_CREATE(gpu_buffer, "Static buffer")
    SDL_DestroyProperties(buffer_desc.props);

	transfer_buffer_desc.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_buffer_desc.size = bufferSize;
	transfer_buffer_desc.props = SDL_CreateProperties();
	SDL_SetStringProperty(transfer_buffer_desc.props, SDL_PROP_GPU_TRANSFERBUFFER_CREATE_NAME_STRING, "Transfer Buffer");
	buf_transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_buffer_desc);
	CHECK_CREATE(buf_transfer, "transfer buffer")
    SDL_DestroyProperties(transfer_buffer_desc.props);

	/* We just need to upload the static data once. */
	map = SDL_MapGPUTransferBuffer(gpu_device, buf_transfer, false);
	SDL_memcpy(map, buffer, bufferSize);
	SDL_UnmapGPUTransferBuffer(gpu_device, buf_transfer);

	cmd = SDL_AcquireGPUCommandBuffer(gpu_device);
	copy_pass = SDL_BeginGPUCopyPass(cmd);
	buf_location.transfer_buffer = buf_transfer;
	buf_location.offset = 0;
	dst_region.buffer = gpu_buffer;
	dst_region.offset = 0;
	dst_region.size = bufferSize;
	SDL_UploadToGPUBuffer(copy_pass, &buf_location, &dst_region, false);
	SDL_EndGPUCopyPass(copy_pass);
	SDL_SubmitGPUCommandBuffer(cmd);

	SDL_ReleaseGPUTransferBuffer(gpu_device, buf_transfer);
    return gpu_buffer;
}


static void InitScene()
{
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

static void init_render_state(int msaa)
{
	SDL_GPUGraphicsPipelineCreateInfo pipelinedesc;
	SDL_GPUColorTargetDescription color_target_desc;
	Uint32 drawablew, drawableh;
	SDL_GPUVertexAttribute vertex_attributes[6];
	SDL_GPUVertexBufferDescription vertex_buffer_desc;
	SDL_GPUShader* vertex_shader;
	SDL_GPUShader* fragment_shader;
	int i;

	gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
	CHECK_CREATE(gpu_device, "GPU device");

	/* Claim the windows */
	for (i = 0; i < state.num_windows; i++) {
		SDL_ClaimWindowForGPUDevice(gpu_device, state.windows[i]);
	}

	/* Create shaders */
    vertex_shader = SDL_CreateGPUShader(gpu_device, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(gpu_device),
        .code                = SkinnedVert_spv,
        .code_size           = sizeof(SkinnedVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 1,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX
    });

    fragment_shader = SDL_CreateGPUShader(gpu_device, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(gpu_device),
        .code                = SkinnedFrag_spv,
        .code_size           = sizeof(SkinnedFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT
    });

    CHECK_CREATE(vertex_shader  , "Vertex Shader")
    CHECK_CREATE(fragment_shader, "Fragment Shader")

    InitScene();

	/* Determine which sample count to use */
	render_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	if (msaa && SDL_GPUTextureSupportsSampleCount(
		gpu_device,
		SDL_GetGPUSwapchainTextureFormat(gpu_device, state.windows[0]),
		SDL_GPU_SAMPLECOUNT_4)) 
    {
		render_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
	}

	/* Set up the graphics pipeline */
	SDL_zero(pipelinedesc);
	SDL_zero(color_target_desc);

	color_target_desc.format = SDL_GetGPUSwapchainTextureFormat(gpu_device, state.windows[0]);

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
	vertex_attributes[1].format      = SDL_GPU_VERTEXELEMENTFORMAT_10BIT_SNORM;
	vertex_attributes[1].location    = 1;
	vertex_attributes[1].offset      = sizeof(float) * 3;

    vertex_attributes[2].buffer_slot = 0;
	vertex_attributes[2].format      = SDL_GPU_VERTEXELEMENTFORMAT_10BIT_SNORM;
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
	vertex_attributes[5].format      = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
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

	/* Set up per-window state */
	window_states = (WindowState*)SDL_calloc(state.num_windows, sizeof(WindowState));

	for (i = 0; i < state.num_windows; i++) 
    {
		WindowState* winstate = &window_states[i];
		/* create a depth texture for the window */
		SDL_GetWindowSizeInPixels(state.windows[i], (int*)&drawablew, (int*)&drawableh);
		winstate->tex_depth   = CreateDepthTexture(drawablew, drawableh);
		winstate->tex_msaa    = CreateMSAATexture(drawablew, drawableh);
		winstate->tex_resolve = CreateResolveTexture(drawablew, drawableh);
	}
}

static int done = 0;
Uint64 lastTime;

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

    Uint64 now = SDL_GetTicks();
    Uint64 ms_elapsed = now - lastTime;
    PlatformCtx.DeltaTime = ms_elapsed / 1000;

    const double timeSinceStartup = (double)(now - PlatformCtx.StartupTime) / 1000.0;

    for (int i = 0; i < 1; i++)
    {
        AnimationController* ac = &animController[i];

        const int numAnims = ac->mPrefab->numAnimations;
        const int animIdx  = Clamp32(WangHash(i + 645) % numAnims, 1, numAnims);

        const double animDuration = (double)ac->mPrefab->animations[animIdx].duration;
        const float animRatio = (float)Fract((timeSinceStartup + (i * 0.1)) / animDuration);

        AnimationController_PlayAnim(animController + i, 2, animRatio);
    }
    
	if (!done)
	{
		for (i = 0; i < state.num_windows; ++i)
		{
			Render(state.windows[i], i);
		}
	}
#ifdef __EMSCRIPTEN__
	else {
		emscripten_cancel_main_loop();
	}
#endif
}

int main(int argc, char* argv[])
{
	int msaa;
	int i;
	const SDL_DisplayMode* mode;

	/* Initialize params */
	msaa = 0;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
		return 0;

	SDL_Window* window = SDL_CreateWindow("SDL Minimal Sample", 1920, 1080, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	state.num_windows = 1;
	state.windows[0] = window;

	if (!window)
		return 0;

	mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(state.windows[0]));
	SDL_Log("Screen bpp: %d", SDL_BITSPERPIXEL(mode->format));
	init_render_state(msaa);

	/* Main render loop */
	frames = 0;
	PlatformCtx.StartupTime = SDL_GetTicks();
    lastTime = PlatformCtx.StartupTime;
	done = 0;

    PlatformInit();
    CameraInit(&globalCamera, 1920, 1080);

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(loop, 0, 1);
#else
	while (!done) {
		loop();
	}
#endif

	/* Print out some timing information */
	SDL_Log("%2.2f frames per second", ((double)frames * 1000) / (SDL_GetTicks() - PlatformCtx.StartupTime));
#if !defined(__ANDROID__)
	quit(0);
#endif
	return 0;
}

/* vi: set ts=4 sw=4 expandtab: */
