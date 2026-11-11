#include "LightingCB.h"
// Disney Principled BRDF 関数群。
// PBR シェーダーのみ include する（MaterialCB.h とは競合しない）。

static const float PBR_PI = 3.14159265358979f;

// (1-u)^5
float SchlickFresnel(float u)
{
    float m = 1.0 - saturate(u);
    float m2 = m * m;
    return m2 * m2 * m;
}

// Disney diffuse（レトロ反射補正付き）
float3 DisneyDiffuse(float3 baseColor, float roughness,
                     float NdotL, float NdotV, float LdotH)
{
    float fl   = SchlickFresnel(NdotL);
    float fv   = SchlickFresnel(NdotV);
    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    return (baseColor / PBR_PI) * lerp(1.0, fd90, fl) * lerp(1.0, fd90, fv);
}

// GGX (Trowbridge-Reitz) 法線分布関数
float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PBR_PI * d * d);
}

// Correlated Smith GGX Visibility term（Heitz 2014）
float V_SmithGGX(float NdotV, float NdotL, float alpha)
{
    float a2   = alpha * alpha;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

// Schlick Fresnel
float3 F_Schlick(float3 F0, float VdotH)
{
    return F0 + (1.0 - F0) * SchlickFresnel(VdotH);
}

// 1ライト分の Disney BRDF × NdotL。caller がライト輝度（color * intensity）と乗算する。
float3 EvalDisneyBRDF(float3 baseColor, float metallic, float roughness, float specular,
                      float3 N, float3 V, float3 L)
{
    float3 H     = normalize(L + V);
    float  NdotL = saturate(dot(N, L));
    float  NdotV = max(dot(N, V), 1e-5);
    float  NdotH = saturate(dot(N, H));
    float  VdotH = saturate(dot(V, H));
    float  LdotH = saturate(dot(L, H));

    float  alpha = max(roughness * roughness, 0.001);

    // F0: 誘電体は specular * 0.08（specular=0.5 → F0=0.04）, 金属は baseColor
    float3 F0   = lerp(0.08 * specular, baseColor, metallic);
    float3 F    = F_Schlick(F0, VdotH);
    float  D    = D_GGX(NdotH, alpha);
    float  Vis  = V_SmithGGX(NdotV, NdotL, alpha);
    float3 spec = D * Vis * F;

    // 誘電体のみ diffuse（金属は diffuse なし）
    float3 kD   = (1.0 - F) * (1.0 - metallic);
    float3 diff = kD * DisneyDiffuse(baseColor, roughness, NdotL, NdotV, LdotH);

    return (diff + spec) * NdotL;
}

float PBR_Attenuation(float dist, float range)
{
    float x = saturate(1.0 - dist / range);
    return x * x;
}

// IBL の鏡面部分（split-sum 近似。設計書/IBL設計.md §3、設計書/SSR設計.md §1）。
// EvalIBL と SSRComposite.fx が共用する（SSR はライティングが足した鏡面キューブの項を差し引くため、
// 同じ式で求める必要がある）。強度は掛けない（呼び出し側が掛ける）。
//   prefiltered: 前置フィルタ済み鏡面キューブの色（mip = roughness × maxMip）
//   specWeight : F0·A + B（A, B は BRDF LUT）
// 鏡面の項は prefiltered × specWeight。F0 は直接光（EvalDisneyBRDF）と同じ定義。
void EvalIBLSpecular(float3 N, float3 V,
                     float3 baseColor, float metallic, float roughness, float specular,
                     TextureCube specCube, Texture2D brdfLut, SamplerState s,
                     float maxMip,
                     out float3 prefiltered, out float3 specWeight)
{
    float3 F0    = lerp(0.08 * specular * globalSpecularScale, baseColor, metallic);
    float  NdotV = max(dot(N, V), 1e-5);
    float3 R     = reflect(-V, N);
    float2 ab    = brdfLut.SampleLevel(s, float2(NdotV, roughness), 0).rg;
    prefiltered  = specCube.SampleLevel(s, R, roughness * maxMip).rgb;
    specWeight   = F0 * ab.x + ab.y;
}

// IBL（split-sum 近似。設計書/IBL設計.md §3）の環境光の寄与。
// テクスチャは引数で受け、宣言は持たない（宣言は使うエントリのファイル側。PSMain に持ち込まないため）。
//   irrCube : irradiance キューブ（E / π を格納）
//   specCube: 前置フィルタ済み鏡面キューブ（mip = roughness × maxMip）
//   brdfLut : u = NdotV, v = roughness, rg = (A, B)
// specular（引数）= 材質の F0 スケール（GBuffer2.w）。F0 は直接光（EvalDisneyBRDF）と同じ定義。
// 鏡面部分は EvalIBLSpecular に括り出してある。式と演算順は括り出す前と同じ
// （F0 / NdotV は Fr のために同じ式でもう一度求める。PSMainIBL の出力を変えないため）。
float3 EvalIBL(float3 N, float3 V,
               float3 baseColor, float metallic, float roughness, float specular,
               TextureCube irrCube, TextureCube specCube, Texture2D brdfLut, SamplerState s,
               float maxMip, float intensity)
{
    float3 specEnv;
    float3 specWeight;
    EvalIBLSpecular(N, V, baseColor, metallic, roughness, specular,
                    specCube, brdfLut, s, maxMip, specEnv, specWeight);

    float3 F0      = lerp(0.08 * specular * globalSpecularScale, baseColor, metallic);
    float  NdotV   = max(dot(N, V), 1e-5);
    float3 Fr      = F0 + (max(1.0 - roughness, F0) - F0) * SchlickFresnel(NdotV);  // roughness 考慮の Fresnel
    float3 kD      = (1.0 - Fr) * (1.0 - metallic);
    float3 diffIBL = irrCube.SampleLevel(s, N, 0).rgb * baseColor * kD;
    float3 specIBL = specEnv * specWeight;
    return (diffIBL + specIBL) * intensity;
}

// Deferred Lighting パス用: 全ライト合算。
// dirShadow  : 各ディレクショナルライトのシャドウ係数 (float4, x=light0 ... w=light3)
// environment: 環境光の寄与（PSMain は簡易 ambient、PSMainIBL は EvalIBL の結果）。
//              ライトより先に足す（従来の簡易 ambient と同じ加算順。PSMain の出力を変えないため）
float3 ComputePBRLighting(float3 worldPos, float3 N,
                          float3 baseColor, float metallic, float roughness, float specular,
                          float3 preScaledEmissive,
                          float4 dirShadow,
                          float3 environment)
{
    float3 V = normalize(cameraPosition - worldPos);

    float3 color = environment;

    float dirShadowArr[4] = { dirShadow.x, dirShadow.y, dirShadow.z, dirShadow.w };
    for (uint di = 0; di < directionalLightCount; ++di)
    {
        float  shadow_i = dirShadowArr[di];
        float3 L = normalize(-directionals[di].direction);
        if (dot(N, L) > 0.0)
            color += EvalDisneyBRDF(baseColor, metallic, roughness, specular * globalSpecularScale, N, V, L)
                     * directionals[di].color * directionals[di].intensity * shadow_i;
    }

    for (uint i = 0; i < pointLightCount; ++i)
    {
        float3 delta = pointLights[i].position - worldPos;
        float  dist  = length(delta);
        if (dist < pointLights[i].range)
        {
            float3 L   = delta / max(dist, 1e-5);
            float  att = PBR_Attenuation(dist, pointLights[i].range);
            color += EvalDisneyBRDF(baseColor, metallic, roughness, specular * globalSpecularScale, N, V, L)
                     * pointLights[i].color * pointLights[i].intensity * att;
        }
    }

    return color + preScaledEmissive;
}
