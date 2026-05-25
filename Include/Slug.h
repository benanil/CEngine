#ifndef CP_SLUG_H
#define CP_SLUG_H

#include "Graphics.h"
#include "DataStructures/HashMap.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SLUG_MAX_GLYPHS         128u
#define SLUG_MAX_TEXT           8196u
#define SLUG_VERTS_PER_GLYPH    6u
#define SLUG_MAX_FALLBACK_FONTS 8u
#define SLUG_MAX_BATCHES        256u

typedef struct SlugVertex_
{
    f32 pos[4];
    f32 tex[4];
    f32 jac[4];
    f32 band[4];
    u32 color;
    f32 z;
} SlugVertex;

typedef struct SlugBatch_
{
    u32 firstVertex;
    u32 vertexCount;
    f32 clip[4];
} SlugBatch;

typedef struct SlugGlyph_
{
    f32 x0, y0, x1, y1;
    f32 advance;
    u32 glyphBandEntry;
    u32 bandMaxX, bandMaxY;
    f32 bandScaleX, bandScaleY;
    f32 bandOffsetX, bandOffsetY;
} SlugGlyph;

typedef struct SlugBuildBuffers_
{
    u32* curves;
    u32  numCurveWords;
    u32  maxCurveWords;
    u32* bands;
    u32  numBands;
    u32  maxBands;
} SlugBuildBuffers;

typedef struct SlugFallbackFont_
{
    char* ttfData;
    u32 ttfSize;
    f32 emScale;
    s32 fontOffset;
    s32 fontIndex;
} SlugFallbackFont;

typedef struct SlugFont_
{
    SlugGlyph glyphs[SLUG_MAX_GLYPHS];
    HashMap unicodeGlyphs;
    char* ttfData;
    u32 ttfSize;
    f32 emScale;
    SlugFallbackFont fallbackFonts[SLUG_MAX_FALLBACK_FONTS];
    u32 numFallbackFonts;
    f32 ascent, descent;
    SlugBuildBuffers buffers;
    SDL_GPUBuffer* curveBuffer;
    SDL_GPUBuffer* bandBuffer;
    u32 gpuCurveWords;
    u32 gpuBandWords;
    SDL_GPUBuffer* vertexBuffer;
    SlugVertex* vertices;
    u32 numVertices;
    u32 maxVertices;
    SlugBatch batches[SLUG_MAX_BATCHES];
    u32 numBatches;
    bool glyphBuffersDirty;
    bool ownsFontData;
} SlugFont;

bool SlugLoadFont(SlugFont* font, const char* path);
void SlugDestroyFont(SlugFont* font);
void SlugClear(SlugFont* font);
bool SlugAppendText2DN(SlugFont* font, const char* text, u32 textBytes, float2 pos, f32 size, u32 color);
bool SlugAppendText2D(SlugFont* font, const char* text, float2 pos, f32 size, u32 color);
bool SlugAppendText3D(SlugFont* font, const char* text, float3 pos, Quaternion rot, f32 size, u32 color);
bool SlugAppendGlyph2D(SlugFont* font, u32 faceIndex, u32 glyphIndex, float2 pos, f32 size, u32 color);
float2 SlugCalcTextSize(SlugFont* font, const char* text, f32 size);
float2 SlugCalcTextSizeN(SlugFont* font, const char* text, u32 bytes, f32 size);
u32 SlugGetFontFaceCount(SlugFont* font);
void* SlugGetFontFaceData(SlugFont* font, u32 faceIndex, u32* size);
f32 SlugGetFontFaceEmScale(SlugFont* font, u32 faceIndex);
s32 SlugGetFontFaceCollectionIndex(SlugFont* font, u32 faceIndex);
f32 SlugGetFontAscent(SlugFont* font);
f32 SlugGetFontDescent(SlugFont* font);
void SlugRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, SlugFont* font, mat4x4 viewProj);
void SlugRender2D(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SlugFont* font);
SlugFont* SlugGetDemoFont(void);

void SlugInitDemo(void);
void SlugDestroyDemo(void);
void RenderSlugDemo(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);

#if defined(__cplusplus)
}
#endif

#endif
