#include "BasisCompressWrapper.h"

#include "basis_universal/encoder/basisu_comp.h"          // Main BasisU encoder API
#include "basis_universal/encoder/basisu_enc.h"         // For load_image()
#include "basis_universal/transcoder/basisu_file_headers.h"  // For basis_tex_format

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

    // Load source image using BasisU's built-in loader (supports PNG, JPG, etc.)
    image img;
    if (!load_image(input_filename, img)) {
        fprintf(stderr, "Failed to load image: %s\n", input_filename);
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

    // Determine compression format (ETC1S or UASTC)
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
        // Quality: UASTC pack level (0..4) � map quality_level (0..100) to 0..4
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
