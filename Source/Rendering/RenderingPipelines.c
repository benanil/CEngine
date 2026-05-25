#include "RenderingInternal.h"
#include "Include/Slug.h"

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
#include "Shaders/spv/SkinnedFrag.spv.h"
#include "Shaders/spv/SkinnedVert.spv.h"
#include "Shaders/spv/SurfaceFrag.spv.h"
#include "Shaders/spv/SurfaceVert.spv.h"
#include "Shaders/spv/DeferredLighting.spv.h"
#include "Shaders/spv/CullDrawArgsCompute.spv.h"
#include "Shaders/spv/AnimationCompute.spv.h"
#include "Shaders/spv/AnimateVertices.spv.h"
#include "Shaders/spv/LineDebugVert.spv.h"
#include "Shaders/spv/LineDebugFrag.spv.h"
#include "Shaders/spv/SlugVert.spv.h"
#include "Shaders/spv/SlugFrag.spv.h"
#include "Shaders/spv/UIShapeVert.spv.h"
#include "Shaders/spv/UIShapeFrag.spv.h"
#include "Shaders/spv/UIImageVert.spv.h"
#include "Shaders/spv/UIImageFrag.spv.h"
#include "Shaders/spv/TonemapCompute.spv.h"
#include "Shaders/spv/HiZBuildCompute.spv.h"
#include "Shaders/spv/HiZDownscaleCompute.spv.h"
#include "Shaders/spv/HBAOCompute.spv.h"
#include "Shaders/spv/HBAOBlurCompute.spv.h"
#include "Shaders/spv/ExtractNormalCompute.spv.h"
#include "Shaders/spv/MLAAEdgeMaskCompute.spv.h"
#include "Shaders/spv/MLAALineLengthCompute.spv.h"
#include "Shaders/spv/MLAABlendCompute.spv.h"
#include "Shaders/spv/SurfaceDepthOnlyVert.spv.h"
#include "Shaders/spv/SurfaceDepthOnlyFrag.spv.h"
#include "Shaders/spv/SkinnedDepthOnlyVert.spv.h"
#include "Shaders/spv/SkinnedDepthOnlyFrag.spv.h"
#include "Shaders/spv/SurfaceShadowDepthOnlyVert.spv.h"
#include "Shaders/spv/SurfaceShadowDepthOnlyFrag.spv.h"
#include "Shaders/spv/SkinnedShadowDepthOnlyVert.spv.h"
#include "Shaders/spv/SkinnedShadowDepthOnlyFrag.spv.h"
#endif

SDL_GPUComputePipeline* g_AnimComputePipeline = NULL;
SDL_GPUComputePipeline* g_AnimVerticesPipeline = NULL;
SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline = NULL;
SDL_GPUComputePipeline* g_TonemapComputePipeline = NULL;
SDL_GPUComputePipeline* g_HiZBuildComputePipeline = NULL;
SDL_GPUComputePipeline* g_HiZDownscaleComputePipeline = NULL;
SDL_GPUComputePipeline* g_HBAOComputePipeline = NULL;
SDL_GPUComputePipeline* g_HBAOBlurComputePipeline = NULL;
SDL_GPUComputePipeline* g_ExtractNormalComputePipeline = NULL;
SDL_GPUComputePipeline* g_DeferredLightingComputePipeline = NULL;
SDL_GPUComputePipeline* g_MLAAEdgeMaskComputePipeline = NULL;
SDL_GPUComputePipeline* g_MLAALineLengthComputePipeline = NULL;
SDL_GPUComputePipeline* g_MLAABlendComputePipeline = NULL;

