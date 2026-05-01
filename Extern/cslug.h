#ifndef cslug_H
#define cslug_H

#include "stb_truetype.h"

typedef          int    cslug_i32;
typedef unsigned int    cslug_u32;
typedef unsigned short  cslug_u16;
typedef float           cslug_f32;

// If defined, removes the need to link to compiler-rt
#ifdef CSLUG_IMPL_F16
typedef cslug_u16       cslug_f16;
#else
typedef _Float16        cslug_f16;
#endif
#define CSLUG_IMPLEMENTATION

typedef struct {
    cslug_f32 x0, y0, x1, y1;          // em-space bounding box
    cslug_f32 advance;                 // advance width in em-space
    cslug_u32 n_hbands, n_vbands;
    cslug_u32 glyph_band_texel;        // location in a 1D band texture
    cslug_u32 band_max_x, band_max_y;
    cslug_f32 band_scale_x, band_scale_y;
    cslug_f32 band_offset_x, band_offset_y;
} cslug_glyph;

typedef struct {
    cslug_f32 p1x, p1y, p2x, p2y, p3x, p3y;
} cslug_curve;

typedef struct {
    cslug_f16 *ptr;
    cslug_u32 len;
    cslug_u32 capacity;
} cslug_buf_f16;

typedef struct {
    cslug_u16 *ptr;
    cslug_u32 len;
    cslug_u32 capacity;
} cslug_buf_u16;

typedef struct {
    cslug_buf_f16 curves;
    cslug_buf_f16 bands;
} cslug_buffers;

typedef struct {
    cslug_buf_f16 curves;
    cslug_buf_u16 bands;
} cslug_buffers_packed;

#ifdef CSLUG_STATIC
#define CSLUG_DEF static
#else
#define CSLUG_DEF extern
#endif

CSLUG_DEF void cslug_free_buffers(stbtt_fontinfo *info, cslug_buffers *buffers);
CSLUG_DEF void cslug_free_buffers_packed(stbtt_fontinfo *info, cslug_buffers_packed *buffers);

// For R16G16B16A16 textures: curves = 8 f16/curve (2 texels), bands = 4 f16/entry (1 texel)
// Curve index divisor = 4, band index divisor = 4
CSLUG_DEF void cslug_build_glyph_for_texture(stbtt_fontinfo *info, cslug_u32 code_point, cslug_f32 em_scale,
                                             cslug_buffers *buffers, cslug_glyph *glyph);

// For tightly packed buffers: curves = 6 f16/curve, bands = 2 u16/entry
// Curve index divisor = 3, band index divisor = 2
CSLUG_DEF void cslug_build_glyph_for_buffer(stbtt_fontinfo *info, cslug_u32 code_point, cslug_f32 em_scale,
                                            cslug_buffers_packed *buffers, cslug_glyph *glyph);

#ifdef CSLUG_IMPLEMENTATION

#define CSLUG_EPS (1.0f / 1024.0f)

#ifndef CSLUG_fminf
#include <math.h>
#define CSLUG_fminf(a, b) fminf(a, b)
#endif

#ifndef CSLUG_fmaxf
#include <math.h>
#define CSLUG_fmaxf(a, b) fmaxf(a, b)
#endif

#define cslug_u32_to_u16(cslug_u32 v) (cslug_u16)(v)

CSLUG_DEF void cslug_free_buffers(stbtt_fontinfo *info, cslug_buffers *buffers) {
    STBTT_free(buffers->curves.ptr, info->userdata);
    STBTT_free(buffers->bands.ptr, info->userdata);
}

CSLUG_DEF void cslug_free_buffers_packed(stbtt_fontinfo *info, cslug_buffers_packed *buffers) {
    STBTT_free(buffers->curves.ptr, info->userdata);
    STBTT_free(buffers->bands.ptr, info->userdata);
}

