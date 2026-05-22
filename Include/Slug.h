#ifndef CP_SLUG_H
#define CP_SLUG_H

#include "Graphics.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SLUG_MAX_GLYPHS 128u
#define SLUG_MAX_TEXT   1024u
#define SLUG_VERTS_PER_GLYPH 6u

typedef struct SlugVertex_
{
    f32 pos[4];
    f32 tex[4];
    f32 jac[4];
    f32 band[4];
    u32 color;
    f32 z;
} SlugVertex;

typedef struct SlugGlyph_
{
    f32 x0, y0, x1, y1;
    f32 advance;
    u32 glyphBandEntry;
    u32 bandMaxX, bandMaxY;
    f32 bandScaleX, bandScaleY;
    f32 bandOffsetX, bandOffsetY;
} SlugGlyph;

typedef struct SlugFont_
{
    SlugGlyph glyphs[SLUG_MAX_GLYPHS];
    f32 ascent, descent;
    SDL_GPUBuffer* curveBuffer;
    SDL_GPUBuffer* bandBuffer;
    SDL_GPUBuffer* vertexBuffer;
    SlugVertex* vertices;
    u32 numVertices;
    u32 maxVertices;
} SlugFont;

bool SlugLoadFont(SlugFont* font, const char* path);
void SlugDestroyFont(SlugFont* font);
void SlugClear(SlugFont* font);
bool SlugAppendText(SlugFont* font, const char* text, float3 pos, f32 size, u32 color);
void SlugRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, SlugFont* font, mat4x4 viewProj);

void SlugInitDemo(void);
void SlugDestroyDemo(void);
void RenderSlugDemo(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj);

#if defined(__cplusplus)
}
#endif

#endif