static void InitComputePipelines(void)
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
    CHECK_CREATE(g_AnimComputePipeline, "Animation Compute Pipeline");

    g_AnimVerticesPipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_AnimateVertices_spv,
        .code_size                     = sizeof(Shaders_AnimateVertices_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 6,
        .num_readwrite_storage_buffers = 1,
        .threadcount_x                 = 1,
        .threadcount_y                 = 32,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_AnimVerticesPipeline, "Animation vertices Pipeline");

    g_CullDrawArgsComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                          = Shaders_CullDrawArgsCompute_spv,
        .code_size                     = sizeof(Shaders_CullDrawArgsCompute_spv),
        .entrypoint                    = "main",
        .format                        = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_readonly_storage_textures = 1,
        .num_uniform_buffers           = 1,
        .num_readonly_storage_buffers  = 3,
        .num_readwrite_storage_buffers = 8,
        .threadcount_x                 = 64,
        .threadcount_y                 = 1,
        .threadcount_z                 = 1,
    });
    CHECK_CREATE(g_CullDrawArgsComputePipeline, "Cull Draw Args Compute Pipeline");

    g_HiZBuildComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_HiZBuildCompute_spv,
        .code_size                      = sizeof(Shaders_HiZBuildCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_HiZBuildComputePipeline, "Hi-Z Build Compute Pipeline");

    g_HiZDownscaleComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_HiZDownscaleCompute_spv,
        .code_size                      = sizeof(Shaders_HiZDownscaleCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_readonly_storage_textures  = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_HiZDownscaleComputePipeline, "Hi-Z Downscale Compute Pipeline");

    g_TonemapComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_TonemapCompute_spv,
        .code_size                      = sizeof(Shaders_TonemapCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 3,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_TonemapComputePipeline, "Tonemap Compute Pipeline");

    g_HBAOComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_HBAOCompute_spv,
        .code_size                      = sizeof(Shaders_HBAOCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 2,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_HBAOComputePipeline, "HBAO Compute Pipeline");

    g_ExtractNormalComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_ExtractNormalCompute_spv,
        .code_size                      = sizeof(Shaders_ExtractNormalCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 1,
        .num_readwrite_storage_textures = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_ExtractNormalComputePipeline, "Extract Normal Compute Pipeline");

    g_HBAOBlurComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_HBAOBlurCompute_spv,
        .code_size                      = sizeof(Shaders_HBAOBlurCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 2,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_HBAOBlurComputePipeline, "HBAO Blur Compute Pipeline");

    g_DeferredLightingComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_DeferredLighting_spv,
        .code_size                      = sizeof(Shaders_DeferredLighting_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 5,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_DeferredLightingComputePipeline, "Deferred Lighting Compute Pipeline");

    g_MLAAEdgeMaskComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_MLAAEdgeMaskCompute_spv,
        .code_size                      = sizeof(Shaders_MLAAEdgeMaskCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_MLAAEdgeMaskComputePipeline, "MLAA Edge Mask Compute Pipeline");

    g_MLAALineLengthComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_MLAALineLengthCompute_spv,
        .code_size                      = sizeof(Shaders_MLAALineLengthCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_readonly_storage_textures  = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_MLAALineLengthComputePipeline, "MLAA Line Length Compute Pipeline");

    g_MLAABlendComputePipeline = SDL_CreateGPUComputePipeline(g_GPUDevice, &(SDL_GPUComputePipelineCreateInfo){
        .code                           = Shaders_MLAABlendCompute_spv,
        .code_size                      = sizeof(Shaders_MLAABlendCompute_spv),
        .entrypoint                     = "main",
        .format                         = SDL_GetGPUShaderFormats(g_GPUDevice),
        .num_samplers                   = 1,
        .num_readonly_storage_textures  = 2,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_MLAABlendComputePipeline, "MLAA Blend Compute Pipeline");
}

static void InitSamplers(void)
{
    g_RenderState.sampler = SDL_CreateGPUSampler(g_GPUDevice, &(SDL_GPUSamplerCreateInfo){
        .min_filter      = SDL_GPU_FILTER_LINEAR,
        .mag_filter      = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode     = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w  = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .min_lod         = 0.0f,
        .max_lod         = 12.0f
    });
    CHECK_CREATE(g_RenderState.sampler, "Linear Sampler")

    g_RenderState.hiZSampler = SDL_CreateGPUSampler(g_GPUDevice, &(SDL_GPUSamplerCreateInfo){
        .min_filter      = SDL_GPU_FILTER_NEAREST,
        .mag_filter      = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode     = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .min_lod         = 0.0f,
        .max_lod         = 32.0f
    });
    CHECK_CREATE(g_RenderState.hiZSampler, "Hi-Z Sampler")

    g_RenderState.shadowSampler = SDL_CreateGPUSampler(g_GPUDevice, &(SDL_GPUSamplerCreateInfo){
        .min_filter      = SDL_GPU_FILTER_NEAREST,
        .mag_filter      = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode     = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w  = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .min_lod         = 0.0f,
        .max_lod         = SHADOW_CASCADE_COUNT
    });
    CHECK_CREATE(g_RenderState.shadowSampler, "Shadow Sampler")
}

