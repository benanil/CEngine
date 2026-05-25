struct ShadowCascadeBuffer
{
    float4 lightViewProj[12];
    float4 splitDistances;
};

float4 MulShadowCascade(ShadowCascadeBuffer cascades, uint cascadeIndex, float4 p)
{
    uint row = cascadeIndex * 4u;
    return float4(dot(p, cascades.lightViewProj[row + 0u]),
                  dot(p, cascades.lightViewProj[row + 1u]),
                  dot(p, cascades.lightViewProj[row + 2u]),
                  dot(p, cascades.lightViewProj[row + 3u]));
}
