#include "PBRFunctions.fx"
#include "ShadowSampling.fx"

// Disney Principled BRDF ディファードライティングパス。
// GBuffer レイアウト（PBRGBuffer.fx / SkeletalPBRGBuffer.fx / TerrainPBRGBuffer.fx と一致）:
//   GBuffer0: baseColor.rgb + metallic
//   GBuffer1: N.xyz + roughness
//   GBuffer2: worldPos.xyz + specular(F0 scale)
//   GBuffer3: emissive*scale + pixelTag
// フルスクリーントライアングル: Draw(3, 0) で呼ぶ（頂点バッファ不要）。

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

float4 PSMain(VSOutput input) : SV_TARGET
{
    int2 coord = int2(input.svPos.xy);

    float4 g0 = gbuffer0.Load(int3(coord, 0));
    float4 g1 = gbuffer1.Load(int3(coord, 0));
    float4 g2 = gbuffer2.Load(int3(coord, 0));
    float4 g3 = gbuffer3.Load(int3(coord, 0));

    // pixelTag が 0.5 未満なら空背景ピクセル → 描画しない
    float pixelTag = g3.a;
    clip(pixelTag - 0.5);

    float3 baseColor        = g0.rgb;
    float  metallic         = g0.a;
    float3 N                = normalize(g1.xyz);
    float  roughness        = g1.w;
    float3 worldPos         = g2.xyz;
    float  specular         = g2.w;
    float3 preScaledEmissive = g3.rgb;

    // pixelTag >= 1.5 なら影を受ける。
    // ライト0のみシャドウを適用し、ライト1以降はフィルライトとしてシャドウなしで寄与させる。
    float4 dirShadow = float4(1.0, 1.0, 1.0, 1.0);
    if (pixelTag >= 1.5 && directionalLightCount > 0)
        dirShadow.x = SampleShadowForLight(worldPos, 0);

    float3 lit = ComputePBRLighting(worldPos, N, baseColor, metallic,
                                    roughness, specular, preScaledEmissive,
                                    dirShadow);

    return float4(lit, 1.0);
}