static void InitLinePipeline(void)
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
    }); CHECK_CREATE(vertex_shader, "Vertex Shader")

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
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_UINT, .offset = sizeof(f32) * 3 }
    };

    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){
                0, sizeof(ALineVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0
            },
            .num_vertex_buffers    = 1,
            .vertex_attributes     = vertex_attributes,
            .num_vertex_attributes = ARRAY_SIZE(vertex_attributes)
        }
    };

    g_RenderState.linePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.linePipeline, "Render Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitSlugPipeline(void)
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SlugVert_spv,
        .code_size           = sizeof(Shaders_SlugVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    }); CHECK_CREATE(vertex_shader, "Slug Vertex Shader")

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SlugFrag_spv,
        .code_size           = sizeof(Shaders_SlugFrag_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 2,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    }); CHECK_CREATE(fragment_shader, "Slug Fragment Shader")

    const SDL_GPUVertexAttribute vertex_attributes[6] = {
        { .location = 0, .buffer_slot = 0, .format = VFORMAT_FLOAT4, .offset = offsetof(SlugVertex, pos) },
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_FLOAT4, .offset = offsetof(SlugVertex, tex) },
        { .location = 2, .buffer_slot = 0, .format = VFORMAT_FLOAT4, .offset = offsetof(SlugVertex, jac) },
        { .location = 3, .buffer_slot = 0, .format = VFORMAT_FLOAT4, .offset = offsetof(SlugVertex, band) },
        { .location = 4, .buffer_slot = 0, .format = VFORMAT_UINT  , .offset = offsetof(SlugVertex, color) },
        { .location = 5, .buffer_slot = 0, .format = VFORMAT_F32   , .offset = offsetof(SlugVertex, z) }
    };
    SDL_GPUColorTargetDescription colorTarget = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {
            .enable_blend = true,
            .color_write_mask = 0xF,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &colorTarget
        },
        .multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){ 0, sizeof(SlugVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 },
            .num_vertex_buffers    = 1,
            .vertex_attributes     = vertex_attributes,
            .num_vertex_attributes = ARRAY_SIZE(vertex_attributes)
        }
    };

    g_RenderState.slugPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.slugPipeline, "Slug Render Pipeline")

    pipelinedesc.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pipelinedesc.target_info.has_depth_stencil_target = true;
    pipelinedesc.depth_stencil_state = (SDL_GPUDepthStencilState){
        .enable_depth_test  = true,
        .enable_depth_write = false,
        .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
    };
    g_RenderState.slugDepthPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.slugDepthPipeline, "Slug Depth Render Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitUIShapePipeline(void)
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_UIShapeVert_spv,
        .code_size           = sizeof(Shaders_UIShapeVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 1,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    }); CHECK_CREATE(vertex_shader, "UI Shape Vertex Shader")

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_UIShapeFrag_spv,
        .code_size           = sizeof(Shaders_UIShapeFrag_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    }); CHECK_CREATE(fragment_shader, "UI Shape Fragment Shader")

    SDL_GPUColorTargetDescription colorTarget = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {
            .enable_blend = true,
            .color_write_mask = 0xF,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &colorTarget
        },
        .multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 }
    };

    g_RenderState.uiShapePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &pipelinedesc);
    CHECK_CREATE(g_RenderState.uiShapePipeline, "UI Shape Render Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitUIImagePipeline(void)
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_UIImageVert_spv,
        .code_size           = sizeof(Shaders_UIImageVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    }); CHECK_CREATE(vertex_shader, "UI Image Vertex Shader")

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_UIImageFrag_spv,
        .code_size           = sizeof(Shaders_UIImageFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    }); CHECK_CREATE(fragment_shader, "UI Image Fragment Shader")

    SDL_GPUColorTargetDescription colorTarget = {
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = {
            .enable_blend = true,
            .color_write_mask = 0xF,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
        }
    };

    g_RenderState.uiImagePipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &colorTarget
        },
        .multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 }
    });
    CHECK_CREATE(g_RenderState.uiImagePipeline, "UI Image Render Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitSkinedPipeline(void)
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedVert_spv,
        .code_size           = sizeof(Shaders_SkinnedVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 5,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedFrag_spv,
        .code_size           = sizeof(Shaders_SkinnedFrag_spv),
        .num_samplers        = 4,
        .num_storage_buffers = 2,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });

    CHECK_CREATE(vertex_shader, "Vertex Shader")
    CHECK_CREATE(fragment_shader, "Fragment Shader")

    const SDL_GPUVertexAttribute vertex_attributes[5] = {
        { .location = 0, .buffer_slot = 0, .format = VFORMAT_HALF4,  .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_UINT,   .offset = offsetof(ASkinedVertex, octTbn) },
        { .location = 2, .buffer_slot = 0, .format = VFORMAT_HALF2,  .offset = offsetof(ASkinedVertex, texCoord) },
        { .location = 3, .buffer_slot = 0, .format = VFORMAT_UBYTE4, .offset = offsetof(ASkinedVertex, joints) },
        { .location = 4, .buffer_slot = 0, .format = VFORMAT_UINT,   .offset = offsetof(ASkinedVertex, weights) }
    };

    const SDL_GPUColorTargetDescription gbufferTargets[3] = {
        { .format = SDL_GPU_TEXTUREFORMAT_R32_UINT },
        { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM },
        { .format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM }
    };
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 3,
            .color_target_descriptions = gbufferTargets,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
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

    SDL_ReleaseGPUShader(g_GPUDevice, vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, fragment_shader);
}

