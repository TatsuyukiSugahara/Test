#include "LightingCB.h"
#include "MaterialCB.h"

// Normal map RG [0,1] → tangent 空間法線 [-1,1]、z を再構築（BC5 対応）
float3 DecodeNormalMap(float2 rg)
{
    float3 n;
    n.xy = rg * 2.0 - 1.0;
    n.z  = sqrt(saturate(1.0 - dot(n.xy, n.xy)));
    return n;
}

// tangent 空間法線を world 空間へ変換（TBN は行ベクトル）
float3 TangentToWorld(float3 tangentNormal, float3 T, float3 B, float3 N)
{
    return normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
}

// Lambert 拡散反射
float3 CalcDiffuse(float3 N, float3 L, float3 lightColor, float intensity)
{
    return lightColor * intensity * saturate(dot(N, L));
}

// Blinn-Phong 鏡面反射
float3 CalcSpecular(float3 N, float3 H, float3 lightColor, float intensity, float shininess, float mask)
{
    return lightColor * intensity * pow(saturate(dot(N, H)), shininess) * mask;
}

// Point light 二乗減衰
float CalcAttenuation(float dist, float range)
{
    float x = saturate(1.0 - dist / range);
    return x * x;
}

// ライティング計算コア。
// dirShadow: 各ディレクショナルライトのシャドウ係数 (x=light0, y=light1, z=light2, w=light3)
//            フォワードシェーダーからは float4(1,1,1,1) を渡す（シャドウなし）。
float3 ComputeLightingEx(
    float3 worldPos,
    float3 N,
    float3 albedo,
    float  specMask,
    float3 preScaledEmissive,
    float  gloss,
    float  specularIntensity,
    float4 dirShadow)
{
    float3 V        = normalize(cameraPosition - worldPos);
    float  shininess = exp2(gloss * 10.0 + 1.0);

    // Ambient
    float3 color = ambient.color * ambient.intensity * albedo;

    // Directional lights (ライトごとにシャドウを適用)
    float scaledSpec = specularIntensity * globalSpecularScale;
    float dirShadowArr[4] = { dirShadow.x, dirShadow.y, dirShadow.z, dirShadow.w };
    for (uint di = 0; di < directionalLightCount; ++di)
    {
        float  shadow_i = dirShadowArr[di];
        float3 L = normalize(-directionals[di].direction);
        float3 H = normalize(L + V);
        color += albedo * CalcDiffuse(N, L, directionals[di].color, directionals[di].intensity) * shadow_i;
        color += CalcSpecular(N, H, directionals[di].color, directionals[di].intensity * scaledSpec, shininess, specMask) * shadow_i;
    }

    // Point lights
    for (uint i = 0; i < pointLightCount; ++i)
    {
        float3 delta = pointLights[i].position - worldPos;
        float  dist  = length(delta);
        float3 L     = delta / max(dist, 0.0001);
        float3 H     = normalize(L + V);
        float  atten  = CalcAttenuation(dist, pointLights[i].range);
        float  intens = pointLights[i].intensity * atten;
        color += albedo * CalcDiffuse(N, L, pointLights[i].color, intens);
        color += CalcSpecular(N, H, pointLights[i].color, intens * scaledSpec, shininess, specMask);
    }

    color += preScaledEmissive;
    return color;
}

// フォワードシェーダー用ラッパー（シャドウなし: dirShadow = all 1.0）。
float3 ComputeLighting(
    float3 worldPos,
    float3 N,
    float3 albedo,
    float  specMask,
    float3 emissive)
{
    return ComputeLightingEx(worldPos, N, albedo, specMask,
                             emissive * emissiveScale,
                             gloss, specularIntensity,
                             float4(1.0, 1.0, 1.0, 1.0));
}
