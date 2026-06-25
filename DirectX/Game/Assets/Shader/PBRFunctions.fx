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
// = G(NdotV, NdotL, alpha) / (4 * NdotV * NdotL) を一体化。
// スペキュラ項: D * V_SmithGGX * F（追加の除算は不要）
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

// Deferred Lighting パス用: 全ライト合算（シャドウ適用前）
float3 ComputePBRLighting(float3 worldPos, float3 N,
                          float3 baseColor, float metallic, float roughness, float specular,
                          float3 preScaledEmissive)
{
    float3 V = normalize(cameraPosition - worldPos);

    // 簡易 ambient（Phase 3 で IBL に置換予定）
    float3 color = ambient.color * ambient.intensity * baseColor * (1.0 - metallic);

    for (uint di = 0; di < directionalLightCount; ++di)
    {
        float3 L = normalize(-directionals[di].direction);
        if (dot(N, L) > 0.0)
            color += EvalDisneyBRDF(baseColor, metallic, roughness, specular * globalSpecularScale, N, V, L)
                     * directionals[di].color * directionals[di].intensity;
    }

    for (uint i = 0; i < pointLightCount; ++i)
    {
        float3 delta = pointLights[i].position - worldPos;
        float  dist  = length(delta);
        if (dist < pointLights[i].range)
        {
            float3 L   = delta / max(dist, 1e-5);
            float  att = PBR_Attenuation(dist, pointLights[i].range);
            color += EvalDisneyBRDF(baseColor, metallic, roughness, specular, N, V, L)
                     * pointLights[i].color * pointLights[i].intensity * att;
        }
    }

    return color + preScaledEmissive;
}
