#include "Include/BasisCompressWrapper.h"

#include "Extern/basis_universal/encoder/basisu_comp.h"          // Main BasisU encoder API
#include "Extern/basis_universal/encoder/basisu_enc.h"         // For load_image()
#include "Extern/basis_universal/encoder/basisu_gpu_texture.h"
#include "Extern/basis_universal/transcoder/basisu_file_headers.h"  // For basis_tex_format
#include "Include/ParallelFor.h"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include "Extern/stb/stb_image.h"
#include "Extern/basis_universal/encoder/3rdparty/tinydds.h"

using namespace basisu;

typedef enum DDSBlockDecodeFormat_ {
    DDS_BLOCK_DECODE_BC1,
    DDS_BLOCK_DECODE_BC3,
    DDS_BLOCK_DECODE_BC4,
    DDS_BLOCK_DECODE_BC5,
} DDSBlockDecodeFormat;

typedef struct DDSUncompressedDecodeTask_
{
    TinyDDS_Format format;
    const uint8_t* src;
    image* img;
    uint32_t width;
    uint32_t bytesPerPixel;
} DDSUncompressedDecodeTask;

typedef struct DDSCompressedDecodeTask_
{
    DDSBlockDecodeFormat decodeFormat;
    const uint8_t* src;
    image* img;
    uint32_t width;
    uint32_t height;
    uint32_t blockBytes;
    uint32_t rowPitch;
} DDSCompressedDecodeTask;

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
static bool g_encoder_initialized = false;

static bool has_extension(const char* filename, const char* ext) {
    if (!filename || !ext) return false;

    const char* dot = NULL;
    for (const char* p = filename; *p; ++p) {
        if (*p == '.') dot = p;
    }
    if (!dot) return false;

    ++dot;
    while (*dot && *ext) {
        char a = *dot++;
        char b = *ext++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }

    return *dot == '\0' && *ext == '\0';
}

static bool is_dds_bc1(TinyDDS_Format format) {
    return format == TDDS_BC1_RGBA_UNORM_BLOCK ||
           format == TDDS_BC1_RGBA_SRGB_BLOCK ||
           (int)format == TIF_DXGI_FORMAT_BC1_TYPELESS;
}

static bool is_dds_bc3(TinyDDS_Format format) {
    return format == TDDS_BC3_UNORM_BLOCK ||
           format == TDDS_BC3_SRGB_BLOCK ||
           (int)format == TIF_DXGI_FORMAT_BC3_TYPELESS;
}

static bool is_dds_bc4(TinyDDS_Format format) {
    return format == TDDS_BC4_UNORM_BLOCK ||
           format == TDDS_BC4_SNORM_BLOCK ||
           (int)format == TIF_DXGI_FORMAT_BC4_TYPELESS;
}

static bool is_dds_bc5(TinyDDS_Format format) {
    return format == TDDS_BC5_UNORM_BLOCK ||
           format == TDDS_BC5_SNORM_BLOCK ||
           (int)format == TIF_DXGI_FORMAT_BC5_TYPELESS;
}

static bool is_dds_compressed(TinyDDS_Format format) {
    return is_dds_bc1(format) || is_dds_bc3(format) || is_dds_bc4(format) || is_dds_bc5(format);
}