static void InitSurfacePipeline(void)
{
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceVert_spv,
        .code_size           = sizeof(Shaders_SurfaceVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 4,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceFrag_spv,
        .code_size           = sizeof(Shaders_SurfaceFrag_spv),
        .num_samplers        = 4,
        .num_storage_buffers = 2,
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

    const SDL_GPUColorTargetDescription gbufferTargets[3] = {
        { .format = SDL_GPU_TEXTUREFORMAT_R32_UINT },
        { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM },
        { .format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 3,
            .color_target_descriptions = gbufferTargets,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
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

static void InitDepthOnlyPipelines(void)
{
    SDL_GPUShader* surface_vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceDepthOnlyVert_spv,
        .code_size           = sizeof(Shaders_SurfaceDepthOnlyVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 3,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });
    SDL_GPUShader* surface_fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceDepthOnlyFrag_spv,
        .code_size           = sizeof(Shaders_SurfaceDepthOnlyFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 2,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });
    SDL_GPUShader* skinned_vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedDepthOnlyVert_spv,
        .code_size           = sizeof(Shaders_SkinnedDepthOnlyVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 4,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });
    SDL_GPUShader* skinned_fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedDepthOnlyFrag_spv,
        .code_size           = sizeof(Shaders_SkinnedDepthOnlyFrag_spv),
        .num_samplers        = 1,
        .num_storage_buffers = 2,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });
    SDL_GPUShader* surface_shadow_vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceShadowDepthOnlyVert_spv,
        .code_size           = sizeof(Shaders_SurfaceShadowDepthOnlyVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 4,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });
    SDL_GPUShader* surface_shadow_fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SurfaceShadowDepthOnlyFrag_spv,
        .code_size           = sizeof(Shaders_SurfaceShadowDepthOnlyFrag_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });
    SDL_GPUShader* skinned_shadow_vertex_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 1,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedShadowDepthOnlyVert_spv,
        .code_size           = sizeof(Shaders_SkinnedShadowDepthOnlyVert_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 5,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });
    SDL_GPUShader* skinned_shadow_fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
        .format              = SDL_GetGPUShaderFormats(g_GPUDevice),
        .code                = Shaders_SkinnedShadowDepthOnlyFrag_spv,
        .code_size           = sizeof(Shaders_SkinnedShadowDepthOnlyFrag_spv),
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });
    CHECK_CREATE(surface_vertex_shader, "Surface Depth Vertex Shader")
    CHECK_CREATE(surface_fragment_shader, "Surface Depth Fragment Shader")
    CHECK_CREATE(skinned_vertex_shader, "Skinned Depth Vertex Shader")
    CHECK_CREATE(skinned_fragment_shader, "Skinned Depth Fragment Shader")
    CHECK_CREATE(surface_shadow_vertex_shader, "Surface Shadow Vertex Shader")
    CHECK_CREATE(surface_shadow_fragment_shader, "Surface Shadow Fragment Shader")
    CHECK_CREATE(skinned_shadow_vertex_shader, "Skinned Shadow Vertex Shader")
    CHECK_CREATE(skinned_shadow_fragment_shader, "Skinned Shadow Fragment Shader")

    const SDL_GPUVertexAttribute surface_attributes[3] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(AVertex, position) },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UINT,   .offset = offsetof(AVertex, octTbn) },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_HALF2,  .offset = offsetof(AVertex, texCoord) }
    };
    const SDL_GPUVertexAttribute skinned_attributes[5] = {
        { .location = 0, .buffer_slot = 0, .format = VFORMAT_HALF4,  .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = VFORMAT_UINT,   .offset = offsetof(ASkinedVertex, octTbn) },
        { .location = 2, .buffer_slot = 0, .format = VFORMAT_HALF2,  .offset = offsetof(ASkinedVertex, texCoord) },
        { .location = 3, .buffer_slot = 0, .format = VFORMAT_UBYTE4, .offset = offsetof(ASkinedVertex, joints) },
        { .location = 4, .buffer_slot = 0, .format = VFORMAT_UINT,   .offset = offsetof(ASkinedVertex, weights) }
    };

    SDL_GPUGraphicsPipelineCreateInfo surface_desc = {
        .vertex_shader   = surface_vertex_shader,
        .fragment_shader = surface_fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT },
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){ 0, sizeof(AVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 },
            .num_vertex_buffers = 1,
            .vertex_attributes = surface_attributes,
            .num_vertex_attributes = ARRAY_SIZE(surface_attributes)
        }
    };
    SDL_GPUGraphicsPipelineCreateInfo skinned_desc = surface_desc;
    skinned_desc.vertex_shader = skinned_vertex_shader;
    skinned_desc.fragment_shader = skinned_fragment_shader;
    skinned_desc.vertex_input_state = (SDL_GPUVertexInputState){
        .vertex_buffer_descriptions = &(SDL_GPUVertexBufferDescription){ 0, sizeof(ASkinedVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 },
        .num_vertex_buffers = 1,
        .vertex_attributes = skinned_attributes,
        .num_vertex_attributes = ARRAY_SIZE(skinned_attributes)
    };

    g_RenderState.surfaceDepthPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &surface_desc);
    g_RenderState.skinnedDepthPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &skinned_desc);
    surface_desc.vertex_shader = surface_shadow_vertex_shader;
    surface_desc.fragment_shader = surface_shadow_fragment_shader;
    skinned_desc.vertex_shader = skinned_shadow_vertex_shader;
    skinned_desc.fragment_shader = skinned_shadow_fragment_shader;
    g_RenderState.surfaceShadowPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &surface_desc);
    g_RenderState.skinnedShadowPipeline = SDL_CreateGPUGraphicsPipeline(g_GPUDevice, &skinned_desc);
    CHECK_CREATE(g_RenderState.surfaceDepthPipeline, "Surface Depth Pipeline")
    CHECK_CREATE(g_RenderState.skinnedDepthPipeline, "Skinned Depth Pipeline")
    CHECK_CREATE(g_RenderState.surfaceShadowPipeline, "Surface Shadow Pipeline")
    CHECK_CREATE(g_RenderState.skinnedShadowPipeline, "Skinned Shadow Pipeline")

    SDL_ReleaseGPUShader(g_GPUDevice, surface_vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, surface_fragment_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, skinned_vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, skinned_fragment_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, surface_shadow_vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, surface_shadow_fragment_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, skinned_shadow_vertex_shader);
    SDL_ReleaseGPUShader(g_GPUDevice, skinned_shadow_fragment_shader);
}
extern void InitSDSM();

