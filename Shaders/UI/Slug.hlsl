// ===================================================
// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright 2017, by Eric Lengyel.
// ===================================================
// https://github.com/EricLengyel/Slug

#define SLUG_WEIGHT 1

struct VSInput
{
    float4 pos : POSITION0;
    float4 tex : TEXCOORD0;
    float4 jac : TEXCOORD1;
    float4 bnd : TEXCOORD2;
    uint col : COLOR0;
    float z : TEXCOORD3;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    nointerpolation float4 banding : TEXCOORD1;
    nointerpolation int4 glyph : TEXCOORD2;
};

cbuffer vs_params : register(b0, space1)
{
    float4x4 slug_matrix;
    float4 slug_viewport;
};

StructuredBuffer<uint> CurveBuffer : register(t0, space2);
StructuredBuffer<uint> BandBuffer  : register(t1, space2);

float4 UnpackColor4Uint(uint color)
{
    return float4(
        float((color >> 0u)  & 0xFFu),
        float((color >> 8u)  & 0xFFu),
        float((color >> 16u) & 0xFFu),
        float((color >> 24u) & 0xFFu)) * (1.0f / 255.0f);
}

uint2 UnpackU16(uint v)
{
    return uint2(v & 0xFFFFu, v >> 16u);
}

float2 UnpackHalf2(uint v)
{
    return float2(f16tof32(v & 0xFFFFu), f16tof32(v >> 16u));
}

uint CalcRootCode(float y1, float y2, float y3)
{
    uint i1 = asuint(y1) >> 31u;
    uint i2 = asuint(y2) >> 30u;
    uint i3 = asuint(y3) >> 29u;
    uint shift = (i2 & 2u) | (i1 & ~2u);
    shift = (i3 & 4u) | (shift & ~4u);
    return ((0x2E74u >> shift) & 0x0101u);
}

float2 SolveHorizPoly(float4 p12, float2 p3)
{
    float2 a = p12.xy - p12.zw * 2.0 + p3;
    float2 b = p12.xy - p12.zw;
    float ra = 1.0 / a.y;
    float rb = 0.5 / b.y;
    float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
    float t1 = (b.y - d) * ra;
    float t2 = (b.y + d) * ra;
    if (abs(a.y) < 1.0 / 65536.0) t1 = t2 = p12.y * rb;
    return float2((a.x * t1 - b.x * 2.0) * t1 + p12.x, (a.x * t2 - b.x * 2.0) * t2 + p12.x);
}

float2 SolveVertPoly(float4 p12, float2 p3)
{
    float2 a = p12.xy - p12.zw * 2.0 + p3;
    float2 b = p12.xy - p12.zw;
    float ra = 1.0 / a.x;
    float rb = 0.5 / b.x;
    float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
    float t1 = (b.x - d) * ra;
    float t2 = (b.x + d) * ra;
    if (abs(a.x) < 1.0 / 65536.0) t1 = t2 = p12.x * rb;
    return float2((a.y * t1 - b.y * 2.0) * t1 + p12.y, (a.y * t2 - b.y * 2.0) * t2 + p12.y);
}

