#include "PBRFunctions.fx"

// SSR の差分合成（設計書/SSR設計.md §0.2・§1）。
//
// ライティング（PSMainIBL）は変えず、SSR の差分だけをシーン RT へ加算する:
//   rgb = ssr.a × (ssr.rgb − 鏡面キューブ × iblIntensity) × (F0·A + B)
//   α   = 1（固定）
// 確信度 1 なら鏡面項がキューブから当たった色に置き換わり、0 なら何も足さない（0 × … = 0）。
// 負の値の加算は float RT でクランプされない前提（§0.1）。
//
// ★α は必ず 1★ BlendMode::Additive は src.rgb × src.a + dst なので、確信度を α に入れると
//   差分が二重に弱まる。確信度は rgb に掛けて済ませる（§0.6-1）。
//
// フルスクリーントライアングル: Draw(3, 0) で呼ぶ（頂点バッファ不要）。VS は PBRLighting.fx と同じく
// z = 0（深度無効パス専用）。G-Buffer の画素だけを描く（空の画素は pixelTag で clip）。
//
// スロット（PBRLighting.fx の PSMainIBL と同じ配置）:
//   b1 = LightingCB（cameraPosition / globalSpecularScale）、b2 = SSRCompositeCB
//   t6 = 鏡面キューブ、t7 = BRDF LUT、s0 = IBL のサンプラ、t8-t11 = GBuffer0-3

struct VSOutput
{
    float4 svPos : SV_POSITION;
    float2 uv    : TEXCOORD0;
};

// GBuffer SRV は t8-t11 に固定（PBRLighting.fx と同じ）
Texture2D gbuffer0 : register(t8);   // baseColor.rgb + metallic
Texture2D gbuffer1 : register(t9);   // N.xyz + roughness
Texture2D gbuffer2 : register(t10);  // worldPos.xyz + specular
Texture2D gbuffer3 : register(t11);  // emissive*scale + pixelTag

// IBL（PBRLighting.fx と同じスロット。irradiance は使わないので宣言しない）
TextureCube  gIblSpecular : register(t6);
Texture2D    gIblBrdfLut  : register(t7);
SamplerState gIblSampler  : register(s0);

// SSRCompositeCommand.h の SSRCompositeCBData と先頭を一致させること（C++ 側は 192B に詰める）
cbuffer SSRCompositeCB : register(b2)
{
    float  iblIntensity;
    float  iblSpecularMaxMip;
    float2 _ssrPad0;
    // P1 のテスト用（SSRRenderer の testMode）。トレースがまだ無いので、ssr（当たった色 + 確信度）を
    // CB の固定値で与える。P2 でトレースの出力 RT（テクスチャ入力）に置き換える。
    float4 ssrTestValue;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    o.uv    = float2((vertexID << 1) & 2, vertexID & 2);
    o.svPos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    int2 coord = int2(input.svPos.xy);

    float4 g3 = gbuffer3.Load(int3(coord, 0));

    // pixelTag が 0.5 未満なら空背景ピクセル → 描画しない（ライティングと同じ判定）
    clip(g3.a - 0.5);

    float4 g0 = gbuffer0.Load(int3(coord, 0));
    float4 g1 = gbuffer1.Load(int3(coord, 0));
    float4 g2 = gbuffer2.Load(int3(coord, 0));

    // PBRLighting.fx の LoadSurface / PSMainIBL と同じ式で表面と視線を作る
    // （ライティングが足した鏡面キューブの項と同じ値を差し引くため）
    float3 baseColor = g0.rgb;
    float  metallic  = g0.a;
    float3 N         = normalize(g1.xyz);
    float  roughness = g1.w;
    float3 worldPos  = g2.xyz;
    float  specular  = g2.w;
    float3 V         = normalize(cameraPosition - worldPos);

    // P1: トレースが無いので CB の固定値。P2 でトレースの出力 RT の Load に置き換える。
    float4 ssr = ssrTestValue;

    float3 prefiltered;
    float3 specWeight;
    EvalIBLSpecular(N, V, baseColor, metallic, roughness, specular,
                    gIblSpecular, gIblBrdfLut, gIblSampler, iblSpecularMaxMip,
                    prefiltered, specWeight);

    // 当たった色には強度を掛けない（キューブ側だけに掛けて差し引く。§0.2）
    float3 delta = ssr.a * (ssr.rgb - prefiltered * iblIntensity) * specWeight;

    return float4(delta, 1.0);
}