static void decode_dds_uncompressed_range(uint32_t begin_y, uint32_t end_y, void* userData)
{
    DDSUncompressedDecodeTask* task = (DDSUncompressedDecodeTask*)userData;

    for (uint32_t y = begin_y; y < end_y; ++y) {
        const uint8_t* in = task->src + (size_t)y * task->width * task->bytesPerPixel;
        color_rgba* out = task->img->get_ptr() + (size_t)y * task->width;

        switch (task->format) {
            case TDDS_R8G8B8A8_UNORM:
            case TDDS_R8G8B8A8_SRGB:
                memcpy(out, in, (size_t)task->width * 4);
                break;

            case TDDS_B8G8R8A8_UNORM:
            case TDDS_B8G8R8A8_SRGB:
                for (uint32_t x = 0; x < task->width; ++x) {
                    out[x].r = in[x * 4 + 2];
                    out[x].g = in[x * 4 + 1];
                    out[x].b = in[x * 4 + 0];
                    out[x].a = in[x * 4 + 3];
                }
                break;

            case TDDS_B8G8R8X8_UNORM:
                for (uint32_t x = 0; x < task->width; ++x) {
                    out[x].r = in[x * 4 + 2];
                    out[x].g = in[x * 4 + 1];
                    out[x].b = in[x * 4 + 0];
                    out[x].a = 255;
                }
                break;

            case TDDS_R8_UNORM:
                for (uint32_t x = 0; x < task->width; ++x) {
                    out[x].r = in[x];
                    out[x].g = in[x];
                    out[x].b = in[x];
                    out[x].a = 255;
                }
                break;

            case TDDS_R8G8_UNORM:
                for (uint32_t x = 0; x < task->width; ++x) {
                    out[x].r = in[x * 2 + 0];
                    out[x].g = in[x * 2 + 1];
                    out[x].b = 0;
                    out[x].a = 255;
                }
                break;

            default:
                break;
        }
    }
}

static bool decode_dds_uncompressed(TinyDDS_Format format, const uint8_t* src, uint32_t width, uint32_t height, uint32_t size, image& img) {
    uint32_t bytes_per_pixel = 0;
    switch (format) {
        case TDDS_R8G8B8A8_UNORM:
        case TDDS_R8G8B8A8_SRGB:
        case TDDS_B8G8R8A8_UNORM:
        case TDDS_B8G8R8A8_SRGB:
        case TDDS_B8G8R8X8_UNORM:
            bytes_per_pixel = 4;
            break;
        case TDDS_R8_UNORM:
            bytes_per_pixel = 1;
            break;
        case TDDS_R8G8_UNORM:
            bytes_per_pixel = 2;
            break;
        default:
            return false;
    }

    if (size < (uint64_t)width * height * bytes_per_pixel) return false;

    DDSUncompressedDecodeTask task;
    task.format = format;
    task.src = src;
    task.img = &img;
    task.width = width;
    task.bytesPerPixel = bytes_per_pixel;
    ParallelFor(height, 128, decode_dds_uncompressed_range, &task);

    return true;
}

static void decode_dds_compressed_range(uint32_t begin_by, uint32_t end_by, void* userData)
{
    DDSCompressedDecodeTask* task = (DDSCompressedDecodeTask*)userData;

    for (uint32_t by = begin_by; by < end_by; ++by) {
        for (uint32_t bx = 0; bx < task->rowPitch / task->blockBytes; ++bx) {
            const uint8_t* block = task->src + (size_t)by * task->rowPitch + (size_t)bx * task->blockBytes;
            color_rgba pixels[16];

            switch (task->decodeFormat) {
                case DDS_BLOCK_DECODE_BC1:
                    unpack_bc1(block, pixels, true);
                    break;

                case DDS_BLOCK_DECODE_BC3:
                    unpack_bc3(block, pixels);
                    break;

                case DDS_BLOCK_DECODE_BC4:
                    unpack_bc4(block, &pixels[0].r, sizeof(color_rgba));
                    for (uint32_t i = 0; i < 16; ++i) {
                        pixels[i].g = pixels[i].r;
                        pixels[i].b = pixels[i].r;
                        pixels[i].a = 255;
                    }
                    break;

                case DDS_BLOCK_DECODE_BC5:
                    unpack_bc5(block, pixels);
                    for (uint32_t i = 0; i < 16; ++i) {
                        pixels[i].b = 0;
                        pixels[i].a = 255;
                    }
                    break;
            }

            for (uint32_t py = 0; py < 4; ++py) {
                const uint32_t y = by * 4 + py;
                if (y >= task->height) break;

                for (uint32_t px = 0; px < 4; ++px) {
                    const uint32_t x = bx * 4 + px;
                    if (x >= task->width) break;
                    (*task->img)(x, y) = pixels[py * 4 + px];
                }
            }
        }
    }
}

