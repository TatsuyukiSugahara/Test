#include "Lighting.fx"
#include "ShadowCB.h"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;  // [0,1] normalized → splat UV
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 svPos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 N        : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

// t0 = splatMap (R=layer0, G=layer1, B=layer2)
// t1 = layer0 albedo (grass)
// t2 = layer1 albedo (rock)
// t3 = layer2 albedo (dirt / sand)
Texture2D      splatMap      : register(t0);
Texture2D      layer0        : register(t1);
Texture2D      layer1        : register(t2);
Texture2D      layer2        : register(t3);
Texture2DArray shadowMapArray : register(t4);
SamplerState              samp         : register(s0);
SamplerComparisonState    shadowSampler : register(s1);

// params[0].x = layer UV tiling  (set via StaticMesh::Param(0).x)

// ----------------------------------------------------------------
// No-tile サンプリング
// テクスチャタイルより低周波のスムーズノイズで UV を連続的に歪ませる。
// 離散パッチを作らないため、タイル境界の色差が生じない。
// ----------------------------------------------------------------
float2 HashUV(float2 p)
{
    float2 q = float2(dot(p, float2(127.1, 311.7)),
                      dot(p, float2(269.5, 183.3)));
    return frac(sin(q) * 43758.5453) * 2.0 - 1.0;
}

// テクスチャタイルより低周波 (scale 倍) の連続スムーズノイズを返す
float2 SmoothNoise2(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(HashUV(i),               HashUV(i + float2(1, 0)), u.x),
                lerp(HashUV(i + float2(0, 1)), HashUV(i + float2(1, 1)), u.x), u.y);
}

float3 SampleNoTile(Texture2D tex, SamplerState s, float2 uv)
{
    // テクスチャタイルの 1/5 の周波数でゆっくり変化するノイズで UV を歪ませる
    float2 distort = SmoothNoise2(uv * 0.2) * 0.45;
    return tex.Sample(s, uv + distort).rgb;
}

float SampleShadow(float3 worldPos)
{
    float4 lightClip = mul(lightViewProj[0], float4(worldPos, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    float  d   = ndc.z - depthBias;
    return shadowMapArray.SampleCmpLevelZero(shadowSampler, float3(uv, 0.0), d);
}

PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput)0;
    float4 worldPos = mul(world, float4(input.position, 1.0));
    o.worldPos = worldPos.xyz;
    o.svPos    = mul(projection, mul(view, worldPos));
    o.N        = normalize(mul((float3x3)world, input.normal));
    o.uv       = input.uv;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float  tiling  = max(params[0].x, 0.001);  // 0 除算ガード
    float2 splatUV = input.uv;
    float2 layerUV = input.uv * tiling;

    float3 albedo;
    if (HasSplatMap())
    {
        // スプラットマップ (t0) の RGB でレイヤー比率を決める
        float3 splat = splatMap.Sample(samp, splatUV).rgb;
        float  total = splat.r + splat.g + splat.b;
        if (total < 0.001)
        {
            splat = float3(1.0, 0.0, 0.0);  // スプラットが黒 → layer0 のみ
        }
        else
        {
            splat /= total;
        }
        float3 c0 = SampleNoTile(layer0, samp, layerUV);
        float3 c1 = SampleNoTile(layer1, samp, layerUV);
        float3 c2 = SampleNoTile(layer2, samp, layerUV);
        albedo = c0 * splat.r + c1 * splat.g + c2 * splat.b;
    }
    else
    {
        // スプラットマップなし: layer0 (t1) のみ使用
        albedo = SampleNoTile(layer0, samp, layerUV);
    }

    float3 N   = normalize(input.N);
    float3 lit = ComputeLighting(input.worldPos, N, albedo, 1.0, float3(0.0, 0.0, 0.0));

    if (ReceivesShadow())
    {
        float  shadow      = SampleShadow(input.worldPos);
        float3 ambientOnly = ambient.color * ambient.intensity * albedo;
        lit = ambientOnly + (lit - ambientOnly) * shadow;
    }

    return float4(lit, 1.0);
}