static void cslug_buf_ensure_capacity(stbtt_fontinfo *info, cslug_buf_f16 *buf, cslug_u32 additional) {
    cslug_u32 minimum_capacity = buf->len + additional;
    if (buf->capacity < minimum_capacity)
    {
        cslug_u32 new_capacity = buf->capacity == 0 ? 128 : buf->capacity;
        while (new_capacity < minimum_capacity) new_capacity *= 2;

        cslug_f16 *new_ptr = (cslug_f16*)STBTT_malloc(new_capacity * sizeof(cslug_f16) + 32, info->userdata);
        if (buf->ptr) {
            STBTT_memcpy(new_ptr, buf->ptr, buf->len * sizeof(cslug_f16));
            STBTT_free(buf->ptr, info->userdata);
        }
        buf->ptr = new_ptr;
        buf->capacity = new_capacity;
    }
}

static void cslug_buf_add_curve(cslug_buf_f16 *buf, cslug_curve curve, cslug_u32 n) {
    Float4ToHalf4(buf->ptr + buf->len, (const cslug_f32*)&curve);
    *(u32*)(buf->ptr + buf->len + 4) = Float2ToHalf2((const cslug_f32*)&curve + 4);
    buf->len += n;
}

static cslug_f32 cslug_fmin3(cslug_f32 a, cslug_f32 b, cslug_f32 c) { return CSLUG_fminf(CSLUG_fminf(a, b), c); }
static cslug_f32 cslug_fmax3(cslug_f32 a, cslug_f32 b, cslug_f32 c) { return CSLUG_fmaxf(CSLUG_fmaxf(a, b), c); }

static cslug_u32 cslug_is_horizontal(const cslug_curve *c) {
    return fabsf(c->p1y - c->p2y) < CSLUG_EPS && fabsf(c->p2y - c->p3y) < CSLUG_EPS;
}

static cslug_u32 cslug_is_vertical(const cslug_curve *c) {
    return fabsf(c->p1x - c->p2x) < CSLUG_EPS && fabsf(c->p2x - c->p3x) < CSLUG_EPS;
}

// for small arrays it is good enough
static void cslug_sort_descend(cslug_u32 *idx, cslug_f32 *m, cslug_u32 cnt) {
    if (cnt) {
      for (cslug_u32 i = 0; i < cnt - 1; i++) {
          for (cslug_u32 j = i + 1; j < cnt; j++) {
              if (m[i] < m[j]) {
                  cslug_u32 ti = idx[i]; idx[i] = idx[j]; idx[j] = ti;
                  cslug_f32 tf = m[i]; m[i] = m[j]; m[j] = tf;
              }
          }
       }
    }
}

static cslug_u32 cslug_extract_curves(stbtt_fontinfo *info, cslug_u32 glyph_index, cslug_f32 em_scale,
                                    cslug_curve **curves) {
    stbtt_vertex *vertices;
    cslug_u32 n_vertices = stbtt_GetGlyphShape(info, glyph_index, &vertices);
    *curves = (cslug_curve*)STBTT_malloc(n_vertices * sizeof(cslug_curve), info->userdata);
    cslug_u32 n_curves = 0;
    cslug_f32 px = 0, py = 0;

    for (cslug_u32 i = 0; i < n_vertices; i++) {
        cslug_f32 x = vertices[i].x * em_scale;
        cslug_f32 y = vertices[i].y * em_scale;
        switch (vertices[i].type) {
        case STBTT_vmove:
            px = x; py = y;
            break;
        case STBTT_vline:
            (*curves)[n_curves++] = (cslug_curve){ px, py, x, y, x, y };
            px = x; py = y;
            break;
        case STBTT_vcurve: {
            cslug_f32 cx = vertices[i].cx * em_scale, cy = vertices[i].cy * em_scale;
            (*curves)[n_curves++] = (cslug_curve){ px, py, cx, cy, x, y };
            px = x; py = y;
            break;
        }
        default:
            px = x; py = y;
            break;
        }
    }
    stbtt_FreeShape(info, vertices);
    return n_curves;
}