static bool decode_dds_compressed(TinyDDS_Format format, const uint8_t* src, uint32_t width, uint32_t height, uint32_t size, image& img) {
    const uint32_t block_bytes = (is_dds_bc1(format) || is_dds_bc4(format)) ? 8 : 16;
    const uint32_t blocks_x = (width + 3) >> 2;
    const uint32_t blocks_y = (height + 3) >> 2;
    const uint32_t row_pitch = blocks_x * block_bytes;
    if (size < (uint64_t)row_pitch * blocks_y) return false;

    DDSBlockDecodeFormat decode_format;
    if (is_dds_bc1(format))      decode_format = DDS_BLOCK_DECODE_BC1;
    else if (is_dds_bc3(format)) decode_format = DDS_BLOCK_DECODE_BC3;
    else if (is_dds_bc4(format)) decode_format = DDS_BLOCK_DECODE_BC4;
    else if (is_dds_bc5(format)) decode_format = DDS_BLOCK_DECODE_BC5;
    else  return false;
    

    DDSCompressedDecodeTask task;
    task.decodeFormat = decode_format;
    task.src = src;
    task.img = &img;
    task.width = width;
    task.height = height;
    task.blockBytes = block_bytes;
    task.rowPitch = row_pitch;
    ParallelFor(blocks_y, 16, decode_dds_compressed_range, &task);

    return true;
}

static void tinydds_error_callback(void* user, char const* msg) {
    BASISU_NOTE_UNUSED(user);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "tinydds: %s", msg);
}

static void* tinydds_alloc_callback(void* user, size_t size) {
    BASISU_NOTE_UNUSED(user);
    return SDL_malloc(size);
}

static void tinydds_free_callback(void* user, void* memory) {
    BASISU_NOTE_UNUSED(user);
    SDL_free(memory);
}

static size_t tinydds_read_callback(void* user, void* buffer, size_t byte_count) {
    return SDL_ReadIO((SDL_IOStream*)user, buffer, byte_count);
}

static bool tinydds_seek_callback(void* user, int64_t offset) {
    return SDL_SeekIO((SDL_IOStream*)user, offset, SDL_IO_SEEK_SET) >= 0;
}

static int64_t tinydds_tell_callback(void* user) {
    return SDL_TellIO((SDL_IOStream*)user);
}

static bool load_dds_image(const char* input_filename, image& img) {
    SDL_IOStream* file = SDL_IOFromFile(input_filename, "rb");
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't open DDS file %s", input_filename);
        return false;
    }

    TinyDDS_Callbacks callbacks;
    callbacks.errorFn = tinydds_error_callback;
    callbacks.allocFn = tinydds_alloc_callback;
    callbacks.freeFn = tinydds_free_callback;
    callbacks.readFn = tinydds_read_callback;
    callbacks.seekFn = tinydds_seek_callback;
    callbacks.tellFn = tinydds_tell_callback;

    TinyDDS_ContextHandle dds = TinyDDS_CreateContext(&callbacks, file);
    if (!dds) {
        SDL_CloseIO(file);
        return false;
    }

    if (!TinyDDS_ReadHeader(dds)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed parsing DDS header in file %s", input_filename);
        TinyDDS_DestroyContext(dds);
        SDL_CloseIO(file);
        return false;
    }

    if (!TinyDDS_Is2D(dds) || TinyDDS_ArraySlices(dds) > 1 || TinyDDS_IsCubemap(dds)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDS arrays, cubemaps, and 3D textures are not supported for Basis compression: %s", input_filename);
        TinyDDS_DestroyContext(dds);
        SDL_CloseIO(file);
        return false;
    }

    const uint32_t width = TinyDDS_Width(dds);
    const uint32_t height = TinyDDS_Height(dds);
    const uint8_t* src = (const uint8_t*)TinyDDS_ImageRawData(dds, 0);
    const uint32_t size = TinyDDS_ImageSize(dds, 0);
    const TinyDDS_Format format = TinyDDS_GetFormat(dds);
    if (!width || !height || !src || !size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DDS has no usable base image: %s", input_filename);
        TinyDDS_DestroyContext(dds);
        SDL_CloseIO(file);
        return false;
    }

    image decoded(width, height);
    bool ok = false;
    if (is_dds_compressed(format)) {
        ok = decode_dds_compressed(format, src, width, height, size, decoded);
    } else {
        ok = decode_dds_uncompressed(format, src, width, height, size, decoded);
    }

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unsupported DDS format %d for Basis compression: %s", (int)format, input_filename);
        TinyDDS_DestroyContext(dds);
        SDL_CloseIO(file);
        return false;
    }

    img = decoded;
    TinyDDS_DestroyContext(dds);
    SDL_CloseIO(file);
    return true;
}

