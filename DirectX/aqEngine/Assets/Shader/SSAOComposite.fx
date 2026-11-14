#include "PBRFunctions.fx"

// SSAO の差分合成（設計書/SSAO設計.md §0.2・§1）。
//
// ライティング（PSMain / PSMainIBL）は変えず、拡散の環境光の遮蔽分だけをシーン RT へ加算する:
//   rgb = −(1 − ao) × 拡散の環境光
//   α   = 1（固定）
// ao = 1 なら何も足さない（0 を足す）。負の値の加算は float RT でクランプされない前提（SSR P1 で実測済み）。
// 拡散の環境光は G-Buffer からライティングと同じ式で計算し直す:
//   PSMain   : PBRLighting.fx の PSMain の ambient の式と一致させること
//   PSMainIBL: EvalIBLDiffuse × iblIntensity（EvalIBL の拡散部分と同じ式・同じ強度）
//
// ★α は必ず 1★ BlendMode::Additive は src.rgb × src.a + dst。
//
// フルスクリーントライアングル: Draw(3, 0) で呼ぶ（頂点バッファ不要）。VS は SSRComposite.fx と同じく
// z = 0（深度無効パス専用）。G-Buffer の画素だけを描く（空の画素は pixelTag で clip）。
//
// ao（ぼかした AO の RT の r）は t0 を同じ画素で Load する。デバッグ表示（debugView）が有効なら
// ao をグレーで返す。そのときコマンドは BlendMode::Opaque で描く。
//
// スロット:
//   b1 = LightingCB（ambient / cameraPosition / globalSpecularScale）、b2 = SSAOCompositeCB
//   t0 = ぼかした AO、t5 = irradiance キューブ（PSMainIBL だけが参照）、s0 = IBL のサンプラ、
//   t8-t11 = GBuffer0-3

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

// ぼかした AO（r = 遮蔽されない割合。1 = 遮蔽なし）
Texture2D gAO : register(t0);

// IBL（PSMainIBL だけが参照する。PSMain から参照しない。設計書/IBL設計.md §0.2）
TextureCube  gIblIrradiance : register(t5);
SamplerState gIblSampler    : register(s0);

// testMode の値（SSAORenderer::TEST_MODE_*。デバッグ用）
static const uint TEST_MODE_AO_ONE  = 1;  // ao を 1 に固定（差分 0）
static const uint TEST_MODE_AO_ZERO = 2;  // ao を 0 に固定（拡散の環境光を全部差し引く）

// SSAOCompositeCommand.h の SSAOCompositeCBData と先頭を一致させること（C++ 側は 192B に詰める）
cbuffer SSAOCompositeCB : register(b2)
{
    float iblIntensity;
    float iblSpecularMaxMip;  // 未使用（IBLCB と並びを揃えるため）
    // デバッグ用（SSAORenderer の testMode）。1 / 2 なら AO を読まず ao を固定する
    uint  testMode;
    // デバッグ表示（SSAORenderer の debugView）。0 以外なら ao をグレーで返す
    uint  debugView;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    o.uv    = float2((vertexID << 1) & 2, vertexID & 2);
    o.svPos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// ao を求める（testMode が 0 のときだけ AO の RT を読む）
float LoadAO(int2 coord)
{
    if (testMode == TEST_MODE_AO_ONE)  { return 1.0; }
    if (testMode == TEST_MODE_AO_ZERO) { return 0.0; }
    return gAO.Load(int3(coord, 0)).r;
}

// 環境光: 簡易 ambient（t5 / s0 を参照しない）
float4 PSMain(VSOutput input) : SV_TARGET
{
    int2 coord = int2(input.svPos.xy);

    float4 g3 = gbuffer3.Load(int3(coord, 0));

    // pixelTag が 0.5 未満なら空背景ピクセル → 描画しない（ライティングと同じ判定）
    clip(g3.a - 0.5);

    float ao = LoadAO(coord);

    // デバッグ表示: ao をグレーで返す（コマンド側は Opaque で上書きする）
    if (debugView != 0) {
        return float4(ao, ao, ao, 1.0);
    }

    float4 g0 = gbuffer0.Load(int3(coord, 0));

    float3 baseColor = g0.rgb;
    float  metallic  = g0.a;

    // PBRLighting.fx の PSMain の ambient の式と一致させること（同じ並び・同じ演算順）
    float3 environment = ambient.color * ambient.intensity * baseColor * (1.0 - metallic);

    return float4(-(1.0 - ao) * environment, 1.0);
}

// 環境光: IBL の拡散（EvalIBLDiffuse × iblIntensity）
float4 PSMainIBL(VSOutput input) : SV_TARGET
{
    int2 coord = int2(input.svPos.xy);

    float4 g3 = gbuffer3.Load(int3(coord, 0));

    clip(g3.a - 0.5);

    float ao = LoadAO(coord);

    if (debugView != 0) {
        return float4(ao, ao, ao, 1.0);
    }

    float4 g0 = gbuffer0.Load(int3(coord, 0));
    float4 g1 = gbuffer1.Load(int3(coord, 0));
    float4 g2 = gbuffer2.Load(int3(coord, 0));

    // PBRLighting.fx の LoadSurface / PSMainIBL と同じ式で表面と視線を作る
    float3 baseColor = g0.rgb;
    float  metallic  = g0.a;
    float3 N         = normalize(g1.xyz);
    float  roughness = g1.w;
    float3 worldPos  = g2.xyz;
    float  specular  = g2.w;
    float3 V         = normalize(cameraPosition - worldPos);

    float3 diffuse = EvalIBLDiffuse(N, V, baseColor, metallic, roughness, specular,
                                    gIblIrradiance, gIblSampler) * iblIntensity;

    return float4(-(1.0 - ao) * diffuse, 1.0);
}
