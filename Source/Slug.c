// https://github.com/EricLengyel/Slug
// https://github.com/ShadowCurse/cslug
#define HMRealloc(mem, size) ReAllocateTLSFGlobal(mem, size)
#define HMFree(mem) DeAllocateTLSFGlobal(mem)
#define HMMemset(mem, val, size) SDL_memset(mem, val, size)
#define HMMemcpy(mem, src, size) MemCopy(mem, src, size)
#define HM_HASHMAP_IMPLEMENTATION
#include "RenderingInternal.h"
#include "Include/Slug.h"
#include "Include/FileSystem.h"
#include "Include/String.h"

#define STBTT_malloc(size, user) ((void)(user), AllocateTLSFGlobal(size))
#define STBTT_free(ptr, user)    ((void)(user), DeAllocateTLSFGlobal(ptr))
#define STB_TRUETYPE_IMPLEMENTATION
#include "Extern/stb/stb_truetype.h"

#include <math.h>

#define SLUG_EPS (1.0f / 1024.0f)
#define SLUG_INITIAL_GPU_WORDS (256u * 1024u)
#define SLUG_BOUNDS_PAD_PX 2.0f
#define SLUG_BOUNDS_PAD_EM (1.0f / 32.0f)
typedef struct SlugCurve_
{
    float2 p1, p2, p3;
} SlugCurve;

typedef struct SlugVertexParams_
{
    mat4x4 matrix;
    f32 viewport[4];
} SlugVertexParams;

static SlugFont g_SlugDemoFont;

static u32 SlugPackU16(u32 x, u32 y)
{
    return (x & 0xFFFFu) | ((y & 0xFFFFu) << 16u);
}

static void SlugEnsureU32Buffer(u32** ptr, u32* capacity, u32 needed)
{
    if (*capacity >= needed) return;
    u32 newCapacity = *capacity ? *capacity : 128u;
    while (newCapacity < needed) newCapacity *= 2u;
    *ptr = (u32*)ReAllocateTLSFGlobal(*ptr, (size_t)newCapacity * sizeof(u32));
    *capacity = newCapacity;
}

static void SlugAddCurve(SlugBuildBuffers* buffers, SlugCurve curve)
{
    SlugEnsureU32Buffer(&buffers->curves, &buffers->maxCurveWords, buffers->numCurveWords + 3u);
    Float4ToHalf4(&buffers->curves[buffers->numCurveWords], &curve.p1.x);
    buffers->curves[buffers->numCurveWords + 2u] = PackHalf2(curve.p3);
    buffers->numCurveWords += 3u;
}

static bool SlugCurveHorizontal(const SlugCurve* c)
{
    return Absf32(c->p1.y - c->p2.y) < SLUG_EPS && Absf32(c->p2.y - c->p3.y) < SLUG_EPS;
}

static bool SlugCurveVertical(const SlugCurve* c)
{
    return Absf32(c->p1.x - c->p2.x) < SLUG_EPS && Absf32(c->p2.x - c->p3.x) < SLUG_EPS;
}

static v128f SlugCurveX(const SlugCurve* c)
{
    return VecSetR(c->p1.x, c->p2.x, c->p3.x, 0.0f);
}

static v128f SlugCurveY(const SlugCurve* c)
{
    return VecSetR(c->p1.y, c->p2.y, c->p3.y, 0.0f);
}

static void SlugSortDescend(u32* indexes, f32* maximums, u32 count)
{
    for (u32 i = 0; i + 1u < count; i++)
    {
        for (u32 j = i + 1u; j < count; j++)
        {
            if (maximums[i] < maximums[j])
            {
                u32 ti = indexes[i]; indexes[i] = indexes[j]; indexes[j] = ti;
                f32 tf = maximums[i]; maximums[i] = maximums[j]; maximums[j] = tf;
            }
        }
    }
}

static void SlugAppendCubicApprox(SlugCurve* curves, u32* count, f32 p0x, f32 p0y, f32 c1x, f32 c1y, f32 c2x, f32 c2y, f32 p3x, f32 p3y)
{
    f32 p01x = (p0x + c1x) * 0.5f;
    f32 p01y = (p0y + c1y) * 0.5f;
    f32 p12x = (c1x + c2x) * 0.5f;
    f32 p12y = (c1y + c2y) * 0.5f;
    f32 p23x = (c2x + p3x) * 0.5f;
    f32 p23y = (c2y + p3y) * 0.5f;
    f32 p012x = (p01x + p12x) * 0.5f;
    f32 p012y = (p01y + p12y) * 0.5f;
    f32 p123x = (p12x + p23x) * 0.5f;
    f32 p123y = (p12y + p23y) * 0.5f;
    f32 midx = (p012x + p123x) * 0.5f;
    f32 midy = (p012y + p123y) * 0.5f;

    curves[(*count)++] = (SlugCurve){ { p0x, p0y }, { (3.0f * p01x - p0x) * 0.5f, (3.0f * p01y - p0y) * 0.5f }, { midx, midy } };
    curves[(*count)++] = (SlugCurve){ { midx, midy }, { (3.0f * p123x - p3x) * 0.5f, (3.0f * p123y - p3y) * 0.5f }, { p3x, p3y } };
}