static bool load_input_image(const char* input_filename, image& img) {
    if (has_extension(input_filename, "dds")) {
        return load_dds_image(input_filename, img);
    }

    int w, h, ch;
    unsigned char* data = stbi_load(input_filename, &w, &h, &ch, 4);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "stbi_load failed: %s", stbi_failure_reason());
        return false;
    }

    image decoded(w, h);
    memcpy(decoded.get_ptr(), data, (size_t)w * h * 4);
    stbi_image_free(data);

    img = decoded;
    return true;
}

static uint32_t get_worker_count(void) {
    int cores = SDL_GetNumLogicalCPUCores();
    if (cores < 1) cores = 1;
    return (uint32_t)cores;
}

int basis_encoder_init(void) {
    if (!g_encoder_initialized) {
        basisu_encoder_init();
        g_encoder_initialized = true;
    }
    return 0;
}

// Determine compression format (ETC1S or UASTC) and quality/effort settings
static bool setup_format_params(basis_compressor_params& params,
                                unsigned int flags,
                                int quality_level,
                                int effort_level) {
    bool use_etc1s = (flags & BASIS_FORMAT_ETC1S) != 0;
    bool use_uastc = (flags & BASIS_FORMAT_UASTC) != 0;
    if (!use_etc1s && !use_uastc) {
        // Default to UASTC (same as original command line behaviour for non-metallic)
        use_uastc = true;
    }

    if (use_etc1s && use_uastc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Cannot specify both ETC1S and UASTC.");
        return false;
    }

    if (use_etc1s) {
        params.set_format_mode(basist::basis_tex_format::cETC1S);
        // ETC1S quality: quality_level [1,255] (or 0? API says 1..255)
        if (quality_level >= 0) {
            params.m_quality_level = quality_level;
        } else {
            params.m_quality_level = BASISU_DEFAULT_QUALITY; // 128
        }
        // ETC1S effort: effort_level maps to compression_level (0..BASISU_MAX_ETC1S_COMPRESSION_LEVEL)
        if (effort_level >= 0) {
            // Map effort 0..10 to 0..BASISU_MAX_ETC1S_COMPRESSION_LEVEL (which is 3 as of 2.10)
            const int max_etc1s_effort = BASISU_MAX_ETC1S_COMPRESSION_LEVEL; // = 3
            int comp_level = (effort_level * max_etc1s_effort) / 10;
            params.m_etc1s_compression_level = comp_level;
        } else {
            params.m_etc1s_compression_level = 2; // good default
        }
    } else { // UASTC (including HDR detection? We assume LDR for now)
        // For LDR 4x4 UASTC (the most common)
        params.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
        // Quality: UASTC pack level (0..4) map quality_level (0..100) to 0..4
        if (quality_level >= 0) {
            int pack_level = (quality_level * 4) / 100;
            if (pack_level < 0) pack_level = 0;
            if (pack_level > 4) pack_level = 4;
            // The pack flags control effort/quality for UASTC encoding
            // We'll use the standard flags from basisu_comp.h
            static const uint32_t pack_flags[] = {
                cPackUASTCLevelFastest,
                cPackUASTCLevelFaster,
                cPackUASTCLevelDefault,
                cPackUASTCLevelSlower,
                cPackUASTCLevelVerySlow
            };
            params.m_pack_uastc_ldr_4x4_flags = pack_flags[pack_level];
        } else {
            params.m_pack_uastc_ldr_4x4_flags = cPackUASTCLevelDefault;
        }

        params.m_rdo_uastc_ldr_4x4 = false;
    }
    return true;
}

