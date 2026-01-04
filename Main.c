#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL

#define SOKOL_GFX_IMPL
#define SOKOL_D3D11
#define SOKOL_ASSERT(x) ASSERT(x)

#define AX_ENABLE_LOGGING

#include "Include/Common.h"
#include "Include/OS.h"
#include "Include/Memory.h"
#include "Include/Random.h"
#include "Include/ECS.h"

#include "Include/FileSystem.h"

#include "Extern/sokol/sokol_gfx.h"
#include "Extern/sokol/sokol_app.h"
#include "Extern/sokol/sokol_log.h"
#include "Extern/sokol/sokol_glue.h"
#include "Extern/sokol/sokol_time.h"
#undef SOKOL_GFX_IMPL

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

#include "Shaders/Cube.glsl.h"

#define NUM_ANIMS (1024)

static struct {
    float rx, ry;
    sg_pipeline pip;
    sg_bindings bind;
} state;

typedef struct {
    float x, y, z;
    uint32_t color;
    int16_t u, v;
} vertex_t;

Camera globalCamera;
Matrix4* nodeTransforms;
static SceneBundle* sceneBundle;
static int characterRootIndex;
static AnimationController animController[NUM_ANIMS];

AX_ALIGN(4) Matrix3x4f16 OutMatrices[MaxBonePoses * NUM_ANIMS];

static void _sapp_setup_wave_icon(void);

static const int ScreenStartWidth = 1920, ScreenStartHeight = 1080;

ECS ecs;

void Init(void)
{
    BasisuSetup();
    _sapp_setup_wave_icon();
    stm_setup();
    PlatformInit();
    rInit();
    
    ECS_Init(ecs.EntityPositions, ecs.EntityRotations);

    PlatformCtx.StartupTime = stm_now();
    CameraInit(&globalCamera, ScreenStartWidth, ScreenStartHeight);

    sg_setup(&(sg_desc) {
        .environment = sglue_environment(),
        .logger.func = slog_func,
        .image_pool_size = 2048
    });

    sceneBundle = AllocateTLSFGlobal(sizeof(SceneBundle));
    if (!LoadSceneBundleBinary("Assets/Meshes/Paladin/Paladin.abm", sceneBundle))
    // if (!ParseGLTF("Assets/Meshes/Paladin/Paladin.gltf", sceneBundle, 1.0f))
    {
        AX_ERROR("gltf scene load failed2");
        return;
    }

    // CreateVerticesIndicesSkined(sceneBundle);
    // SaveSceneImages(sceneBundle, "Assets/Meshes/Paladin/PaladinTest.bdc");   
    // SaveGLTFBinary(sceneBundle, "Assets/Meshes/Paladin/PaladinTest.abm");

    nodeTransforms = AllocateTLSFGlobal(sizeof(Matrix4) * sceneBundle->numNodes);
    characterRootIndex = Prefab_FindAnimRootNodeIndex(sceneBundle);

    for (int i = 0; i < NUM_ANIMS; i++)
    {
        Matrix3x4f16* outMatrices = OutMatrices + (i * MaxBonePoses);
        AnimationController_Create(sceneBundle, &animController[i], outMatrices);
    }

    Texture textures[8];
    LoadSceneImages("Assets/Meshes/Paladin/PaladinTest.bdc", textures, sceneBundle->numImages);

    sg_buffer vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = (sg_range){sceneBundle->allVertices, sceneBundle->totalVertices * sizeof(ASkinedVertex)},
        .label = "vertices"
    });

    sg_buffer ibuf = sg_make_buffer(&(sg_buffer_desc){
        .usage.index_buffer = true,
        .data = (sg_range){sceneBundle->allIndices, sceneBundle->totalIndices * sizeof(uint32_t)},
        .label = "indices"
    });

    // the first 4 samplers are just different min-filters
    sg_sampler_desc smp_desc = { .mag_filter = SG_FILTER_LINEAR, };
    smp_desc.min_lod        = 0.0f;
    smp_desc.max_lod        = 8;
    smp_desc.min_filter     = SG_FILTER_LINEAR;
    smp_desc.mag_filter     = SG_FILTER_LINEAR;
    smp_desc.mipmap_filter  = SG_FILTER_LINEAR;
    smp_desc.max_anisotropy = 8;
    sg_sampler sampler = sg_make_sampler(&smp_desc);
    
    sg_sampler_desc animSmpDesc = { };
    animSmpDesc.min_filter = SG_FILTER_NEAREST;
    animSmpDesc.mag_filter = SG_FILTER_NEAREST;
    animSmpDesc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
    animSmpDesc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
    animSmpDesc.label      = "joint-texture-sampler";
    sg_sampler  jointSampler = sg_make_sampler(&animSmpDesc);

    /* create shader */
    sg_shader shader = sg_make_shader(cube_shader_desc(sg_query_backend()));

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .layout = {
            .attrs = {
                [0].format = SG_VERTEXFORMAT_FLOAT3,
                [1].format = SG_VERTEXFORMAT_UINT10_N2,
                [2].format = SG_VERTEXFORMAT_UINT10_N2,
                [3].format = SG_VERTEXFORMAT_HALF2,
                [4].format = SG_VERTEXFORMAT_UBYTE4,
                [5].format = SG_VERTEXFORMAT_UBYTE4N,
            }
        },
        .shader     = shader,
        .index_type = SG_INDEXTYPE_UINT32,
        .cull_mode  = SG_CULLMODE_FRONT,
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true
        },
        .label = "pipeline"
    });
    
    sg_buffer matrixSSBO = sg_make_buffer(&(sg_buffer_desc){
        .usage = {
            .storage_buffer = true,
            .stream_update  = true,
        },
        .size  = sizeof(OutMatrices),   // reserve GPU memory
        .label = "JointMatrixBuffer",
    });

    sg_buffer EntityPositionSSBO = sg_make_buffer(&(sg_buffer_desc){
        .usage = {
            .storage_buffer = true,
            .stream_update  = true,
        },
        .size  = MAX_ENTITY * sizeof(Vector4x32f),
        .label = "EntityPositionSSBO",
    });

    sg_buffer EntityRotationSSBO = sg_make_buffer(&(sg_buffer_desc){
        .usage = {
            .storage_buffer = true,
            .stream_update  = true,
        },
        .size  = MAX_ENTITY * sizeof(uint32_t) * 2,
        .label = "EntityRotationSSBO",
    });
    
    state.bind = (sg_bindings) {
        .vertex_buffers[0]  = vbuf,
        .samplers[0]        = sampler,
        .samplers[1]        = jointSampler,
        .images[0]          = textures[sceneBundle->materials[0].baseColorTexture.index].handle,
        .index_buffer       = ibuf,
        .storage_buffers[0] = matrixSSBO,
        .storage_buffers[1] = EntityPositionSSBO,
        .storage_buffers[2] = EntityRotationSSBO
    };
}


