#include "Lighting.fx"

// ModelLit 用 G-Buffer パス (MRT 書き込み)。ライティング計算はしない。
// VSMain は ModelLit.fx と同一レイアウト。

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 svPos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 N        : TEXCOORD1;
    float3 T        : TEXCOORD2;
    float3 B        : TEXCOORD3;
    float2 uv       : TEXCOORD4;
};

// G-Buffer MRT 出力
struct PSOutput
{
    float4 gbuffer0 : SV_Target0;  // albedo.rgb + specMask
    float4 gbuffer1 : SV_Target1;  // N.xyz + gloss
    float4 gbuffer2 : SV_Target2;  // worldPos.xyz + specularIntensity
    float4 gbuffer3 : SV_Target3;  // emissive*emissiveScale + pixelTag(1=影なし, 2=影あり)
};

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

Texture2D albedoTex   : register(t0);
Texture2D normalTex   : register(t1);
Texture2D specularTex : register(t2);
Texture2D emissiveTex : register(t3);
SamplerState samp     : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput)0;

    float4 worldPos = mul(world, float4(input.position, 1.0));
    o.worldPos = worldPos.xyz;
    o.svPos    = mul(projection, mul(view, worldPos));

    float3 wN    = normalize(mul((float3x3)world, input.normal));
    float3 wTraw = mul((float3x3)world, input.tangent.xyz);
    float3 wT = (dot(wTraw, wTraw) > 0.0001) ? normalize(wTraw) : float3(1.0, 0.0, 0.0);
    float3 wB = cross(wN, wT) * input.tangent.w;

    o.N  = wN;
    o.T  = wT;
    o.B  = wB;
    o.uv = input.uv;
    return o;
}

PSOutput PSMain(PSInput input)
{
    float4 albedoSample = albedoTex.Sample(samp, input.uv);

    float3 N;
    if (HasNormalMap())
    {
        float3 tangentN = DecodeNormalMap(normalTex.Sample(samp, input.uv).rg);
        N = TangentToWorld(tangentN, input.T, input.B, input.N);
    }
    else
    {
        N = normalize(input.N);
    }

    float3 albedo   = albedoSample.rgb;
    float  specMask = HasSpecularMap() ? specularTex.Sample(samp, input.uv).r : 1.0;

    float3 emissive = HasEmissiveMap()
        ? emissiveTex.Sample(samp, input.uv).rgb
        : float3(0.0, 0.0, 0.0);

    // ReceivesShadow フラグをピクセルタグに変換
    // 0.0 = 空ピクセル（背景）, 1.0 = 影なし, 2.0 = 影あり
    float pixelTag = ReceivesShadow() ? 2.0 : 1.0;

    PSOutput o;
    o.gbuffer0 = float4(albedo, specMask);
    o.gbuffer1 = float4(N, gloss);
    o.gbuffer2 = float4(input.worldPos, specularIntensity);
    o.gbuffer3 = float4(emissive * emissiveScale, pixelTag);
    return o;
}
