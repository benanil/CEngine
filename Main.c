#define SOKOL_LOG_IMPL
#define SOKOL_GLUE_IMPL

#define SOKOL_GFX_IMPL
#define SOKOL_D3D11

#include "Include/OS.h"
#include "Include/Memory.h"

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
static AnimationController animController;

static void _sapp_setup_wave_icon(void);

static const int ScreenStartWidth = 1920, ScreenStartHeight = 1080;

void Init(void)
{
    BasisuSetup();
    _sapp_setup_wave_icon();
    stm_setup();
    PlatformInit();
    rInit();

    PlatformCtx.StartupTime = stm_now();
    MemsetZero(&globalCamera, sizeof(globalCamera));
    globalCamera.pitch       = 0.0f;
    globalCamera.yaw         = 0.0f;
    globalCamera.senstivity  = 15.0f;
    globalCamera.verticalFOV = 65.0f;
    globalCamera.nearClip    = 0.1f;
    globalCamera.farClip     = 2400.0f;
    globalCamera.speed       = 0.4f;
    globalCamera.position.x -= 6;

    CameraInit(&globalCamera, ScreenStartWidth, ScreenStartHeight);

    sg_setup(&(sg_desc) {
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });

    // if (!LoadSceneBundleBinary("Assets/Meshes/Paladin/Paladin.abm", sceneBundle))
    sceneBundle = AllocateTLSFGlobal(sizeof(SceneBundle));
    if (!ParseGLTF("Assets/Meshes/Paladin/Paladin.gltf", sceneBundle, 1.0f))
    {
        AX_ERROR("gltf scene load failed2");
        return;
    }
    
    CreateVerticesIndicesSkined(sceneBundle);
    // SaveSceneImages(sceneBundle, "Assets/Meshes/Paladin/PaladinTest.bdc");
    // SaveGLTFBinary(sceneBundle, "Assets/Meshes/Paladin/PaladinTest.abm");

    nodeTransforms = AllocateTLSFGlobal(sizeof(Matrix4) * sceneBundle->numNodes);
    characterRootIndex = Prefab_FindAnimRootNodeIndex(sceneBundle);

    AnimationController* ac = &animController;
    AnimationController_Create(sceneBundle, &animController, true, 58);
    Prefab_UpdateGlobalNodeTransforms(sceneBundle, characterRootIndex, Matrix4Identity());
    AnimationController_SampleAnimationPose(ac, ac->mAnimPoseA, 1, 0.0f);
    AnimationController_UploadPose(ac, ac->mAnimPoseA);

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
    smp_desc.min_lod = 0.0f;
    smp_desc.max_lod = 8;
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.mipmap_filter = SG_FILTER_LINEAR;
    smp_desc.max_anisotropy = 8;
    sg_sampler sampler = sg_make_sampler(&smp_desc);
    
    sg_sampler_desc animSmpDesc = { };
    animSmpDesc.min_filter = SG_FILTER_NEAREST;
    animSmpDesc.mag_filter = SG_FILTER_NEAREST;
    animSmpDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    animSmpDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    animSmpDesc.label = "joint-texture-sampler";
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
        .shader = shader,
        .index_type = SG_INDEXTYPE_UINT32,
        .cull_mode = SG_CULLMODE_FRONT,
        .depth = {
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true
        },
        .label = "pipeline"
    });

    
    /* setup resource bindings */
    state.bind = (sg_bindings) {
        .vertex_buffers[0] = vbuf,
        .samplers[0] = sampler,
        .samplers[1] = jointSampler,
        .images[0] = textures[sceneBundle->materials[0].baseColorTexture.index].handle,
        .images[1] = animController.mMatrixTex.handle,
        .index_buffer = ibuf
    };
}


void Frame(void)
{
    /* NOTE: the vs_params_t struct has been code-generated by the shader-code-gen */
    vs_params_t vs_params;
    const float w = sapp_widthf();
    const float h = sapp_heightf();
    const double dt = sapp_frame_duration();
    PlatformCtx.DeltaTime = dt;

    state.rx += (float)dt;
    state.ry += (float)dt;

    static bool first = true;
    if (!first)
    {
        double animTime = Fract(TimeSinceStartup() * 0.1);
        AnimationController_SampleAnimationPose(&animController, animController.mAnimPoseA, 3, (float)animTime);
        AnimationController_UploadPose(&animController, animController.mAnimPoseA);
    }
    first = false;

    CameraUpdate(&globalCamera, (float)dt);
    
    const Matrix4 model = MatrixFromScalef(1.0f);
    const Matrix4 view_proj = Matrix4Multiply(globalCamera.view, globalCamera.projection);
    
    vs_params.mvp = Matrix4Multiply(model, view_proj);
    vs_params.uModel = model;
    // vs_params.uLightMatrix;
    vs_params.uViewProj = view_proj;

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
    int x = 0;

    while (stackLen > 0)
    {
        const int nodeIndex = nodeStack[--stackLen];
        const ANode* node = &sceneBundle->nodes[nodeIndex];
        const AMesh* mesh = sceneBundle->meshes + node->index;

        if (node->type == 0 && node->index != -1)
        for (int j = 0; j < mesh->numPrimitives; ++j)
        {
            const APrimitive* primitive = &mesh->primitives[j];
            const bool hasMaterial = sceneBundle->materials && primitive->material != UINT16_MAX;
            const AMaterial material = sceneBundle->materials[primitive->material];
            const Matrix4 model = nodeTransforms[nodeIndex];
            
            sg_draw(primitive->indexOffset, primitive->numIndices, 40);
        }

        for (int i = 0; i < node->numChildren; i++)
        {
            nodeStack[stackLen++] = node->children[i];
        }
    }

    sg_end_pass();
    sg_commit();
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

extern char AppMemory[1 * 1000 * 1000 * 1000];

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    const uint64_t halfGig = 500ull * 1000ull * 1000ull;
    GlobalTLSF = TLSFCreateWithPool(AppMemory, halfGig);
    GlobalArena.buf = AppMemory + halfGig;
    GlobalArena.buffLen = halfGig;
    GlobalArena.currOffset = 0;

    return (sapp_desc){
        .init_cb = Init,
        .frame_cb = Frame,
        .cleanup_cb = Cleanup,
        .allocator.alloc_fn = alloc_fn,
        .allocator.free_fn = free_fn,
        .event_cb = sokol_event_callback,
        .width = ScreenStartWidth,
        .height = ScreenStartHeight,
        .sample_count = 1,
        .window_title = "CPlayground",
        .icon.sokol_default = true,
        .logger.func = slog_func,
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