void Frame(void)
{
    /* NOTE: the vs_params_t struct has been code-generated by the shader-code-gen */
    const float    w                = sapp_widthf();
    const float    h                = sapp_heightf();
    const double   dt               = sapp_frame_duration();
    const uint64_t frameStart       = stm_now();
    const double   timeSinceStartup = stm_sec(stm_diff(frameStart, PlatformCtx.StartupTime));
    PlatformCtx.DeltaTime = dt;

    state.rx += (float)dt;
    state.ry += (float)dt;

    CameraUpdate(&globalCamera, (float)dt);
    
    static uint64_t globalFrameCount = 0;

    int i;
    #pragma omp parallel for schedule(static)
    for (i = 0; i < NUM_ANIMS; i++)
    {
        Vec3f entityPos; 
        Vec3Store(&entityPos.x, ecs.EntityPositions[i]);
    
        float distSqr = Vec3DistSqr(globalCamera.position, entityPos);
    
        const float MedAnimDistSqr = 40 * 40;  
        const float FarAnimDistSqr = 120 * 120;
    
        // Determine the update frequency based on distance
        int updateRate = 1; 
        if (distSqr > FarAnimDistSqr) {
            updateRate = 8; // Sample every 8th frame
        } else if (distSqr > MedAnimDistSqr) {
            updateRate = 4;  // Sample every 4th frame
        }

        // Logic: Only update if (CurrentFrame + EntityIndex) % UpdateRate == 0
        // This staggers the updates so the CPU doesn't spike all at once.
        bool shouldUpdate = (timeSinceStartup < 1.0f) || ((globalFrameCount + i) % updateRate == 0);

        if (shouldUpdate)
        {
            AnimationController* ac = &animController[i];

            const int numAnims = ac->mPrefab->numAnimations;
            const int animIdx  = Clamp32(WangHash(i + 645) % numAnims, 1, numAnims);

            const double animDuration = (double)ac->mPrefab->animations[animIdx].duration;
            const float animRatio = (float)Fract((timeSinceStartup + (i * 0.1)) / animDuration);

            AnimationController_PlayAnim(ac, animIdx, animRatio);
        }
    }

    ECS_Update(&ecs, PlatformCtx.DeltaTime);

    // upload anim matrix 
    sg_update_buffer(state.bind.storage_buffers[0], &SG_RANGE(OutMatrices));
    sg_update_buffer(state.bind.storage_buffers[1], &SG_RANGE(ecs.EntityPositions));
    sg_update_buffer(state.bind.storage_buffers[2], &SG_RANGE(ecs.EntityRotations));
    
    vs_params_t vs_params;
    vs_params.uViewProj = Matrix4Multiply(globalCamera.view, globalCamera.projection);
    for (i = 0; i < 16; i++)
        vs_params.uLightMatrix.m[0][i] = (float)timeSinceStartup;

    sg_begin_pass(&(sg_pass) {
        .action = {
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = { 0.25f, 0.5f, 0.75f, 1.0f }
            },
        },
        .swapchain = sglue_swapchain()
    });

    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);
    sg_apply_uniforms(UB_vs_params, &SG_RANGE(vs_params));
    
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
        
            sg_draw(primitive->indexOffset, primitive->numIndices, NUM_ANIMS);
        }
    
        for (int i = 0; i < node->numChildren; i++)
        {
            nodeStack[stackLen++] = node->children[i];
        }
    }
    

    sg_end_pass();
    sg_commit();
    

    static uint64_t lastUpdate = 0;
    uint64_t now = stm_now();

    if (stm_ms(stm_diff(now, lastUpdate)) >= 250) 
    {
        lastUpdate = now;
        int64_t elapsedMS = (int64_t)stm_us(stm_since(frameStart));
        static char msBuffer[128]; 
        IntToString(msBuffer, elapsedMS, 0); 
        sapp_set_window_title(msBuffer);
    }

    
    globalFrameCount++;
}