void InitRenderPipelines(void)
{
    InitSamplers();
    InitSkinedPipeline();
    InitSurfacePipeline();
    InitDepthOnlyPipelines();
    InitSDSM();
    InitLinePipeline();
    InitSlugPipeline();
    InitUIShapePipeline();
    InitUIImagePipeline();
    InitComputePipelines();
}

extern void DestroySDSM();

void DestroyRenderPipelines(void)
{
    DestroySDSM();
    if (g_RenderState.skinnedPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedPipeline);
    if (g_RenderState.surfacePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfacePipeline);
    if (g_RenderState.skinnedDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedDepthPipeline);
    if (g_RenderState.surfaceDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfaceDepthPipeline);
    if (g_RenderState.skinnedShadowPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedShadowPipeline);
    if (g_RenderState.surfaceShadowPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfaceShadowPipeline);
    if (g_RenderState.linePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.linePipeline);
    if (g_RenderState.slugPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.slugPipeline);
    if (g_RenderState.slugDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.slugDepthPipeline);
    if (g_RenderState.uiShapePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.uiShapePipeline);
    if (g_RenderState.uiImagePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.uiImagePipeline);
    if (g_AnimComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_AnimComputePipeline);
    if (g_AnimVerticesPipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_AnimVerticesPipeline);
    if (g_CullDrawArgsComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_CullDrawArgsComputePipeline);
    if (g_TonemapComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_TonemapComputePipeline);
    if (g_HiZBuildComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HiZBuildComputePipeline);
    if (g_HiZDownscaleComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HiZDownscaleComputePipeline);
    if (g_HBAOComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HBAOComputePipeline);
    if (g_HBAOBlurComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HBAOBlurComputePipeline);
    if (g_ExtractNormalComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_ExtractNormalComputePipeline);
    if (g_DeferredLightingComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_DeferredLightingComputePipeline);
    if (g_MLAAEdgeMaskComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_MLAAEdgeMaskComputePipeline);
    if (g_MLAALineLengthComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_MLAALineLengthComputePipeline);
    if (g_MLAABlendComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_MLAABlendComputePipeline);
}
