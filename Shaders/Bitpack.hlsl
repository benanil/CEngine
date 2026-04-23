#ifndef BITPACK_HLSL
#define BITPACK_HLSL

#include "Common.hlsl"

fp16_3 UnpackVec3XY11Z10Snorm(uint packed) {
    s16 sx = s16((int)( packed << 21) >> 21); // 32 - 11
    s16 sy = s16((int)((packed >> 11) << 21) >> 21);
    s16 sz = s16((int)((packed >> 22) << 22) >> 22); // 32 - 10
    return fp16_3(sx * (1.0 / 1023.0), sy * (1.0 / 1023.0), sz * (1.0 / 511.0));
}

fp16_2 unpackHalf2x16(uint packed) {
    #if INT16_SUPPORTED
    return asfloat16(uint16_t2(u16(packed), u16(packed >> 16)));
    #else
    return fp16_2(f16tof32(packed), f16tof32(packed >> 16));
    #endif
}

fp16_4 UnpackRGBA16Snorm(uint xy, uint zw) {
    s16x4 raw;
    raw.x = s16(int(xy << 16u) >> 16);
    raw.y = s16(int(xy) >> 16);
    raw.z = s16(int(zw << 16u) >> 16);
    raw.w = s16(int(zw) >> 16);
    return fp16_4(fp16_4(raw) / fp16_4(32767.0, 32767.0, 32767.0, 32767.0));
}

fp16_3 UnpackVec3XY11Z10Unorm(uint packed) {
    u16 ux = u16( packed        & 0x7FFu);
    u16 uy = u16((packed >> 11) & 0x7FFu);
    u16 uz = u16((packed >> 22)         );
    return fp16_3(ux / 2047.0, uy / 2047.0, uz / 1023.0);
}

fp16_3 OctDecode(fp16_2 f)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    fp16_3 n = fp16_3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    fp16  t  = saturate(-n.z);
    n.xy    += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

fp16_2 decode_diamond(fp16 p)
{
    fp16_2 v;
    fp16 p_sign = fp16(sign(p - 0.5));
    v.x = -p_sign * 4.0 * p + 1.0 + p_sign * 2.0;
    v.y = p_sign * (1.0 - abs(v.x));
    return normalize(v);
}

// https://www.jeremyong.com/graphics/2023/01/09/tangent-spaces-and-diamond-encoding/
fp16_3 decode_tangent(fp16_3 normal, fp16 diamond_tangent)
{
    fp16_3 t1;
    if (abs(normal.y) > abs(normal.z))
        t1 = fp16_3(normal.y, -normal.x, 0.f);
    else
        t1 = fp16_3(normal.z, 0.f, -normal.x);
    
    t1 = normalize(t1);
    fp16_3 t2 = cross(t1, normal);
    fp16_2 packed_tangent = decode_diamond(diamond_tangent);
    return packed_tangent.x * t1 + packed_tangent.y * t2;
}

void UnpackNormalTangent(uint packed, out fp16_3 normal, out fp16_3 tangent)
{
    fp16_3 oct    = UnpackVec3XY11Z10Snorm(packed);
    normal        = OctDecode(oct.xy);
    fp16 diamond  = oct.z * 0.5 + 0.5;
    tangent       = decode_tangent(normal, diamond);
    // binormal      = cross(normal, tangent);
}

#endif