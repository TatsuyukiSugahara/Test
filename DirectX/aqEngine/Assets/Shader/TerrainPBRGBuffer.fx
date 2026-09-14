#include "PBRMaterialCB.h"

// Terrain 用 PBR G-Buffer パス。
// テクスチャスロットは Terrain 専用レイアウト（t2 競合回避）:
//   t0 = splatMap, t1 = layer0(grass), t2 = layer1(rock), t3 = layer2(dirt)
// metallic / roughness / specular は CB 定数のみ使用（テクスチャなし）。
// テクスチャ由来のフラグ（PBR_HAS_NORMAL 等）はこのシェーダーでは無視する。

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
    float2 uv       : TEXCOORD2;
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

Texture2D    splatMap : register(t0);
Texture2D    layer0   : register(t1);  // grass
Texture2D    layer1   : register(t2);  // rock
Texture2D    layer2   : register(t3);  // dirt
SamplerState samp     : register(s0);

// no-tile サンプリング（TerrainGBufferLit.fx と同一）
float2 HashUV(float2 p)
{
    float2 q = float2(dot(p, float2(127.1, 311.7)),
                      dot(p, float2(269.5, 183.3)));
    return frac(sin(q) * 43758.5453) * 2.0 - 1.0;
}

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
    float2 distort = SmoothNoise2(uv * 0.2) * 0.45;
    return tex.Sample(s, uv + distort).rgb;
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

PSOutput PSMain(PSInput input)
{
    // params[0].x = layer UV tiling（TerrainGBufferLit.fx と同じ規則）
    float  tiling  = max(_extra[0].x, 0.001);
    float2 splatUV = input.uv;
    float2 layerUV = input.uv * tiling;

    float3 baseColor;
    if (PBR_HasSplatMap())
    {
        float3 splat = splatMap.Sample(samp, splatUV).rgb;
        float  total = splat.r + splat.g + splat.b;
        splat = (total < 0.001) ? float3(1.0, 0.0, 0.0) : splat / total;
        float3 c0 = SampleNoTile(layer0, samp, layerUV);
        float3 c1 = SampleNoTile(layer1, samp, layerUV);
        float3 c2 = SampleNoTile(layer2, samp, layerUV);
        baseColor = c0 * splat.r + c1 * splat.g + c2 * splat.b;
    }
    else
    {
        baseColor = SampleNoTile(layer0, samp, layerUV);
    }

    float3 N        = normalize(input.N);
    float  pixelTag = PBR_EncodePixelTag();

    PSOutput o;
    o.gbuffer0 = float4(baseColor, 0.0);        // metallic = 0（地形は常に誘電体）
    o.gbuffer1 = float4(N, roughness);           // roughness は CB 定数
    o.gbuffer2 = float4(input.worldPos, specular);
    o.gbuffer3 = float4(0.0, 0.0, 0.0, pixelTag); // 地形はエミッシブなし
    return o;
}
