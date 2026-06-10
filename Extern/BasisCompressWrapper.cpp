#include "BasisCompressWrapper.h"

#include "basis_universal/encoder/basisu_comp.h"          // Main BasisU encoder API
#include "basis_universal/encoder/basisu_enc.h"         // For load_image()
#include "basis_universal/transcoder/basisu_file_headers.h"  // For basis_tex_format

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stb/stb_image.h"

using namespace basisu;

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
static bool g_encoder_initialized = false;

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
        fprintf(stderr, "Error: Cannot specify both ETC1S and UASTC.\n");
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

    int w, h, ch;
    unsigned char* data = stbi_load(input_filename, &w, &h, &ch, 4);
    if (!data) {
        fprintf(stderr, "stbi_load failed: %s\n", stbi_failure_reason());
        return false;
    }

    image img(w, h);
    memcpy(img.get_ptr(), data, w * h * 4);
    stbi_image_free(data);

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
        fprintf(stderr, "Compression failed for %s input_filename null", output_filename);
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
    job_pool jpool(params.m_multithreading ? std::thread::hardware_concurrency() : 1);
    params.m_pJob_pool = &jpool;

    basis_compressor compressor;
    if (!compressor.init(params)) {
        fprintf(stderr, "Compressor init failed for %s\n", input_filename);
        return -2;
    }

    basis_compressor::error_code err = compressor.process();
    if (err != basis_compressor::cECSuccess) {
        fprintf(stderr, "Compression failed for %s with error %d\n", input_filename, (int)err);
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
        fprintf(stderr, "basis_compress_array_memory: invalid arguments for %s\n",
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
            fprintf(stderr, "basis_compress_array_memory: layer %d mip 0 is null\n", layer);
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
                fprintf(stderr, "basis_compress_array_memory: layer %d mip %d is null\n", layer, m);
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

    job_pool jpool(std::thread::hardware_concurrency());
    params.m_pJob_pool = &jpool;

    basis_compressor compressor;
    if (!compressor.init(params)) {
        fprintf(stderr, "Compressor init failed for %s\n", output_filename);
        return -2;
    }

    basis_compressor::error_code err = compressor.process();
    if (err != basis_compressor::cECSuccess) {
        fprintf(stderr, "Compression failed for %s with error %d\n", output_filename, (int)err);
        return -3;
    }

    return 0;
}
