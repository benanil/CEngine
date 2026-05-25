#include "../Bitpack.hlsl"

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 rect : TEXCOORD1;
    nointerpolation float4 clip : TEXCOORD2;
    nointerpolation uint tintColor : COLOR0;
    nointerpolation float radius : TEXCOORD3;
};

cbuffer ui_image_params : register(b0, space1)
{
    float4 ui_screen_scale;
    float4 ui_image_rect;
    float4 ui_image_uv;
    float4 ui_image_clip;
    uint ui_image_tint;
    float ui_image_radius;
    float2 ui_image_padding;
};

Texture2D<float4> ImageTexture : register(t0, space2);
SamplerState ImageSampler : register(s0, space2);

float EvaluateSdf(float2 p, float2 halfSize, float radius)
{
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 d = abs(p) - (halfSize - radius);
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - radius;
}

VSOutput vert(uint vertexID : SV_VertexID)
{
    float2 quadUV = float2((vertexID == 2u || vertexID == 3u || vertexID == 5u),
                          (vertexID == 1u || vertexID == 4u || vertexID == 5u));
    float2 pixel = ui_image_rect.xy + ui_image_rect.zw * quadUV;
    float2 clip = float2(pixel.x / ui_screen_scale.x * 2.0f - 1.0f, 1.0f - pixel.y / ui_screen_scale.y * 2.0f);

    VSOutput o;
    o.position = float4(clip, 0.0f, 1.0f);
    o.uv = ui_image_uv.xy + ui_image_uv.zw * quadUV;
    o.rect = ui_image_rect;
    o.clip = ui_image_clip;
    o.tintColor = ui_image_tint;
    o.radius = ui_image_radius;
    return o;
}

float4 frag(VSOutput input) : SV_Target0
{
    if (input.position.x < input.clip.x || input.position.y < input.clip.y || input.position.x > input.clip.z || input.position.y > input.clip.w) discard;

    float2 halfSize = input.rect.zw * 0.5f;
    float2 local = input.position.xy - (input.rect.xy + halfSize);
    float sd = EvaluateSdf(local, halfSize, input.radius);
    float coverage = saturate(0.5f - sd / max(fwidth(sd), 0.001f));

    float4 color = ImageTexture.Sample(ImageSampler, input.uv);
    float4 tint = UnpackColor4Uint(input.tintColor);
    if (tint.a > 0.0f) color *= tint;

    color.a *= coverage;
    color.rgb *= color.a;
    return color;
}