static u32 SlugExtractCurves(stbtt_fontinfo* info, u32 glyphIndex, f32 emScale, SlugCurve** outCurves)
{
    stbtt_vertex* vertices = NULL;
    s32 numVertices = stbtt_GetGlyphShape(info, (int)glyphIndex, &vertices);
    if (numVertices <= 0)
    {
        *outCurves = NULL;
        return 0;
    }

    SlugCurve* curves = (SlugCurve*)AllocateTLSFGlobal((size_t)numVertices * 2u * sizeof(SlugCurve));
    u32 numCurves = 0;
    f32 px = 0.0f;
    f32 py = 0.0f;

    for (s32 i = 0; i < numVertices; i++)
    {
        f32 x = (f32)vertices[i].x * emScale;
        f32 y = (f32)vertices[i].y * emScale;
        switch (vertices[i].type)
        {
            case STBTT_vmove:
                px = x; py = y;
                break;
            case STBTT_vline:
                curves[numCurves++] = (SlugCurve){ { px, py }, { x, y }, { x, y } };
                px = x; py = y;
                break;
            case STBTT_vcurve:
            {
                f32 cx = (f32)vertices[i].cx * emScale;
                f32 cy = (f32)vertices[i].cy * emScale;
                curves[numCurves++] = (SlugCurve){ { px, py }, { cx, cy }, { x, y } };
                px = x; py = y;
            } break;
            case STBTT_vcubic:
            {
                f32 c1x = (f32)vertices[i].cx  * emScale;
                f32 c1y = (f32)vertices[i].cy  * emScale;
                f32 c2x = (f32)vertices[i].cx1 * emScale;
                f32 c2y = (f32)vertices[i].cy1 * emScale;
                SlugAppendCubicApprox(curves, &numCurves, px, py, c1x, c1y, c2x, c2y, x, y);
                px = x; py = y;
            } break;
            default:
                px = x; py = y;
                break;
        }
    }

    stbtt_FreeShape(info, vertices);
    *outCurves = curves;
    return numCurves;
}

static void SlugBuildGlyphByIndex(stbtt_fontinfo* info, u32 glyphIndex, f32 emScale, SlugBuildBuffers* buffers, SlugGlyph* glyph)
{
    *glyph = (SlugGlyph){0};

    s32 advance, lsb;
    stbtt_GetGlyphHMetrics(info, (int)glyphIndex, &advance, &lsb);
    glyph->advance = (f32)advance * emScale;

    s32 ix0, iy0, ix1, iy1;
    if (!stbtt_GetGlyphBox(info, (int)glyphIndex, &ix0, &iy0, &ix1, &iy1)) return;

    glyph->x0 = (f32)ix0 * emScale;
    glyph->y0 = (f32)iy0 * emScale;
    glyph->x1 = (f32)ix1 * emScale;
    glyph->y1 = (f32)iy1 * emScale;

    SlugCurve* curves = NULL;
    u32 numCurves = SlugExtractCurves(info, glyphIndex, emScale, &curves);
    if (numCurves == 0u)
    {
        AX_LOG("Slug glyph index %u has no curves", glyphIndex);
        return;
    }

    u32* curveIndexes = (u32*)ArenaPushGlobal((u64)numCurves * sizeof(u32));
    for (u32 i = 0; i < numCurves; i++)
    {
        curveIndexes[i] = buffers->numCurveWords / 3u;
        SlugAddCurve(buffers, curves[i]);
    }

    u32 numHBands = (u32)Maxf32(1.0f, Minf32(16.0f, Ceilf(Sqrtf((f32)numCurves))));
    u32 numVBands = numHBands;
    u32 totalBands = numHBands + numVBands;
    SlugEnsureU32Buffer(&buffers->bands, &buffers->maxBands, buffers->numBands + totalBands + totalBands * numCurves);

    f32 glyphWidth  = glyph->x1 - glyph->x0;
    f32 glyphHeight = glyph->y1 - glyph->y0;
    f32 hbandSize = glyphHeight / (f32)numHBands;
    f32 vbandSize = glyphWidth  / (f32)numVBands;

    glyph->bandMaxX = numVBands - 1u;
    glyph->bandMaxY = numHBands - 1u;
    glyph->bandScaleX = glyphWidth  > 0.0f ? (f32)numVBands / glyphWidth  : 0.0f;
    glyph->bandScaleY = glyphHeight > 0.0f ? (f32)numHBands / glyphHeight : 0.0f;
    glyph->bandOffsetX = -glyph->x0 * glyph->bandScaleX;
    glyph->bandOffsetY = -glyph->y0 * glyph->bandScaleY;

    u32 bandHeaderOffset = buffers->numBands;
    glyph->glyphBandEntry = bandHeaderOffset;
    buffers->numBands += totalBands;

    u32 mark = (u32)ArenaGetCurrentOffset();
    u32* bandIndexes = (u32*)ArenaPushGlobal((u64)numCurves * sizeof(u32));
    f32* bandMaximums = (f32*)ArenaPushGlobal((u64)numCurves * sizeof(f32));

    for (u32 b = 0; b < numHBands; b++)
    {
        f32 by0 = glyph->y0 + (f32)b * hbandSize - SLUG_EPS;
        f32 by1 = glyph->y0 + (f32)(b + 1u) * hbandSize + SLUG_EPS;
        u32 count = 0;
        for (u32 c = 0; c < numCurves; c++)
        {
            if (SlugCurveHorizontal(&curves[c])) continue;
            v128f curveY = SlugCurveY(&curves[c]);
            f32 ymin = Min3(curveY);
            f32 ymax = Max3(curveY);
            if (by0 <= ymax && ymin <= by1)
            {
                bandIndexes[count] = c;
                bandMaximums[count++] = Max3(SlugCurveX(&curves[c]));
            }
        }

        SlugSortDescend(bandIndexes, bandMaximums, count);
        u32 offset = buffers->numBands - bandHeaderOffset;
        buffers->bands[bandHeaderOffset + b] = SlugPackU16(count, offset);
        for (u32 i = 0; i < count; i++) buffers->bands[buffers->numBands++] = SlugPackU16(curveIndexes[bandIndexes[i]], 0u);
    }

    for (u32 b = 0; b < numVBands; b++)
    {
        f32 bx0 = glyph->x0 + (f32)b * vbandSize - SLUG_EPS;
        f32 bx1 = glyph->x0 + (f32)(b + 1u) * vbandSize + SLUG_EPS;
        u32 count = 0;
        for (u32 c = 0; c < numCurves; c++)
        {
            if (SlugCurveVertical(&curves[c])) continue;
            v128f curveX = SlugCurveX(&curves[c]);
            f32 xmin = Min3(curveX);
            f32 xmax = Max3(curveX);
            if (bx0 <= xmax && xmin <= bx1)
            {
                bandIndexes[count] = c;
                bandMaximums[count++] = Max3(SlugCurveY(&curves[c]));
            }
        }

        SlugSortDescend(bandIndexes, bandMaximums, count);
        u32 offset = buffers->numBands - bandHeaderOffset;
        buffers->bands[bandHeaderOffset + numHBands + b] = SlugPackU16(count, offset);
        for (u32 i = 0; i < count; i++) buffers->bands[buffers->numBands++] = SlugPackU16(curveIndexes[bandIndexes[i]], 0u);
    }

    ArenaSetCurrentOffset(mark);
    ArenaPopGlobal((u64)numCurves * sizeof(u32));
    DeAllocateTLSFGlobal(curves);
}

