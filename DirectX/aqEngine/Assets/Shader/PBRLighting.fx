#include "PBRFunctions.fx"
#include "ShadowSampling.fx"

// Disney Principled BRDF ディファードライティングパス。
// GBuffer レイアウト（PBRGBuffer.fx / SkeletalPBRGBuffer.fx / TerrainPBRGBuffer.fx と一致）:
//   GBuffer0: baseColor.rgb + metallic
//   GBuffer1: N.xyz + roughness
//   GBuffer2: worldPos.xyz + specular(F0 scale)
//   GBuffer3: emissive*scale + pixelTag
// フルスクリーントライアングル: Draw(3, 0) で呼ぶ（頂点バッファ不要）。
//
// エントリ:
//   PSMain   : 環境光は簡易 ambient（AmbientLight の 1 色）。t5-t7 / s0 / b2 を参照しない
//   PSMainIBL: 環境光は IBL（設計書/IBL設計.md §3）。t5-t7 / s0 / b2 を参照する
//   2 つの差は環境光の 1 項だけ。GBuffer 読み取りと影は LoadSurface に括り出してある。

struct VSOutput
{
    float4 svPos : SV_POSITION;
    float2 uv    : TEXCOORD0;
};

// GBuffer SRV は t8-t11 に固定（t4 はシャドウマップ）
Texture2D gbuffer0 : register(t8);   // baseColor.rgb + metallic
Texture2D gbuffer1 : register(t9);   // N.xyz + roughness
Texture2D gbuffer2 : register(t10);  // worldPos.xyz + specular
Texture2D gbuffer3 : register(t11);  // emissive*scale + pixelTag

// IBL（PSMainIBL だけが参照する。PSMain から参照すると、IBL 無効時に未バインドの
// TextureCube が Metal の 2D フォールバックと型が合わない。設計書/IBL設計.md §0.2）
TextureCube  gIblIrradiance : register(t5);
TextureCube  gIblSpecular   : register(t6);
Texture2D    gIblBrdfLut    : register(t7);
SamplerState gIblSampler    : register(s0);

// IBLEnvironment.h の IBLCBData と先頭を一致させること（C++ 側は 192B に詰める）
cbuffer IBLCB : register(b2)
{
    float  iblIntensity;
    float  iblSpecularMaxMip;
    float2 _iblPad;
};

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    o.uv    = float2((vertexID << 1) & 2, vertexID & 2);
    o.svPos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// GBuffer から読んだ 1 画素分の表面情報（PSMain / PSMainIBL 共通）
struct PBRSurface
{
    float3 baseColor;
    float  metallic;
    float3 N;
    float  roughness;
    float3 worldPos;
    float  specular;
    float3 preScaledEmissive;
    float4 dirShadow;
};

// GBuffer 読み取りと影。空背景の画素はここで clip する。
PBRSurface LoadSurface(float4 svPos)
{
    int2 coord = int2(svPos.xy);

    float4 g0 = gbuffer0.Load(int3(coord, 0));
    float4 g1 = gbuffer1.Load(int3(coord, 0));
    float4 g2 = gbuffer2.Load(int3(coord, 0));
    float4 g3 = gbuffer3.Load(int3(coord, 0));

    // pixelTag が 0.5 未満なら空背景ピクセル → 描画しない
    float pixelTag = g3.a;
    clip(pixelTag - 0.5);

    // +4 はデカール非対象マーカー。シャドウ判定では下位値 (1/2) に戻す。
    float shadowTag = (pixelTag >= 4.5) ? (pixelTag - 4.0) : pixelTag;

    PBRSurface surf;
    surf.baseColor         = g0.rgb;
    surf.metallic          = g0.a;
    surf.N                 = normalize(g1.xyz);
    surf.roughness         = g1.w;
    surf.worldPos          = g2.xyz;
    surf.specular          = g2.w;
    surf.preScaledEmissive = g3.rgb;

    surf.dirShadow = float4(1.0, 1.0, 1.0, 1.0);
    if (shadowTag >= 1.5)
    {
        if (directionalLightCount > 0) surf.dirShadow.x = SampleShadowForLight(surf.worldPos, 0);
        if (directionalLightCount > 1) surf.dirShadow.y = SampleShadowForLight(surf.worldPos, 1);
        if (directionalLightCount > 2) surf.dirShadow.z = SampleShadowForLight(surf.worldPos, 2);
        if (directionalLightCount > 3) surf.dirShadow.w = SampleShadowForLight(surf.worldPos, 3);
    }
    return surf;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    PBRSurface surf = LoadSurface(input.svPos);

    // 簡易 ambient（従来と同じ式・同じ演算順。出力を変えない）
    // ★この式は SSAOComposite.fx の PSMain と一致させること（SSAO が同じ式で差し引く。設計書/SSAO設計.md §0.2）
    float3 environment = ambient.color * ambient.intensity * surf.baseColor * (1.0 - surf.metallic);

    float3 lit = ComputePBRLighting(surf.worldPos, surf.N, surf.baseColor, surf.metallic,
                                    surf.roughness, surf.specular, surf.preScaledEmissive,
                                    surf.dirShadow, environment);

    return float4(lit, 1.0);
}

float4 PSMainIBL(VSOutput input) : SV_TARGET
{
    PBRSurface surf = LoadSurface(input.svPos);

    // IBL が AmbientLight を置き換える（二重に足さない）
    float3 V = normalize(cameraPosition - surf.worldPos);
    float3 environment = EvalIBL(surf.N, V, surf.baseColor, surf.metallic,
                                 surf.roughness, surf.specular,
                                 gIblIrradiance, gIblSpecular, gIblBrdfLut, gIblSampler,
                                 iblSpecularMaxMip, iblIntensity);

    float3 lit = ComputePBRLighting(surf.worldPos, surf.N, surf.baseColor, surf.metallic,
                                    surf.roughness, surf.specular, surf.preScaledEmissive,
                                    surf.dirShadow, environment);

    return float4(lit, 1.0);
}
