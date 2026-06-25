#include "ShadowCB.h"

Texture2DArray         shadowMapArray : register(t4);
SamplerComparisonState shadowSampler  : register(s1);

// 3x3 グリッド PCF のサンプルオフセット（テクセル単位）
static const float2 kPcfOffsets[9] =
{
    float2(-1, -1), float2(0, -1), float2(1, -1),
    float2(-1,  0), float2(0,  0), float2(1,  0),
    float2(-1,  1), float2(0,  1), float2(1,  1),
};

// shadowMapArray の 1 テクセルあたりの UV サイズ（解像度 2048 固定）
static const float kShadowTexelSize = 1.0 / 2048.0;

// 指定ライトインデックス（配列スライス）のシャドウを PCF サンプリングする。
// 戻り値: 0.0=完全に影、1.0=完全に照らされている
float SampleShadowForLight(float3 worldPos, uint lightIndex)
{
    float4 lightClip = mul(lightViewProj[lightIndex], float4(worldPos, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;

    // シャドウマップ投影範囲外は影なし
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    float d      = ndc.z - depthBias;
    float uvStep = softness * kShadowTexelSize;

    float shadow = 0.0;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        shadow += shadowMapArray.SampleCmpLevelZero(
            shadowSampler, float3(uv + kPcfOffsets[i] * uvStep, (float)lightIndex), d);
    }
    return shadow / 9.0;
}

// 後方互換: ライト 0 のシャドウを返す
float SampleShadow(float3 worldPos, float viewDepth)
{
    return SampleShadowForLight(worldPos, 0);
}
