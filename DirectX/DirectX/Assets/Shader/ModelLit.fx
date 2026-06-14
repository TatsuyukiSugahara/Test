#include "Lighting.fx"

// VertexData メモリレイアウト順に宣言する（D3D11_APPEND_ALIGNED_ELEMENT 対応）
// position(float3) → normal(float3) → uv(float2) → tangent(float4)
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

    // 法線・タンジェントを world 空間へ変換（非一様スケールを考慮する場合は逆転置が必要）
    float3 wN    = normalize(mul((float3x3)world, input.normal));
    float3 wTraw = mul((float3x3)world, input.tangent.xyz);
    // タンジェントが未初期化 (0 or NaN) の場合 normalize で NaN になるのを防ぐ
    float3 wT = (dot(wTraw, wTraw) > 0.0001) ? normalize(wTraw) : float3(1.0, 0.0, 0.0);
    float3 wB = cross(wN, wT) * input.tangent.w;

    o.N  = wN;
    o.T  = wT;
    o.B  = wB;
    o.uv = input.uv;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
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

    float3 albedo  = albedoSample.rgb;
    float specMask = HasSpecularMap() ? specularTex.Sample(samp, input.uv).r : 1.0;

    float3 emissive = HasEmissiveMap()
        ? emissiveTex.Sample(samp, input.uv).rgb
        : float3(0.0, 0.0, 0.0);

    float3 lit = ComputeLighting(input.worldPos, N, albedo, specMask, emissive);
    return float4(lit, albedoSample.a);
}
