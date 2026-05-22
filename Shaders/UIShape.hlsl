#include "PBR.hlsl"

struct UIShape
{
    float4 rect;
    float4 params;
    uint color;
    uint borderColor;
    uint shape;
    uint flags;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    nointerpolation float2 halfSize : TEXCOORD1;
    nointerpolation float4 params : TEXCOORD2;
    nointerpolation uint color : COLOR0;
    nointerpolation uint borderColor : COLOR1;
    nointerpolation uint shape : TEXCOORD3;
};

cbuffer ui_params : register(b0, space1)
{
    float4 ui_screen_scale;
};

StructuredBuffer<UIShape> ShapeBuffer : register(t0);

float SdfBox(float2 p, float2 b)
{
    float2 d = abs(p) - b;
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

float SdfRoundedBox(float2 p, float2 b, float r)
{
    r = min(r, min(b.x, b.y));
    return SdfBox(p, max(b - r, 0.0f)) - r;
}

float ShapeDistance(float2 p, float2 halfSize, float radius, uint shape)
{
    if (shape == 2u) return length(p) - min(halfSize.x, halfSize.y);
    if (shape == 3u) return SdfRoundedBox(p, halfSize, min(halfSize.x, halfSize.y));
    if (shape == 1u) return SdfRoundedBox(p, halfSize, radius);
    return SdfBox(p, halfSize);
}

VSOutput vert(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(0.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 0.0f),
        float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f)
    };

    UIShape shape = ShapeBuffer[instanceID];
    float2 halfSize = shape.rect.zw * 0.5f;
    float pad = max(shape.params.y + shape.params.z, 1.0f);
    float2 expandedMin = shape.rect.xy - pad;
    float2 expandedSize = shape.rect.zw + pad * 2.0f;
    float2 uv = corners[vertexID];
    float2 pixel = expandedMin + expandedSize * uv;
    float2 center = shape.rect.xy + halfSize;
    float2 clip = float2(pixel.x / ui_screen_scale.x * 2.0f - 1.0f, 1.0f - pixel.y / ui_screen_scale.y * 2.0f);

    VSOutput o;
    o.position = float4(clip, 0.0f, 1.0f);
    o.local = pixel - center;
    o.halfSize = halfSize;
    o.params = shape.params;
    o.color = shape.color;
    o.borderColor = shape.borderColor;
    o.shape = shape.shape;
    return o;
}

float4 frag(VSOutput input) : SV_Target0
{
    float radius = input.params.x;
    float border = input.params.y;
    float softness = max(input.params.z, 0.0f);
    float sd = ShapeDistance(input.local, input.halfSize, radius, input.shape);
    float aa = max(fwidth(sd), softness);
    float fillCoverage = saturate(0.5f - sd / aa);
    float4 fill = UnpackColor4Uint(input.color);

    if (border > 0.0f)
    {
        float2 innerHalf = max(input.halfSize - border, 0.0f);
        float innerRadius = max(radius - border, 0.0f);
        float innerSd = ShapeDistance(input.local, innerHalf, innerRadius, input.shape);
        float innerCoverage = saturate(0.5f - innerSd / aa);
        float4 stroke = UnpackColor4Uint(input.borderColor);
        float4 color = lerp(stroke, fill, innerCoverage);
        color.a *= fillCoverage;
        color.rgb *= color.a;
        return color;
    }

    fill.a *= fillCoverage;
    fill.rgb *= fill.a;
    return fill;
}