CSLUG_DEF void cslug_build_glyph_for_buffer(stbtt_fontinfo *info, cslug_u32 code_point, cslug_f32 em_scale,
                                            cslug_buffers_packed *buffers, cslug_glyph *glyph) {
    const int CURVE_STRIDE = 6;
    const int BAND_STRIDE  = 2;
    *glyph = (cslug_glyph){0};                                                                        
    cslug_u32 gi = stbtt_FindGlyphIndex(info, code_point);                                            
                                                                                                      
    cslug_i32 adv, lsb;                                                                               
    stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);                                                     
    glyph->advance = adv * em_scale;                                                                  
                                                                                                      
    cslug_i32 ix0, iy0, ix1, iy1;                                                                     
    if (!stbtt_GetGlyphBox(info, gi, &ix0, &iy0, &ix1, &iy1)) return;                                 
                                                                                                      
    glyph->x0 = ix0 * em_scale;                                                                       
    glyph->y0 = iy0 * em_scale;                                                                       
    glyph->x1 = ix1 * em_scale;                                                                       
    glyph->y1 = iy1 * em_scale;                                                                       
                                                                                                      
    cslug_curve *curves;                                                                              
    cslug_u32 n_curves = cslug_extract_curves(info, gi, em_scale, &curves);                           
    if (n_curves == 0) return;                                                                        
                                                                                                      
    cslug_buf_ensure_capacity(info, &buffers->curves, n_curves * (CURVE_STRIDE));                     
                                                                                                      
    cslug_u32 n_hbands = (cslug_u32)fmaxf(1, fminf(16, ceilf(sqrtf((cslug_f32)n_curves))));           
    cslug_u32 n_vbands = n_hbands;                                                                    
    cslug_u32 total_bands = n_hbands + n_vbands;                                                      
                                                                                                      
    /* f16 and u16 have same size, so cast is fine */                                                 
    cslug_buf_ensure_capacity(info, (cslug_buf_f16*)&buffers->bands,                                  
                              (total_bands + total_bands * n_curves) * (BAND_STRIDE));                
                                                                                                      
    cslug_u32 *curve_indexes = (cslug_u32*)STBTT_malloc(n_curves * sizeof(cslug_u32), info->userdata);
    for (cslug_u32 ci = 0; ci < n_curves; ci++) {                                                     
        curve_indexes[ci] = buffers->curves.len / ((CURVE_STRIDE) / 2);                               
        cslug_buf_add_curve(&buffers->curves, curves[ci], (CURVE_STRIDE));                            
    }                                                                                                 
                                                                                                      
    cslug_f32 glyph_width  = glyph->x1 - glyph->x0;                                                   
    cslug_f32 glyph_height = glyph->y1 - glyph->y0;                                                   
    cslug_f32 hband_size = glyph_height / n_hbands;                                                   
    cslug_f32 vband_size = glyph_width  / n_vbands;                                                   
                                                                                                      
    glyph->n_hbands = n_hbands;                                                                       
    glyph->n_vbands = n_vbands;                                                                       
    glyph->band_max_x = n_vbands - 1;                                                                 
    glyph->band_max_y = n_hbands - 1;                                                                 
    glyph->band_scale_x = (0.0f < glyph_width)  ? (cslug_f32)n_vbands / glyph_width  : 0.0f;          
    glyph->band_scale_y = (0.0f < glyph_height) ? (cslug_f32)n_hbands / glyph_height : 0.0f;          
    glyph->band_offset_x = -glyph->x0 * glyph->band_scale_x;                                          
    glyph->band_offset_y = -glyph->y0 * glyph->band_scale_y;                                          
                                                                                                      
    cslug_u32 band_hdr_offset = buffers->bands.len;                                                   
    glyph->glyph_band_texel = band_hdr_offset / (BAND_STRIDE);                                        
    buffers->bands.len += total_bands * (BAND_STRIDE);                                                
                                                                                                      
    void *tmp = (void*)STBTT_malloc(n_curves * 2 * sizeof(cslug_u32), info->userdata);                
    cslug_u32 *band_indexes  = (cslug_u32*)tmp;                                                       
    cslug_f32 *band_maximums = (cslug_f32*)((char*)tmp + n_curves * sizeof(cslug_u32));               
                                                                                                      
    for (cslug_u32 b = 0; b < n_hbands; b++) {                                                        
        cslug_f32 by0 = glyph->y0 + b * hband_size - CSLUG_EPS;                                       
        cslug_f32 by1 = glyph->y0 + (b + 1) * hband_size + CSLUG_EPS;                                 
        cslug_u32 cnt = 0;                                                                            
        for (cslug_u32 c = 0; c < n_curves; c++) {                                                    
            if (!cslug_is_horizontal(&curves[c])) {                                                   
                cslug_f32 ymin = cslug_fmin3(curves[c].p1y, curves[c].p2y, curves[c].p3y);            
                cslug_f32 ymax = cslug_fmax3(curves[c].p1y, curves[c].p2y, curves[c].p3y);            
                if (by0 <= ymax && ymin <= by1) {                                                     
                    band_indexes[cnt]  = c;                                                           
                    band_maximums[cnt] = cslug_fmax3(curves[c].p1x, curves[c].p2x, curves[c].p3x);    
                    cnt++;                                                                            
                }                                                                                     
            }                                                                                         
        }                                                                                             
        cslug_sort_descend(band_indexes, band_maximums, cnt);                                         
        cslug_u32 off = (buffers->bands.len - band_hdr_offset) / (BAND_STRIDE);                       
        cslug_u32 hi  = band_hdr_offset + b * (BAND_STRIDE);                                          
        buffers->bands.ptr[hi + 0] = cslug_u32_to_u16(cnt);                                           
        buffers->bands.ptr[hi + 1] = cslug_u32_to_u16(off);                                           
        for (cslug_u32 i = 0; i < cnt; i++) {                                                         
            cslug_u32 bi = buffers->bands.len;                                                        
            buffers->bands.ptr[bi + 0] = cslug_u32_to_u16(curve_indexes[band_indexes[i]]);            
            buffers->bands.ptr[bi + 1] = cslug_u32_to_u16(0);                                         
            for (cslug_u32 p = 2; p < (BAND_STRIDE); p++)                                             
                buffers->bands.ptr[bi + p] = cslug_u32_to_u16(0);                                     
            buffers->bands.len += (BAND_STRIDE);                                                      
        }                                                                                             
    }                                                                                                 
                                                                                                      
    for (cslug_u32 b = 0; b < n_vbands; b++) {                                                        
        cslug_f32 bx0 = glyph->x0 + b * vband_size - CSLUG_EPS;                                       
        cslug_f32 bx1 = glyph->x0 + (b + 1) * vband_size + CSLUG_EPS;                                 
        cslug_u32 cnt = 0;                                                                            
        for (cslug_u32 c = 0; c < n_curves; c++) {                                                    
            if (!cslug_is_vertical(&curves[c])) {                                                     
                cslug_f32 xmin = cslug_fmin3(curves[c].p1x, curves[c].p2x, curves[c].p3x);            
                cslug_f32 xmax = cslug_fmax3(curves[c].p1x, curves[c].p2x, curves[c].p3x);            
                if (bx0 <= xmax && xmin <= bx1) {                                                     
                    band_indexes[cnt]  = c;                                                           
                    band_maximums[cnt] = cslug_fmax3(curves[c].p1y, curves[c].p2y, curves[c].p3y);    
                    cnt++;                                                                            
                }                                                                                     
            }                                                                                         
        }                                                                                             
        cslug_sort_descend(band_indexes, band_maximums, cnt);                                         
        cslug_u32 off = (buffers->bands.len - band_hdr_offset) / (BAND_STRIDE);                       
        cslug_u32 hi  = band_hdr_offset + (n_hbands + b) * (BAND_STRIDE);                             
        buffers->bands.ptr[hi + 0] = cslug_u32_to_u16(cnt);                                           
        buffers->bands.ptr[hi + 1] = cslug_u32_to_u16(off);                                           
        for (cslug_u32 i = 0; i < cnt; i++) {                                                         
            cslug_u32 bi = buffers->bands.len;                                                        
            buffers->bands.ptr[bi + 0] = cslug_u32_to_u16(curve_indexes[band_indexes[i]]);            
            buffers->bands.ptr[bi + 1] = cslug_u32_to_u16(0);                                         
            for (cslug_u32 p = 2; p < (BAND_STRIDE); p++)                                             
                buffers->bands.ptr[bi + p] = cslug_u32_to_u16(0);                                     
            buffers->bands.len += (BAND_STRIDE);                                                      
        }                                                                                             
    }                                                                                                 
                                                                                                      
    STBTT_free(tmp, info->userdata);                                                                  
    STBTT_free(curves, info->userdata);                                                               
    STBTT_free(curve_indexes, info->userdata);
}

#endif // CSLUG_IMPLEMENTATION
#endif