void Cleanup(void) {
    sg_shutdown();
    rDestroy();
}

extern void sokol_event_callback(const sapp_event* event);

void* alloc_fn(size_t size, void* user_data)
{
    (void)user_data;
    return AllocateTLSFGlobal(size);
}

void free_fn(void* ptr, void* user_data)
{
    (void)user_data;
    DeAllocateTLSFGlobal(ptr);
}

extern char AppMemory[TLSF_MEMORY_SIZE];

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    meshopt_setAllocator(AllocateTLSFGlobal, DeAllocateTLSFGlobal);

    return (sapp_desc){
        .init_cb            = Init,
        .frame_cb           = Frame,
        .cleanup_cb         = Cleanup,
        .allocator.alloc_fn = alloc_fn,
        .allocator.free_fn  = free_fn,
        .event_cb           = sokol_event_callback,
        .width              = ScreenStartWidth,
        .height             = ScreenStartHeight,
        .sample_count       = 1,
        .window_title       = "CPlayground",
        .swap_interval      = 1, // vsync
        .icon.sokol_default = true,
        .logger.func        = slog_func,
    };
}

static void _sapp_setup_wave_icon(void)
{
    const uint32_t colors[8] = { 0xFF003300, 0xFF006600, 0xFF009900, 0xFF00CC00, 0xFF00FF00, 0xFF33FF33, 0xFF66FF66, 0xFF99FF99 };
    const int icon_sizes[3] = { 16, 32, 64 };
    uint32_t wave_icon_pixels[16 * 16 + 32 * 32 + 64 * 64] = {0};
    uint32_t* dst = wave_icon_pixels;
    
    sapp_icon_desc icon_desc = {0};
    for (int i = 0; i < 3; i++) {
        const int dim = icon_sizes[i];
        icon_desc.images[i] = (sapp_image_desc){dim, dim, {dst, dim * dim * sizeof(uint32_t)}};
        dst += dim * dim;
    }
    
    dst = wave_icon_pixels;
    for (int i = 0; i < 3; i++) {
        const int dim = icon_sizes[i];
        for (int y = 0; y < dim; y++) {
            for (int x = 0; x < dim; x++) {
                int wave_y = dim/2 + SinR(x * 6.28f / dim * 2.0f) * dim * 0.3f;
                *dst++ = (Absf(y - wave_y) <= 2) ? colors[x * 7 / dim] : 0x00FFFFFF;
            }
        }
    }
    sapp_set_icon(&icon_desc);
}