// Convert flags and quality/effort to basis_compressor_params
static bool setup_params(basis_compressor_params& params,
                         const char* input_filename,
                         const char* output_filename,
                         unsigned int flags,
                         int mip_smallest_dim,
                         int quality_level,
                         int effort_level) {
    // Clear and set basic output
    params.clear();
    params.m_out_filename = output_filename;
    params.m_write_output_basis_or_ktx2_files = true;  // let the compressor write directly
    params.m_create_ktx2_file = false;                 // we want .basis, not KTX2

    image img;
    if (!load_input_image(input_filename, img)) {
        return false;
    }

    if (flags & BASIS_FLAG_NORMAL_MAP) {
        for (uint32_t y = 0; y < img.get_height(); y++) {
            for (uint32_t x = 0; x < img.get_width(); x++) {
                color_rgba& p = img(x, y);
                p.a = p.g; // Basis BC5 transcodes R+A into the two BC4 blocks.
            }
        }
    } else if (flags & BASIS_FLAG_METALLIC_ROUGHNESS) {
        for (uint32_t y = 0; y < img.get_height(); y++) {
            for (uint32_t x = 0; x < img.get_width(); x++) {
                color_rgba& p = img(x, y);
                const uint8_t metallic = p.b;
                const uint8_t roughness = p.g;
                p.r = metallic;
                p.a = roughness; // Basis BC5 expects the second channel in alpha.
                p.g = roughness;
                p.b = 0;
            }
        }
    }
    params.m_source_images.push_back(img);

    // Flags handling
    params.m_y_flip = ((flags & BASIS_FLAG_Y_FLIP) != 0);

    // Normal map: renormalize normals, disable perceptual (linear) metrics
    if (flags & BASIS_FLAG_NORMAL_MAP) {
        params.m_renormalize = true;
        params.m_perceptual = false;
        params.m_ktx2_and_basis_srgb_transfer_function = false;
        // Also avoid sRGB filtering for mipmaps
        params.m_mip_srgb = false;
    } else {
        // Default: treat as sRGB (perceptual, sRGB transfer function)
        params.m_perceptual = true;
        params.m_ktx2_and_basis_srgb_transfer_function = true;
        params.m_mip_srgb = true;
    }

    // Mipmap generation
    if (!(flags & BASIS_FLAG_NO_MIPMAPS)) {
        params.m_mip_gen = true;
        params.m_mip_smallest_dimension = mip_smallest_dim;
        // Use a good default filter
        params.m_mip_filter = "kaiser";
        params.m_mip_wrapping = true;
        params.m_mip_fast = false;   // better quality mipmaps
    } else {
        params.m_mip_gen = false;
    }

    if (!setup_format_params(params, flags, quality_level, effort_level))
        return false;

    // Multithreading: enable by default if we have a job pool.
    params.m_multithreading = true;
    // Status output: match original code (prints to console)
    params.m_status_output = true;
    params.m_print_stats = false;   // avoid extra clutter
    return true;
}