static void SlugBuildGlyph(stbtt_fontinfo* info, u32 codePoint, f32 emScale, SlugBuildBuffers* buffers, SlugGlyph* glyph)
{
    SlugBuildGlyphByIndex(info, (u32)stbtt_FindGlyphIndex(info, (int)codePoint), emScale, buffers, glyph);
}

static const u32 g_SlugUnicodeCodepoints[] = {
    0x00FCu, 0x00F6u, 0x00E7u, 0x011Fu, 0x015Fu, 0x0131u, 0x00E4u, 0x00DFu,
    0x00F1u, 0x00E5u, 0x00E2u, 0x00E1u, 0x00E6u, 0x00EAu, 0x0142u, 0x0107u,
    0x00F8u, 0x00DCu, 0x00D6u, 0x00C7u, 0x011Eu, 0x015Eu, 0x00C4u, 0x1E9Eu,
    0x00D1u, 0x00C5u, 0x00C2u, 0x00C1u, 0x00C6u, 0x00CAu, 0x0141u, 0x0106u,
    0x00D8u, 0x0130u, 0x21BAu, 0x23F0u, 0x2605u, 0x2764u, 0x2714u,
    0x23F3u, 0x23F4u, 0x23F5u, 0x23F6u, 0x23F7u, 0x23F8u, 0x23F9u, 0x23FAu,
    0x017Au, 0x017Bu, 0x017Cu, 0x017Eu, 0x0103u, 0x0105u, 0x0143u, 0x0144u,
    0x01F9u, 0x0119u, 0x0163u, 0x021Bu, 0x1E6Bu, 0x00F2u, 0x00F3u, 0x00F4u,
    0x00EEu, 0x00CCu, 0x00CDu, 0x00E9u, 0x00E8u, 0x00E0u
};

static const char* g_SlugFallbackFontPaths[] = {
    "Assets/Fonts/simsun.ttc",
    "Assets/Fonts/YuGothR.ttc",
    "Assets/Fonts/NotoSansArabic-Regular.ttf",
};

static bool SlugLoadFallbackFont(SlugFont* font, const char* path)
{
    if (font->numFallbackFonts >= SLUG_MAX_FALLBACK_FONTS) return false;
    u64 fileSize = FileSize(path);
    if (fileSize == 0u || fileSize > UINT32_MAX) return false;

    char* ttf = ReadAllFileAlloc(path);
    if (!ttf) return false;

    s32 fontCount = stbtt_GetNumberOfFonts((const unsigned char*)ttf);
    if (fontCount <= 0) fontCount = 1;
    bool loaded = false;
    for (s32 i = 0; i < fontCount && font->numFallbackFonts < SLUG_MAX_FALLBACK_FONTS; i++)
    {
        s32 offset = stbtt_GetFontOffsetForIndex((const unsigned char*)ttf, i);
        if (offset < 0) continue;

        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, (const unsigned char*)ttf, offset)) continue;

        SlugFallbackFont* fallback = &font->fallbackFonts[font->numFallbackFonts++];
        fallback->ttfData = ttf;
        fallback->ttfSize = (u32)fileSize;
        fallback->fontOffset = offset;
        fallback->fontIndex = i;
        fallback->emScale = stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
        loaded = true;
    }

    if (!loaded) FreeAllText(ttf);
    return loaded;
}