float CalcCoverage(float xcov, float ycov, float xwgt, float ywgt, int flags)
{
    (void)flags;
    float coverage = max(abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
    coverage = saturate(coverage);
    #if SLUG_WEIGHT
    coverage = sqrt(coverage);
    #endif
    return coverage;
}

void LoadCurve(uint curveIndex, float2 renderCoord, out float4 p12, out float2 p3)
{
    uint baseIndex = curveIndex * 3u;
    float2 p1 = UnpackHalf2(CurveBuffer[baseIndex + 0u]) - renderCoord;
    float2 p2 = UnpackHalf2(CurveBuffer[baseIndex + 1u]) - renderCoord;
    p3 = UnpackHalf2(CurveBuffer[baseIndex + 2u]) - renderCoord;
    p12 = float4(p1, p2);
}

float SlugRender(float2 renderCoord, float4 bandTransform, int4 glyphData)
{
    float2 emsPerPixel = fwidth(renderCoord);
    float2 pixelsPerEm = 1.0 / max(emsPerPixel, float2(1.0 / 65536.0, 1.0 / 65536.0));

    int2 bandMax = glyphData.zw;
    bandMax.y &= 0x00FF;
    int2 bandIndex = clamp(int2(renderCoord * bandTransform.xy + bandTransform.zw), int2(0, 0), bandMax);
    uint glyphLoc = uint(glyphData.x);

    float xcov = 0.0;
    float xwgt = 0.0;
    uint2 hbandData = UnpackU16(BandBuffer[glyphLoc + uint(bandIndex.y)]);
    uint hbandLoc = glyphLoc + hbandData.y;

    for (uint curveIndex = 0u; curveIndex < hbandData.x; curveIndex++)
    {
        uint curveLoc = UnpackU16(BandBuffer[hbandLoc + curveIndex]).x;
        float4 p12;
        float2 p3;
        LoadCurve(curveLoc, renderCoord, p12, p3);
        if (max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) break;

        uint code = CalcRootCode(p12.y, p12.w, p3.y);
        if (code != 0u)
        {
            float2 r = SolveHorizPoly(p12, p3) * pixelsPerEm.x;
            if ((code & 1u) != 0u)
            {
                xcov += saturate(r.x + 0.5);
                xwgt = max(xwgt, saturate(1.0 - abs(r.x) * 2.0));
            }
            if (code > 1u)
            {
                xcov -= saturate(r.y + 0.5);
                xwgt = max(xwgt, saturate(1.0 - abs(r.y) * 2.0));
            }
        }
    }

    float ycov = 0.0;
    float ywgt = 0.0;
    uint2 vbandData = UnpackU16(BandBuffer[glyphLoc + uint(bandMax.y + 1 + bandIndex.x)]);
    uint vbandLoc = glyphLoc + vbandData.y;

    for (uint curveIndex = 0u; curveIndex < vbandData.x; curveIndex++)
    {
        uint curveLoc = UnpackU16(BandBuffer[vbandLoc + curveIndex]).x;
        float4 p12;
        float2 p3;
        LoadCurve(curveLoc, renderCoord, p12, p3);
        if (max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) break;

        uint code = CalcRootCode(p12.x, p12.z, p3.x);
        if (code != 0u)
        {
            float2 r = SolveVertPoly(p12, p3) * pixelsPerEm.y;
            if ((code & 1u) != 0u)
            {
                ycov -= saturate(r.x + 0.5);
                ywgt = max(ywgt, saturate(1.0 - abs(r.x) * 2.0));
            }
            if (code > 1u)
            {
                ycov += saturate(r.y + 0.5);
                ywgt = max(ywgt, saturate(1.0 - abs(r.y) * 2.0));
            }
        }
    }

    return CalcCoverage(xcov, ycov, xwgt, ywgt, glyphData.w);
}

void SlugUnpack(float4 tex, float4 bnd, out float4 vbnd, out int4 vgly)
{
    uint2 g = asuint(tex.zw);
    vgly = int4(g.x, 0u, g.y & 0xFFFFu, g.y >> 16u);
    vbnd = bnd;
}

float2 SlugDilate(float4 pos, float4 tex, float4 jac, float z, float4 m0, float4 m1, float4 m3, float2 dim, out float2 vpos)
{
    float2 n = normalize(pos.zw);
    float3 p = float3(pos.xy, z);
    float s = dot(m3.xyz, p) + m3.w;
    float t = dot(m3.xy, n);
    float u = (s * dot(m0.xy, n) - t * (dot(m0.xyz, p) + m0.w)) * dim.x;
    float v = (s * dot(m1.xy, n) - t * (dot(m1.xyz, p) + m1.w)) * dim.y;
    float s2 = s * s;
    float st = s * t;
    float uv = u * u + v * v;
    float2 d = pos.zw * (s2 * (st + sqrt(max(uv, 0.0))) / max(uv - st * st, 1.0 / 65536.0));
    vpos = pos.xy + d;
    return float2(tex.x + dot(d, jac.xy), tex.y + dot(d, jac.zw));
}

VSOutput vert(VSInput input)
{
    float2 p;
    VSOutput o;
    o.texcoord = SlugDilate(input.pos, input.tex, input.jac, input.z, slug_matrix[0], slug_matrix[1], slug_matrix[3], slug_viewport.xy, p);
    o.position = mul(slug_matrix, float4(p, input.z, 1.0));
    SlugUnpack(input.tex, input.bnd, o.banding, o.glyph);
    o.color = UnpackColor4Uint(input.col);
    return o;
}

VSOutput vert2d(VSInput input)
{
    VSOutput o;
    o.texcoord = input.tex.xy;
    o.position = float4(input.pos.x * slug_viewport.z - 1.0f, 1.0f - input.pos.y * slug_viewport.w, input.z, 1.0f);
    SlugUnpack(input.tex, input.bnd, o.banding, o.glyph);
    o.color = UnpackColor4Uint(input.col);
    return o;
}

float4 frag(VSOutput input) : SV_Target0
{
    float coverage = SlugRender(input.texcoord, input.banding, input.glyph);
    return input.color * coverage;
}