// -----------------------------------------------------------------------------
// Public C API
// -----------------------------------------------------------------------------
int basis_compress_file(const char* input_filename,
                        const char* output_filename,
                        unsigned int textureFlags,
                        int mip_smallest_dim,
                        int quality_level,
                        int effort_level) 
{
    if (!g_encoder_initialized) {
        if (basis_encoder_init() != 0) {
            return -100;
        }
    }
    if (input_filename == NULL)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compression failed for %s input_filename null", output_filename);
        return 0;
    }
    // Check if this is a metallic/roughness texture (bit 1 set)
    int isMetallicRoughness = (textureFlags & 2) != 0;
    int isNormal = (textureFlags & 1) != 0;
    unsigned int flags = 0;
    if (isMetallicRoughness)
        flags |= BASIS_FORMAT_ETC1S;   // Use ETC1S for metallic/roughness (higher compression)
    else
        flags |= BASIS_FORMAT_UASTC;   // Use UASTC for other textures (higher quality)
        
    if (isNormal)
        flags |= BASIS_FLAG_NORMAL_MAP;
    if (isMetallicRoughness)
        flags |= BASIS_FLAG_METALLIC_ROUGHNESS;

    basis_compressor_params params;
    if (!setup_params(params, input_filename, output_filename, flags,
                      mip_smallest_dim, quality_level, effort_level)) {
        return -1;  // parameter setup failed
    }

    // Create a job pool for threading (optional but recommended)
    job_pool jpool(params.m_multithreading ? get_worker_count() : 1);
    params.m_pJob_pool = &jpool;

    basis_compressor compressor;
    if (!compressor.init(params)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compressor init failed for %s", input_filename);
        return -2;
    }

    basis_compressor::error_code err = compressor.process();
    if (err != basis_compressor::cECSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compression failed for %s with error %d", input_filename, (int)err);
        return -3;
    }

    return 0; // success
}

int basis_compress_array_memory(const unsigned char* const* layer_mips,
                                int num_layers,
                                int num_mips,
                                int width,
                                int height,
                                unsigned int flags,
                                int quality_level,
                                int effort_level,
                                const char* output_filename)
{
    if (!g_encoder_initialized) {
        if (basis_encoder_init() != 0) {
            return -100;
        }
    }
    if (!layer_mips || num_layers < 1 || num_mips < 1 || width < 1 || height < 1 || !output_filename) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "basis_compress_array_memory: invalid arguments for %s",
                output_filename ? output_filename : "(null)");
        return -1;
    }

    basis_compressor_params params;
    params.clear();
    params.m_out_filename = output_filename;
    params.m_write_output_basis_or_ktx2_files = true;
    params.m_create_ktx2_file = false;
    params.m_tex_type = basist::cBASISTexType2DArray;

    // mips are caller supplied, never generated, and the data keeps the channel layout
    // it had inside the source basis files: no swizzling, no renormalization
    params.m_mip_gen = false;
    params.m_y_flip = false;
    params.m_renormalize = false;

    bool linear = (flags & (BASIS_FLAG_NORMAL_MAP | BASIS_FLAG_METALLIC_ROUGHNESS)) != 0;
    params.m_perceptual = !linear;
    params.m_ktx2_and_basis_srgb_transfer_function = !linear;
    params.m_mip_srgb = !linear;

    if (!setup_format_params(params, flags, quality_level, effort_level))
        return -1;

    for (int layer = 0; layer < num_layers; layer++) {
        const unsigned char* mip0 = layer_mips[(size_t)layer * num_mips];
        if (!mip0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "basis_compress_array_memory: layer %d mip 0 is null", layer);
            return -1;
        }
        image img(width, height);
        memcpy(img.get_ptr(), mip0, (size_t)width * height * 4);
        params.m_source_images.push_back(img);

        basisu::vector<image> mips;
        for (int m = 1; m < num_mips; m++) {
            const unsigned char* data = layer_mips[(size_t)layer * num_mips + m];
            int mw = width >> m;  if (mw < 1) mw = 1;
            int mh = height >> m; if (mh < 1) mh = 1;
            if (!data) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "basis_compress_array_memory: layer %d mip %d is null", layer, m);
                return -1;
            }
            image mip(mw, mh);
            memcpy(mip.get_ptr(), data, (size_t)mw * mh * 4);
            mips.push_back(mip);
        }
        if (num_mips > 1)
            params.m_source_mipmap_images.push_back(mips);
    }

    params.m_multithreading = true;
    params.m_status_output = true;
    params.m_print_stats = false;

    job_pool jpool(get_worker_count());
    params.m_pJob_pool = &jpool;

    basis_compressor compressor;
    if (!compressor.init(params)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compressor init failed for %s", output_filename);
        return -2;
    }

    basis_compressor::error_code err = compressor.process();
    if (err != basis_compressor::cECSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compression failed for %s with error %d", output_filename, (int)err);
        return -3;
    }

    return 0;
}
