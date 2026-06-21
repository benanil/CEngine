#ifndef MATH_COLOR
#define MATH_COLOR

#include "Vector.h"

#define UCOLOR_BLACK       0xFF000000u
#define UCOLOR_WHITE       0xFFFFFFFFu

#define UCOLOR_RED         0xFF0000FFu
#define UCOLOR_GREEN       0xFF00FF00u
#define UCOLOR_BLUE        0xFFFF0000u

#define UCOLOR_YELLOW      0xFF00FFFFu
#define UCOLOR_CYAN        0xFFFFFF00u
#define UCOLOR_MAGENTA     0xFFFF00FFu

#define UCOLOR_ORANGE      0xFF0099FFu
#define UCOLOR_PURPLE      0xFFFF0080u
#define UCOLOR_PINK        0xFFAA66FFu
#define UCOLOR_BROWN       0xFF13458Bu

#define UCOLOR_GRAY        0xFF808080u
#define UCOLOR_DARK_GRAY   0xFF202020u
#define UCOLOR_LIGHT_GRAY  0xFFC0C0C0u

#define UCOLOR_DARK_RED    0xFF000080u
#define UCOLOR_DARK_GREEN  0xFF008000u
#define UCOLOR_DARK_BLUE   0xFF800000u

#define UCOLOR_GOLD        0xFF00D7FFu
#define UCOLOR_SKY_BLUE    0xFFFFCC66u
#define UCOLOR_LIME        0xFF00FFBFu
#define UCOLOR_TEAL        0xFF808000u

#define UCOLOR_BG_DARK     0xFF121212u
#define UCOLOR_BG_LIGHT    0xFFF5F5F5u

#define UCOLOR_TEXT_DARK   0xFF111111u
#define UCOLOR_TEXT_LIGHT  0xFFEEEEEEu

#define UCOLOR_SUCCESS     0xFF71CC2Eu
#define UCOLOR_WARNING     0xFF0FC4F1u
#define UCOLOR_ERROR       0xFF3C4CE7u
#define UCOLOR_INFO        0xFFDB9834u

purefn u32 PackColorToUint(u8 r, u8 g, u8 b, u8 a) {
    return r | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24);
}

purefn u32 MakeRGBAGrayScale(u8 gray) {
    return (u32)(gray) * 0x01010101u;
}

purefn u32 MakeRGBGrayScale(u8 gray) {
    return 0xFF000000u | ((u32)(gray) * 0x01010101u);
}

purefn u32 PackColorToUint3Float(f32 r, f32 g, f32 b) {
    return (u32)(r * 255.0f) | ((u32)(g * 255.0f) << 8) | ((u32)(b * 255.0f) << 16);
}

purefn u32 PackColor3PtrToUint(const f32* c) {
    return (u32)(*c * 255.0f) | ((u32)(c[1] * 255.0f) << 8) | ((u32)(c[2] * 255.0f) << 16);
}

purefn u32 PackColor4PtrToUint(const f32* c) {
    return (u32)(*c * 255.0f) | ((u32)(c[1] * 255.0f) << 8) | ((u32)(c[2] * 255.0f) << 16) | ((u32)(c[3] * 255.0f) << 24);
}

forceinline void UnpackColor3Uint(u32 color, f32* colorf) {
    const f32 tof1 = 1.0f / 255.0f;
    colorf[0] = (f32)(color >> 0  & 0xFF) * tof1;
    colorf[1] = (f32)(color >> 8  & 0xFF) * tof1;
    colorf[2] = (f32)(color >> 16 & 0xFF) * tof1;
}

forceinline void UnpackColor4Uint(u32 color, f32* colorf) {
    const f32 tof1 = 1.0f / 255.0f;
    colorf[0] = (f32)(color >> 0  & 0xFF) * tof1;
    colorf[1] = (f32)(color >> 8  & 0xFF) * tof1;
    colorf[2] = (f32)(color >> 16 & 0xFF) * tof1;
    colorf[3] = (f32)(color >> 24) * tof1;
}

purefn u32 MultiplyU32Colors(u32 a, u32 b)
{
    u32 result = 0u;
    result |= ((a & 0xffu) * (b & 0xffu)) >> 8u;
    result |= ((((a >> 8u) & 0xffu) * ((b >> 8u) & 0xffu)) >> 8u) << 8u;
    result |= ((((a >> 16u) & 0xffu) * ((b >> 16u) & 0xffu)) >> 8u) << 16u;
    return result;
}

purefn float3 HUEToRGB(f32 h) {
    f32 r = Saturatef32(Absf32(h * 6.0f - 3.0f) - 1.0f);
    f32 g = Saturatef32(2.0f - Absf32(h * 6.0f - 2.0f));
    f32 b = Saturatef32(2.0f - Absf32(h * 6.0f - 4.0f));
    return (float3){ r, g, b };
}

// converts hue to rgb color
purefn u32 HUEToRGBU32(f32 h) {
    float3 v3 = HUEToRGB(h);
    u32 res = PackColor3PtrToUint(&v3.x);
    return res | 0xFF000000u; // make the alpha 255
}

purefn float3 RGBToHSV(float3 rgb)
{
    f32 r = rgb.x, g = rgb.y, b = rgb.z;
    f32 K = 0.0f;
    if (g < b) {
        f32 t = g;
        g = b;
        b = t;
        // Swap(g, b);
        K = -1.0f;
    }

    if (r < g) {
        f32 t = g;
        g = r;
        r = t;
        //Swap(r, g);
        K = -2.0f / 6.0f - K;
    }
    const f32 chroma = r - (g < b ? g : b);
    return (float3){
        Absf32(K + (g - b) / (6.0f * chroma + 1e-20f)),
        chroma / (r + 1e-20f),
        r
    };
}

forceinline void HSVToRGB(float3 hsv, f32* dst)
{
    const v128f K = VecSetR(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    v128f p  = VecFabs(VecSub(VecMul(VecFract(VecAdd(VecSet1(hsv.x), K)), VecSet1(6.0f)), VecSet1(3.0f)));
    v128f kx = VecSplatX(K);
    v128f rv = VecMul(VecLerp(kx, VecClamp01(VecSub(p, kx)), hsv.y), VecSet1(hsv.z));
    Vec3Store(dst, rv);
}

#endif // MATH_COLOR