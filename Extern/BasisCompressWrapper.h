#ifndef BASIS_COMPRESSOR_C_H
#define BASIS_COMPRESSOR_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compression format flags
#define BASIS_FORMAT_ETC1S  0x01   // Use ETC1S (high compression, lower quality)
#define BASIS_FORMAT_UASTC  0x02   // Use UASTC (higher quality, larger files)
// If neither flag is given, UASTC is used by default.

// Other flags
#define BASIS_FLAG_NORMAL_MAP     0x04   // Input is a normal map (disable sRGB, renormalize)
#define BASIS_FLAG_NO_MIPMAPS     0x08   // Disable automatic mipmap generation
#define BASIS_FLAG_Y_FLIP         0x10   // Flip image vertically before compression
#define BASIS_FLAG_METALLIC_ROUGHNESS 0x20

// Quality preset for fast/slow trade-off (used when quality level is not set)
#define BASIS_QUALITY_FASTEST     0
#define BASIS_QUALITY_FAST        1
#define BASIS_QUALITY_DEFAULT     2
#define BASIS_QUALITY_SLOW        3
#define BASIS_QUALITY_VERY_SLOW   4

/**
 * Initializes the Basis Universal encoder library.
 * Must be called once before any compression functions.
 * @return 0 on success, non-zero on failure.
 */
int basis_encoder_init(void);

/**
 * Compresses an image file (PNG, JPG, TGA, BMP, etc.) to a .basis file.
 *
 * @param input_filename    Path to the input image.
 * @param output_filename   Path where the .basis file will be written.
 * @param flags             Bitwise OR of BASIS_FORMAT_* and BASIS_FLAG_*.
 *                          If neither BASIS_FORMAT_ETC1S nor BASIS_FORMAT_UASTC is given,
 *                          UASTC is used by default.
 * @param mip_smallest_dim  Smallest dimension for the mip chain (e.g., 256).
 *                          Ignored if BASIS_FLAG_NO_MIPMAPS is set.
 * @param quality_level     Compression quality (0..255 for ETC1S, 0..100 for UASTC).
 *                          Use -1 for library default.
 * @param effort_level      Compression effort/speed (0..10). Use -1 for default.
 * @return 0 on success, non-zero error code on failure.
 */
int basis_compress_file(const char* input_filename,
                        const char* output_filename,
                        unsigned int flags,
                        int mip_smallest_dim,
                        int quality_level,
                        int effort_level);

#ifdef __cplusplus
}
#endif

#endif /* BASIS_COMPRESSOR_C_H */
