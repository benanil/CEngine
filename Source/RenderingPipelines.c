#include "RenderingInternal.h"

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
#include "Shaders/spv/CullDrawArgsCompute.spv.h"
#include "Shaders/spv/AnimationCompute.spv.h"
#include "Shaders/spv/AnimateVertices.spv.h"
#include "Shaders/spv/LineDebugVert.spv.h"
#include "Shaders/spv/LineDebugFrag.spv.h"
#include "Shaders/spv/TonemapCompute.spv.h"
#include "Shaders/spv/HiZBuildCompute.spv.h"
#include "Shaders/spv/HiZDownscaleCompute.spv.h"
#include "Shaders/spv/SurfaceDepthOnlyVert.spv.h"
#include "Shaders/spv/SurfaceDepthOnlyFrag.spv.h"
#include "Shaders/spv/SkinnedDepthOnlyVert.spv.h"
#include "Shaders/spv/SkinnedDepthOnlyFrag.spv.h"
#endif

#define VFORMAT_FLOAT3 SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3
#define VFORMAT_UINT   SDL_GPU_VERTEXELEMENTFORMAT_UINT
#define VFORMAT_HALF2  SDL_GPU_VERTEXELEMENTFORMAT_HALF2
#define VFORMAT_HALF4  SDL_GPU_VERTEXELEMENTFORMAT_HALF4
#define VFORMAT_UBYTE4 SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4

SDL_GPUComputePipeline* g_AnimComputePipeline = NULL;
SDL_GPUComputePipeline* g_AnimVerticesPipeline = NULL;
SDL_GPUComputePipeline* g_CullDrawArgsComputePipeline = NULL;
SDL_GPUComputePipeline* g_TonemapComputePipeline = NULL;
SDL_GPUComputePipeline* g_HiZBuildComputePipeline = NULL;
SDL_GPUComputePipeline* g_HiZDownscaleComputePipeline = NULL;

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
        .threadcount_x                 = 32,
        .threadcount_y                 = 1,
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
        .num_readonly_storage_textures  = 1,
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
        .num_samplers                   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    });
    CHECK_CREATE(g_TonemapComputePipeline, "Tonemap Compute Pipeline");
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
        .max_lod         = 0.0f
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

    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
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
        .multisample_state  = (SDL_GPUMultisampleState){ .sample_count = g_RenderState.sample_count },
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

static void InitSkinedPipeline(void)
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

    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = false,
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
        .num_storage_buffers = 3,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .entrypoint          = "vert"
    });

    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(g_GPUDevice, &(SDL_GPUShaderCreateInfo){
        .num_uniform_buffers = 0,
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

    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipelinedesc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info     = (SDL_GPUGraphicsPipelineTargetInfo){
            .num_color_targets         = 1,
            .color_target_descriptions = &(SDL_GPUColorTargetDescription){ .format = colorFormat },
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target  = true
        },
        .depth_stencil_state = (SDL_GPUDepthStencilState){
            .enable_depth_test  = true,
            .enable_depth_write = false,
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
        .num_samplers        = 0,
        .num_storage_buffers = 0,
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
        .num_samplers        = 0,
        .num_storage_buffers = 0,
        .stage               = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .entrypoint          = "frag"
    });
    CHECK_CREATE(surface_vertex_shader, "Surface Depth Vertex Shader")
    CHECK_CREATE(surface_fragment_shader, "Surface Depth Fragment Shader")
    CHECK_CREATE(skinned_vertex_shader, "Skinned Depth Vertex Shader")
    CHECK_CREATE(skinned_fragment_shader, "Skinned Depth Fragment Shader")

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
        .multisample_state = (SDL_GPUMultisampleState){ .sample_count = g_RenderState.sample_count },
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
    surface_desc.multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 };
    skinned_desc.multisample_state = (SDL_GPUMultisampleState){ .sample_count = SDL_GPU_SAMPLECOUNT_1 };
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
}

void InitRenderPipelines(void)
{
    InitSamplers();
    InitSkinedPipeline();
    InitSurfacePipeline();
    InitDepthOnlyPipelines();
    InitLinePipeline();
    InitComputePipelines();
}

void DestroyRenderPipelines(void)
{
    if (g_RenderState.skinnedPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedPipeline);
    if (g_RenderState.surfacePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfacePipeline);
    if (g_RenderState.skinnedDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedDepthPipeline);
    if (g_RenderState.surfaceDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfaceDepthPipeline);
    if (g_RenderState.skinnedShadowPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.skinnedShadowPipeline);
    if (g_RenderState.surfaceShadowPipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.surfaceShadowPipeline);
    if (g_RenderState.linePipeline) SDL_ReleaseGPUGraphicsPipeline(g_GPUDevice, g_RenderState.linePipeline);
    if (g_AnimComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_AnimComputePipeline);
    if (g_AnimVerticesPipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_AnimVerticesPipeline);
    if (g_CullDrawArgsComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_CullDrawArgsComputePipeline);
    if (g_TonemapComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_TonemapComputePipeline);
    if (g_HiZBuildComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HiZBuildComputePipeline);
    if (g_HiZDownscaleComputePipeline) SDL_ReleaseGPUComputePipeline(g_GPUDevice, g_HiZDownscaleComputePipeline);
}
