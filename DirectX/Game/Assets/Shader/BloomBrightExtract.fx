Texture2D<float4>   g_Scene  : register(t0);
RWTexture2D<float4> g_Bright : register(u0);

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

    float4 c   = g_Scene[id.xy];
    float  lum = dot(c.rgb, float3(0.2126, 0.7152, 0.0722));
    g_Bright[id.xy] = (lum > g_Threshold) ? c : float4(0.0, 0.0, 0.0, 0.0);
}