static bool SlugInitFontInfoForFace(SlugFont* font, u32 faceIndex, stbtt_fontinfo* info, f32* emScale)
{
    if (!font || !info || !emScale) return false;
    if (faceIndex == 0u)
    {
        if (!font->ttfData) return false;
        if (!stbtt_InitFont(info, (const unsigned char*)font->ttfData, 0)) return false;
        *emScale = font->emScale;
        return true;
    }

    u32 fallbackIndex = faceIndex - 1u;
    if (fallbackIndex >= font->numFallbackFonts) return false;
    SlugFallbackFont* fallback = &font->fallbackFonts[fallbackIndex];
    if (!fallback->ttfData) return false;
    if (!stbtt_InitFont(info, (const unsigned char*)fallback->ttfData, fallback->fontOffset)) return false;
    *emScale = fallback->emScale;
    return true;
}

static void SlugLoadFallbackFonts(SlugFont* font)
{
    for (u32 i = 0; i < (u32)ARRAY_SIZE(g_SlugFallbackFontPaths); i++)
    {
        SlugLoadFallbackFont(font, g_SlugFallbackFontPaths[i]);
    }
}

static void SlugFreeFallbackFonts(SlugFont* font)
{
    for (u32 i = 0; i < font->numFallbackFonts; i++)
    {
        bool seen = false;
        for (u32 j = 0; j < i; j++)
        {
            if (font->fallbackFonts[j].ttfData == font->fallbackFonts[i].ttfData)
            {
                seen = true;
                break;
            }
        }
        if (!seen && font->fallbackFonts[i].ttfData) FreeAllText(font->fallbackFonts[i].ttfData);
    }
}

static const SlugGlyph* SlugEnsureGlyph(SlugFont* font, u32 codePoint)
{
    if (codePoint < SLUG_MAX_GLYPHS) return &font->glyphs[codePoint];

    SlugGlyph* found = (SlugGlyph*)HMFind(&font->unicodeGlyphs, (u64)codePoint);
    if (found) return found;

    SlugGlyph glyph = {0};
    if (font->ttfData)
    {
        stbtt_fontinfo info;
        if (stbtt_InitFont(&info, (const unsigned char*)font->ttfData, 0))
        {
            if (stbtt_FindGlyphIndex(&info, (int)codePoint) != 0)
            {
                SlugBuildGlyph(&info, codePoint, font->emScale, &font->buffers, &glyph);
                if (glyph.advance != 0.0f || glyph.glyphBandEntry != 0u) font->glyphBuffersDirty = true;
            }
        }
        else AX_WARN("Slug dynamic glyph skipped, font data is invalid");
    }

    if (glyph.glyphBandEntry == 0u && glyph.advance == 0.0f && codePoint != ' ')
    {
        for (u32 i = 0; i < font->numFallbackFonts; i++)
        {
            const SlugFallbackFont* fallback = &font->fallbackFonts[i];
            stbtt_fontinfo info;
            if (!stbtt_InitFont(&info, (const unsigned char*)fallback->ttfData, fallback->fontOffset)) continue;
            if (stbtt_FindGlyphIndex(&info, (int)codePoint) == 0) continue;

            SlugBuildGlyph(&info, codePoint, fallback->emScale, &font->buffers, &glyph);
            if (glyph.advance != 0.0f || glyph.glyphBandEntry != 0u)
            {
                font->glyphBuffersDirty = true;
                break;
            }
        }
    }

    if (glyph.glyphBandEntry == 0u && glyph.advance == 0.0f) glyph = font->glyphs['-'];

    return (const SlugGlyph*)HMInsertOrAssign(&font->unicodeGlyphs, (u64)codePoint, &glyph);
}

static const SlugGlyph* SlugEnsureGlyphIndex(SlugFont* font, u32 faceIndex, u32 glyphIndex)
{
    u64 key = 0x8000000000000000ull | ((u64)faceIndex << 32u) | (u64)glyphIndex;
    SlugGlyph* found = (SlugGlyph*)HMFind(&font->unicodeGlyphs, key);
    if (found) return found;

    SlugGlyph glyph = {0};
    stbtt_fontinfo info;
    f32 emScale;
    if (SlugInitFontInfoForFace(font, faceIndex, &info, &emScale))
    {
        SlugBuildGlyphByIndex(&info, glyphIndex, emScale, &font->buffers, &glyph);
        if (glyph.advance != 0.0f || glyph.glyphBandEntry != 0u) font->glyphBuffersDirty = true;
    }
    if (glyph.glyphBandEntry == 0u && glyph.advance == 0.0f) glyph = font->glyphs['-'];
    return (const SlugGlyph*)HMInsertOrAssign(&font->unicodeGlyphs, key, &glyph);
}

static u32 SlugTextCodepointCount(const char* text, u32 maxBytes)
{
    if (!text) return 0u;
    u32 bytes = (u32)StringLengthSafe(text, maxBytes + 1u);
    if (bytes > maxBytes) return bytes;

    u32 count = 0u;
    const char* at = text;
    const char* end = text + bytes;
    while (at < end && *at)
    {
        u32 codePoint;
        int step = CodepointFromUtf8(&codePoint, at, end);
        at += step > 0 ? step : 1;
        count++;
    }
    return count;
}

