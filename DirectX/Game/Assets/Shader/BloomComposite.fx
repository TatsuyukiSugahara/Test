#include "Tonemap.fx"

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
    float g_Exposure;     // 露出倍率
    uint  g_TonemapMode;  // 0=None 1=Reinhard 2=ReinhardExt 3=ACES 4=Uncharted2
    float g_WhitePoint;   // ReinhardExt 用
    uint  g_ApplyGamma;   // 1 ならトーンマップ後に sRGB エンコード
    uint3 g_Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Width || id.y >= g_Height) return;

    // HDR 空間でシーンとブルームを合成する (まだクランプしない)。
    float3 scene = g_Scene[id.xy].rgb;
    float3 bloom = g_Bloom[id.xy].rgb * g_Intensity;
    float3 hdr   = scene + bloom;

    // HDR → LDR トーンマップ (露出を乗算してから演算子を適用)。
    float3 ldr = ApplyTonemap(hdr, g_TonemapMode, g_Exposure, g_WhitePoint);

    // ガンマ空間パイプラインでは通常不要 (既定 off)。
    if (g_ApplyGamma != 0)
        ldr = LinearToGamma(ldr);

    g_Output[id.xy] = float4(saturate(ldr), 1.0);
}
