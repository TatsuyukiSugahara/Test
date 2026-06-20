Texture2D<float4>   g_Scene  : register(t0);
Texture2D<float4>   g_Bloom  : register(t1);
RWTexture2D<float4> g_Output : register(u0);

cbuffer BloomCB : register(b0)
{
    float g_Threshold;
    float g_Intensity;
    uint  g_Width;
    uint  g_Height;
    uint  g_IsVertical;
    uint3 g_Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Width || id.y >= g_Height) return;

    float4 scene = g_Scene[id.xy];
    float4 bloom = g_Bloom[id.xy] * g_Intensity;
    g_Output[id.xy] = saturate(scene + bloom);
}