bool SlugLoadFont(SlugFont* font, const char* path)
{
    SDL_zero(*font);
    u64 fileSize = FileSize(path);
    if (fileSize == 0u)
    {
        AX_WARN("Slug font missing: %s", path);
        return false;
    }

    char* ttf = ReadAllFileAlloc(path);
    if (!ttf)
    {
        AX_WARN("Failed to read Slug font: %s", path);
        return false;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, (const unsigned char*)ttf, 0))
    {
        AX_WARN("Failed to init Slug font: %s", path);
        FreeAllText(ttf);
        return false;
    }

    f32 emScale = stbtt_ScaleForMappingEmToPixels(&info, 1.0f);
    font->ttfData = ttf;
    font->ttfSize = (u32)fileSize;
    font->emScale = emScale;
    s32 ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    font->ascent = (f32)ascent * emScale;
    font->descent = (f32)descent * emScale;
    SlugLoadFallbackFonts(font);

    for (u32 codePoint = 0; codePoint < SLUG_MAX_GLYPHS; codePoint++)
    {
        if (codePoint >= ' ' && codePoint <= '~') SlugBuildGlyph(&info, codePoint, emScale, &font->buffers, &font->glyphs[codePoint]);
    }

    font->unicodeGlyphs = HMCreate((u32)ARRAY_SIZE(g_SlugUnicodeCodepoints), sizeof(SlugGlyph));
    for (u32 i = 0; i < (u32)ARRAY_SIZE(g_SlugUnicodeCodepoints); i++)
    {
        u32 codePoint = g_SlugUnicodeCodepoints[i];
        if (stbtt_FindGlyphIndex(&info, (int)codePoint) == 0) continue;
        SlugGlyph glyph;
        SlugBuildGlyph(&info, codePoint, emScale, &font->buffers, &glyph);
        if (glyph.advance == 0.0f) continue;
        HMInsertOrAssign(&font->unicodeGlyphs, (u64)codePoint, &glyph);
    }

    if (font->buffers.numCurveWords == 0u || font->buffers.numBands == 0u)
    {
        AX_WARN("Slug font generated no draw data: %s", path);
        HMDestroy(&font->unicodeGlyphs);
        DeAllocateTLSFGlobal(font->buffers.curves);
        DeAllocateTLSFGlobal(font->buffers.bands);
        SlugFreeFallbackFonts(font);
        FreeAllText(font->ttfData);
        SDL_zero(*font);
        return false;
    }

    font->gpuCurveWords = Maxu32(font->buffers.maxCurveWords, SLUG_INITIAL_GPU_WORDS);
    font->gpuBandWords = Maxu32(font->buffers.maxBands, SLUG_INITIAL_GPU_WORDS);
    font->curveBuffer = CreateBuffer(NULL, (size_t)font->gpuCurveWords * sizeof(u32), BReadRasterBit, "SlugCurveBuffer");
    font->bandBuffer  = CreateBuffer(NULL, (size_t)font->gpuBandWords * sizeof(u32), BReadRasterBit, "SlugBandBuffer");
    font->maxVertices = SLUG_MAX_TEXT * SLUG_VERTS_PER_GLYPH;
    font->vertices = (SlugVertex*)AllocateTLSFGlobal((size_t)font->maxVertices * sizeof(SlugVertex));
    font->vertexBuffer = CreateBuffer(NULL, (size_t)font->maxVertices * sizeof(SlugVertex), BVertexBit, "SlugVertexBuffer");
    font->glyphBuffersDirty = true;
    return true;
}

void SlugDestroyFont(SlugFont* font)
{
    if (font->curveBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, font->curveBuffer);
    if (font->bandBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, font->bandBuffer);
    if (font->vertexBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, font->vertexBuffer);
    if (font->vertices) DeAllocateTLSFGlobal(font->vertices);
    HMDestroy(&font->unicodeGlyphs);
    if (font->buffers.curves) DeAllocateTLSFGlobal(font->buffers.curves);
    if (font->buffers.bands) DeAllocateTLSFGlobal(font->buffers.bands);
    SlugFreeFallbackFonts(font);
    if (font->ttfData) FreeAllText(font->ttfData);
    SDL_zero(*font);
}

static void SlugWriteVertexV(SlugVertex* v, v128f pos, f32 ex, f32 ey, v128f normal, const SlugGlyph* glyph, u32 color)
{
    u32 packedLoc  = glyph->glyphBandEntry;
    u32 packedInfo = (glyph->bandMaxX & 0xFFFFu) | ((glyph->bandMaxY & 0x00FFu) << 16u);
    VecStore(v->pos, VecShuffle_0101(pos, normal));
    VecStore(v->tex, VecSetR(ex, ey, BitCast(f32, packedLoc), BitCast(f32, packedInfo)));
    VecStore(v->jac, VecSetR(1.0f, 0.0f, 0.0f, 1.0f));
    VecStore(v->band, VecSetR(glyph->bandScaleX, glyph->bandScaleY, glyph->bandOffsetX, glyph->bandOffsetY));
    v->color = color;
    v->z = VecGetZ(pos);
}

