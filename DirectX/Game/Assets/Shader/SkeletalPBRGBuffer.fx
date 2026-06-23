#include "PBRMaterialCB.h"

// Skeletal mesh 用 PBR G-Buffer パス（スキニング + MRT 書き込み）。
// GBuffer レイアウトは PBRGBuffer.fx と同一。

struct VSInput
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float2 uv          : TEXCOORD0;
    float4 tangent     : TANGENT;
    float4 boneWeights : BLENDWEIGHT;
    uint4  boneIndices : BLENDINDICES;
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

struct PSOutput
{
    float4 gbuffer0 : SV_Target0;
    float4 gbuffer1 : SV_Target1;
    float4 gbuffer2 : SV_Target2;
    float4 gbuffer3 : SV_Target3;
};

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

cbuffer BonesCB : register(b4)
{
    float4x4 boneMatrices[128];
};

Texture2D baseColorTex         : register(t0);
Texture2D normalTex            : register(t1);
Texture2D metallicRoughnessTex : register(t2);
Texture2D emissiveTex          : register(t3);
SamplerState samp              : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput)0;

    float4 weights  = input.boneWeights;
    float weightSum = weights.x + weights.y + weights.z + weights.w;
    weights = (weightSum > 0.0001) ? (weights / weightSum) : float4(1.0, 0.0, 0.0, 0.0);

    float4x4 skinMatrix =
        boneMatrices[input.boneIndices.x] * weights.x +
        boneMatrices[input.boneIndices.y] * weights.y +
        boneMatrices[input.boneIndices.z] * weights.z +
        boneMatrices[input.boneIndices.w] * weights.w;

    float4 skinnedPos     = mul(skinMatrix, float4(input.position, 1.0));
    float3 skinnedNormal  = mul((float3x3)skinMatrix, input.normal);
    float3 skinnedTangent = mul((float3x3)skinMatrix, input.tangent.xyz);
    if (abs(skinnedPos.w) < 0.0001)
    {
        skinnedPos     = float4(input.position, 1.0);
        skinnedNormal  = input.normal;
        skinnedTangent = input.tangent.xyz;
    }

    float4 worldPos = mul(world, skinnedPos);
    o.worldPos = worldPos.xyz;
    o.svPos    = mul(projection, mul(view, worldPos));

    float3 wN    = normalize(mul((float3x3)world, skinnedNormal));
    float3 wTraw = mul((float3x3)world, skinnedTangent);
    float3 wT    = (dot(wTraw, wTraw) > 0.0001) ? normalize(wTraw) : float3(1.0, 0.0, 0.0);
    float3 wB    = cross(wN, wT) * input.tangent.w;
    o.N  = wN;
    o.T  = wT;
    o.B  = wB;
    o.uv = input.uv;
    return o;
}

float3 DecodeNM(float2 rg)
{
    float3 n;
    n.xy = rg * 2.0 - 1.0;
    n.z  = sqrt(saturate(1.0 - dot(n.xy, n.xy)));
    return n;
}

float3 TBNToWorld(float3 tN, float3 T, float3 B, float3 N)
{
    return normalize(tN.x * T + tN.y * B + tN.z * N);
}

PSOutput PSMain(PSInput input)
{
    float3 baseColor = baseColorTex.Sample(samp, input.uv).rgb;

    float3 N;
    if (PBR_HasNormalMap())
        N = TBNToWorld(DecodeNM(normalTex.Sample(samp, input.uv).rg), input.T, input.B, input.N);
    else
        N = normalize(input.N);

    float mat_metallic  = metallic;
    float mat_roughness = roughness;
    if (PBR_HasMetallicRoughness())
    {
        float2 mr   = metallicRoughnessTex.Sample(samp, input.uv).rg;
        mat_metallic  = mr.r;
        mat_roughness = mr.g;
    }

    float3 emissive = PBR_HasEmissiveMap()
        ? emissiveTex.Sample(samp, input.uv).rgb
        : float3(0.0, 0.0, 0.0);

    float pixelTag = PBR_ReceivesShadow() ? 2.0 : 1.0;

    PSOutput o;
    o.gbuffer0 = float4(baseColor, mat_metallic);
    o.gbuffer1 = float4(N, mat_roughness);
    o.gbuffer2 = float4(input.worldPos, specular);
    o.gbuffer3 = float4(emissive * emissiveScale, pixelTag);
    return o;
}
