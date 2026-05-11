#ifndef BITPACK_HLSL
#define BITPACK_HLSL

#include "Common.hlsl"

f16_3 UnpackVec3XY11Z10Snorm(uint packed) {
    const f16 scale11 = f16(1.0 / 1023.0);
    const f16 scale10 = f16(1.0 / 511.0);
    return f16_3(
        f16((int)(packed << 21) >> 21) * scale11,
        f16((int)(packed << 10) >> 21) * scale11,
        f16((int)(packed      ) >> 22) * scale10
    );
}

f16_2 UnpackHalf2(uint packed) {
    #if INT16_SUPPORTED
    return asfloat16(uint16_t2(u16(packed), u16(packed >> 16)));
    #else
    return f16_2(f16tof32(packed), f16tof32(packed >> 16));
    #endif
}

uint PackHalf2(f16_2 v)
{
    #if INT16_SUPPORTED
    uint16_t2 h = asuint16(v);
    return (uint(h.y) << 16) | uint(h.x);
    #else
    uint lo = f32tof16(v.x);
    uint hi = f32tof16(v.y);
    return (hi << 16) | lo;
    #endif
}

f16_4 UnpackRGBA16Snorm(uint xy, uint zw) {
    return f16_4(
        f16(int(xy << 16u) >> 16),
        f16(int(xy)        >> 16),
        f16(int(zw << 16u) >> 16),
        f16(int(zw)        >> 16)
    ) * f16(1.0 / 32767.0);
}

f16_3 UnpackVec3XY11Z10Unorm(uint packed) {
    const f16 scale11 = f16(1.0 / 2047.0);
    const f16 scale10 = f16(1.0 / 1023.0);
    return f16_3(
        f16( packed        & 0x7FFu) * scale11,
        f16((packed >> 11) & 0x7FFu) * scale11,
        f16( packed >> 22          ) * scale10
    );
}

float3 UnpackColor3Uint(uint color)
{
    const float tof1 = 1.0 / 255.0;
    return float3(
        (float)((color >> 0)  & 0xFF),
        (float)((color >> 8)  & 0xFF),
        (float)((color >> 16) & 0xFF)
    ) * tof1;
}

f16_3 OctDecode(f16_2 f)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    f16_3 n = f16_3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    f16  t  = saturate(-n.z);
    n.xy   += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

f16_2 DecodeDiamond(f16 p)
{
    f16   p_sign = f16(sign(p - f16(0.5)));
    f16_2 v;
    v.x = mad(p_sign * f16(-4.0), p, f16(1.0) + p_sign * f16(2.0));
    v.y = p_sign * (f16(1.0) - abs(v.x));
    return normalize(v);
}

// https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/
f16_3 DecodeTangent(f16_3 normal, f16 diamond_tangent)
{
    f16_3 t1_a = normalize(f16_3( normal.y, -normal.x, f16(0.0)));
    f16_3 t1_b = normalize(f16_3( normal.z,  f16(0.0), -normal.x));
    f16_3 t1   = select(abs(normal.y) > abs(normal.z), t1_a, t1_b);

    f16_2 packed_tangent = DecodeDiamond(diamond_tangent);
    return packed_tangent.x * t1 + packed_tangent.y * cross(t1, normal);
}

void UnpackNormalTangent(uint packed, out f16_3 normal, out f16_3 tangent)
{
    f16_2 oct = f16_2(
        f16((int)(packed << 21) >> 21) * f16(1.0 / 1023.0),
        f16((int)(packed << 10) >> 21) * f16(1.0 / 1023.0));
    f16 diamond = f16((packed >> 22) & 0x1FFu) * f16(1.0 / 511.0);
    normal      = OctDecode(oct);
    tangent     = DecodeTangent(normal, diamond);
}

f16 UnpackTangentHandedness(uint packed)
{
    return (packed & 0x80000000u) != 0u ? f16(-1.0) : f16(1.0);
}

#endif