static void SlugWriteVertex(SlugVertex* v, f32 ox, f32 oy, f32 ex, f32 ey, f32 nx, f32 ny, const SlugGlyph* glyph, u32 color)
{
    SlugWriteVertexV(v, VecSetR(ox, oy, 0.0f, 0.0f), ex, ey, VecSetR(nx, ny, 0.0f, 0.0f), glyph, color);
}

static void SlugUploadVertexBuffer(SDL_GPUCommandBuffer* cmd, SDL_GPUBuffer* buffer, const void* data, size_t size)
{
    SDL_GPUTransferBufferCreateInfo transferDesc = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = size
    };
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(g_GPUDevice, &transferDesc);
    void* dst = SDL_MapGPUTransferBuffer(g_GPUDevice, transferBuffer, false);
    MemCopy(dst, data, size);
    SDL_UnmapGPUTransferBuffer(g_GPUDevice, transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(copyPass,
        &(SDL_GPUTransferBufferLocation){ .transfer_buffer = transferBuffer, .offset = 0 },
        &(SDL_GPUBufferRegion){ .buffer = buffer, .offset = 0, .size = size },
        true);
    SDL_EndGPUCopyPass(copyPass);
    SDL_ReleaseGPUTransferBuffer(g_GPUDevice, transferBuffer);
}

static bool SlugUploadGlyphBuffers(SDL_GPUCommandBuffer* cmd, SlugFont* font)
{
    if (!font->glyphBuffersDirty) return true;
    if (font->buffers.numCurveWords == 0u || font->buffers.numBands == 0u) return false;

    if (font->buffers.numCurveWords > font->gpuCurveWords)
    {
        if (font->curveBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, font->curveBuffer);
        font->gpuCurveWords = Maxu32(font->buffers.maxCurveWords, font->buffers.numCurveWords);
        font->curveBuffer = CreateBuffer(NULL, (size_t)font->gpuCurveWords * sizeof(u32), BReadRasterBit, "SlugCurveBuffer");
    }
    if (font->buffers.numBands > font->gpuBandWords)
    {
        if (font->bandBuffer) SDL_ReleaseGPUBuffer(g_GPUDevice, font->bandBuffer);
        font->gpuBandWords = Maxu32(font->buffers.maxBands, font->buffers.numBands);
        font->bandBuffer = CreateBuffer(NULL, (size_t)font->gpuBandWords * sizeof(u32), BReadRasterBit, "SlugBandBuffer");
    }
    if (!font->curveBuffer || !font->bandBuffer) return false;

    SlugUploadVertexBuffer(cmd, font->curveBuffer, font->buffers.curves, (size_t)font->buffers.numCurveWords * sizeof(u32));
    SlugUploadVertexBuffer(cmd, font->bandBuffer, font->buffers.bands, (size_t)font->buffers.numBands * sizeof(u32));
    font->glyphBuffersDirty = false;
    return true;
}

void SlugClear(SlugFont* font)
{
    font->numVertices = 0u;
}

bool SlugAppendText3D(SlugFont* font, const char* text, float3 pos, Quaternion rot, f32 size, u32 color)
{
    if (!font->vertices || !font->vertexBuffer || !font->curveBuffer || !font->bandBuffer)
    {
        AX_WARN("Slug append skipped, resources not initialized");
        return false;
    }

    u32 textBytes = (u32)StringLengthSafe(text, SLUG_MAX_TEXT + 1u);
    u32 textLen = SlugTextCodepointCount(text, SLUG_MAX_TEXT);
    if (textBytes == 0u) return true;
    if (textBytes > SLUG_MAX_TEXT)
    {
        AX_WARN("Slug text too long: %u", textBytes);
        return false;
    }
    if (font->numVertices + textLen * SLUG_VERTS_PER_GLYPH > font->maxVertices)
    {
        AX_WARN("Slug vertex batch full: vertices=%u needed=%u capacity=%u", font->numVertices, textLen * SLUG_VERTS_PER_GLYPH, font->maxVertices);
        return false;
    }

    v128f posV  = VecSetR(pos.x, pos.y, pos.z, 0.0f);
    v128f right = QMulVec3V(VecSetR(1.0f, 0.0f, 0.0f, 0.0f), rot);
    v128f up    = QMulVec3V(VecSetR(0.0f, 1.0f, 0.0f, 0.0f), rot);

    SlugVertex* vertices = font->vertices;
    f32 cursor = 0.0f;
    const f32 n = 1.0 / MATH_Sqrt2;
    f32 invSize = 1.0f / Maxf32(size, 1.0e-6f);

    const char* at = text;
    const char* end = text + textBytes;
    while (at < end && *at)
    {
        u32 codePoint;
        int step = CodepointFromUtf8(&codePoint, at, end);
        at += step > 0 ? step : 1;

        const SlugGlyph* glyph = SlugEnsureGlyph(font, codePoint);
        if (glyph->advance == 0.0f && codePoint != ' ') continue;

        if (glyph->glyphBandEntry != 0u || codePoint != ' ')
        {
            f32 ex0 = glyph->x0 - SLUG_BOUNDS_PAD_EM, ey0 = glyph->y0 - SLUG_BOUNDS_PAD_EM;
            f32 ex1 = glyph->x1 + SLUG_BOUNDS_PAD_EM, ey1 = glyph->y1 + SLUG_BOUNDS_PAD_EM;

            v128f lx0 = VecSet1((cursor + ex0) * size);
            v128f ly0 = VecSet1(ey0 * size);
            v128f lx1 = VecSet1((cursor + ex1) * size);
            v128f ly1 = VecSet1(ey1 * size);

            v128f p0 = VecFmadd(lx0, right, VecFmadd(ly1, up, posV));
            v128f p1 = VecFmadd(lx1, right, VecFmadd(ly1, up, posV));
            v128f p2 = VecFmadd(lx1, right, VecFmadd(ly0, up, posV));
            v128f p3 = VecFmadd(lx0, right, VecFmadd(ly0, up, posV));

            v128f nn = VecSet1(n);
            v128f n0 = VecMul(nn, VecSub(up, right));
            v128f n1 = VecMul(nn, VecAdd(up, right));
            v128f n2 = VecMul(nn, VecSub(right, up));
            v128f n3 = VecMul(nn, VecNeg(VecAdd(up, right)));

            struct { v128f p, normal; f32 ex, ey; } corners[4] = {
                { p0, n0, ex0, ey1 },
                { p1, n1, ex1, ey1 },
                { p2, n2, ex1, ey0 },
                { p3, n3, ex0, ey0 }
            };

            const u32 tri[6] = { 0u, 3u, 1u, 1u, 3u, 2u };
            for (u32 vi = 0; vi < 6u; vi++)
            {
                const u32 ci = tri[vi];
                SlugVertex* v = &vertices[font->numVertices++];
                SlugWriteVertexV(v, corners[ci].p, corners[ci].ex, corners[ci].ey, corners[ci].normal, glyph, color);
                v->jac[0] = invSize;
                v->jac[3] = invSize;
            }
        }
        cursor += glyph->advance;
    }
    return true;
}

bool SlugAppendGlyph2D(SlugFont* font, u32 faceIndex, u32 glyphIndex, float2 pos, f32 size, u32 color)
{
    if (font == NULL) font = &g_SlugDemoFont;
    if (!font->vertices || !font->vertexBuffer || !font->curveBuffer || !font->bandBuffer)
    {
        AX_WARN("Slug glyph append skipped, resources not initialized");
        return false;
    }
    if (font->numVertices + SLUG_VERTS_PER_GLYPH > font->maxVertices)
    {
        AX_WARN("Slug glyph vertex batch full: vertices=%u capacity=%u", font->numVertices, font->maxVertices);
        return false;
    }

    const SlugGlyph* glyph = SlugEnsureGlyphIndex(font, faceIndex, glyphIndex);
    if (glyph->advance == 0.0f && glyph->glyphBandEntry == 0u) return true;

    SlugVertex* vertices = font->vertices;
    const f32 n = 0.70710678f;
    f32 invSize = 1.0f / Maxf32(size, 1.0e-6f);
    f32 pad = SLUG_BOUNDS_PAD_PX * invSize;
    f32 ex0 = glyph->x0 - pad, ey0 = glyph->y0 - pad;
    f32 ex1 = glyph->x1 + pad, ey1 = glyph->y1 + pad;
    f32 ox0 = pos.x + ex0 * size;
    f32 ox1 = pos.x + ex1 * size;
    f32 oy0 = pos.y - ey0 * size;
    f32 oy1 = pos.y - ey1 * size;
    struct { f32 ox, oy, ex, ey, nx, ny; } corners[4] = {
        { ox0, oy0, ex0, ey0, -n,  n },
        { ox1, oy0, ex1, ey0,  n,  n },
        { ox1, oy1, ex1, ey1,  n, -n },
        { ox0, oy1, ex0, ey1, -n, -n }
    };
    const u32 tri[6] = { 0u, 3u, 1u, 1u, 3u, 2u };
    for (u32 vi = 0; vi < 6u; vi++)
    {
        const u32 ci = tri[vi];
        SlugVertex* v = &vertices[font->numVertices++];
        SlugWriteVertex(v, corners[ci].ox, corners[ci].oy, corners[ci].ex, corners[ci].ey, corners[ci].nx, corners[ci].ny, glyph, color);
        v->z = 0.0f;
        v->jac[0] = invSize;
        v->jac[3] = invSize;
    }
    return true;
}

u32 SlugGetFontFaceCount(SlugFont* font)
{
    if (!font || !font->ttfData) return 0u;
    return 1u + font->numFallbackFonts;
}

void* SlugGetFontFaceData(SlugFont* font, u32 faceIndex, u32* size)
{
    if (size) *size = 0u;
    if (!font) return NULL;
    if (faceIndex == 0u)
    {
        if (size) *size = font->ttfSize;
        return font->ttfData;
    }
    u32 fallbackIndex = faceIndex - 1u;
    if (fallbackIndex >= font->numFallbackFonts) return NULL;
    if (size) *size = font->fallbackFonts[fallbackIndex].ttfSize;
    return font->fallbackFonts[fallbackIndex].ttfData;
}

f32 SlugGetFontFaceEmScale(SlugFont* font, u32 faceIndex)
{
    if (!font) return 0.0f;
    if (faceIndex == 0u) return font->emScale;
    u32 fallbackIndex = faceIndex - 1u;
    if (fallbackIndex >= font->numFallbackFonts) return 0.0f;
    return font->fallbackFonts[fallbackIndex].emScale;
}

s32 SlugGetFontFaceCollectionIndex(SlugFont* font, u32 faceIndex)
{
    if (!font) return 0;
    if (faceIndex == 0u) return 0;
    u32 fallbackIndex = faceIndex - 1u;
    if (fallbackIndex >= font->numFallbackFonts) return 0;
    return font->fallbackFonts[fallbackIndex].fontIndex;
}

f32 SlugGetFontAscent(SlugFont* font)
{
    return font ? font->ascent : 0.0f;
}

f32 SlugGetFontDescent(SlugFont* font)
{
    return font ? font->descent : 0.0f;
}

float2 SlugCalcTextSize(SlugFont* font, const char* text, f32 size)
{
    float2 result = {0.0f, 0.0f};
    if (!font || !text) return result;

    f32 lineWidth = 0.0f;
    f32 maxWidth = 0.0f;
    f32 lineHeight = Maxf32((font->ascent - font->descent) * size, size);
    result.y = lineHeight;

    while (*text)
    {
        u32 ch;
        int step = CodepointFromUtf8(&ch, text, NULL);
        text += step > 0 ? step : 1;
        if (ch == '\n')
        {
            maxWidth = Maxf32(maxWidth, lineWidth);
            lineWidth = 0.0f;
            result.y += lineHeight;
            continue;
        }
        const SlugGlyph* glyph = SlugEnsureGlyph(font, ch);
        lineWidth += glyph->advance * size;
    }

    result.x = Maxf32(maxWidth, lineWidth);
    return result;
}

void SlugRender(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, SlugFont* font, mat4x4 viewProj)
{
    SDL_GPUGraphicsPipeline* pipeline = depthTarget ? g_RenderState.slugDepthPipeline : g_RenderState.slugPipeline;
    if (!font->vertexBuffer || !font->curveBuffer || !font->bandBuffer || !pipeline)
    {
        AX_WARN("Slug render skipped, resources not initialized");
        return;
    }
    if (font->numVertices == 0u) return;

    if (!SlugUploadGlyphBuffers(cmd, font))
    {
        AX_WARN("Slug render skipped, glyph buffers upload failed");
        return;
    }
    SlugUploadVertexBuffer(cmd, font->vertexBuffer, font->vertices, (size_t)font->numVertices * sizeof(SlugVertex));

    SlugVertexParams params = {0};
    params.matrix = viewProj;
    params.viewport[0] = (f32)Maxu32(g_WindowState.prev_drawablew, 1u);
    params.viewport[1] = (f32)Maxu32(g_WindowState.prev_drawableh, 1u);

    SDL_GPUBuffer* storageBuffers[2] = { font->curveBuffer, font->bandBuffer };
    SDL_GPUBufferBinding vertexBinding = { font->vertexBuffer, 0 };
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, colorTarget, 1, depthTarget);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
    SDL_BindGPUFragmentStorageBuffers(pass, 0, storageBuffers, SDL_arraysize(storageBuffers));
    SDL_PushGPUVertexUniformData(cmd, 0, &params, sizeof(params));
    SDL_DrawGPUPrimitives(pass, font->numVertices, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    SlugClear(font);
}

void SlugRender2D(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SlugFont* font)
{
    f32 w = (f32)Maxu32(g_WindowState.prev_drawablew, 1u);
    f32 h = (f32)Maxu32(g_WindowState.prev_drawableh, 1u);
    mat4x4 screen = M44Identity();
    screen.r[0] = VecSetR(2.0f / w, 0.0f, 0.0f, 0.0f);
    screen.r[1] = VecSetR(0.0f, -2.0f / h, 0.0f, 0.0f);
    screen.r[2] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f);
    screen.r[3] = VecSetR(-1.0f, 1.0f, 0.0f, 1.0f);
    SlugRender(cmd, colorTarget, NULL, font, screen);
}

SlugFont* SlugGetDemoFont(void)
{
    return &g_SlugDemoFont;
}

void SlugInitDemo(void)
{
    // if (!SlugLoadFont(&g_SlugDemoFont, "Assets/Fonts/Quivira.otf")) AX_WARN("Slug demo font load failed");
    if (!SlugLoadFont(&g_SlugDemoFont, "Assets/Fonts/JetBrainsMono-Regular.ttf")) AX_WARN("Slug demo font load failed");
}

void SlugDestroyDemo(void)
{
    SlugDestroyFont(&g_SlugDemoFont);
}

void RenderSlugDemo(SDL_GPUCommandBuffer* cmd, SDL_GPUColorTargetInfo* colorTarget, SDL_GPUDepthStencilTargetInfo* depthTarget, mat4x4 viewProj)
{
    Quaternion rot = QFromAxisAngle(F3Up(), -MATH_PI * 0.5);
    SlugAppendText3D(&g_SlugDemoFont, "Anılcan Gülkaya", (float3){ -2.0f, -2.0f, -3.0f }, rot, 0.35f, 0xFFFFFFFFu);
    SlugAppendText3D(&g_SlugDemoFont, "Quivira.otf buffer renderer", (float3){ -2.0f, -2.55f, -3.0f }, QIdentity(), 0.22f, 0xFFFFC060u);
    SlugRender(cmd, colorTarget, depthTarget, &g_SlugDemoFont, viewProj);
}
