// #include "ModelCB.h"

cbuffer VSPSCb : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 project;
};

/**
 * 頂点シェーダーの入力
 */
struct VSInput
{
    float3 position : SV_Position;
    float3 normal : NORMAL0;
    float2 tex : TEXCOORD0;
};
/**
 * ピクセルシェーダーの入力
 */
struct PSInput
{
    float4 position : SV_Position;
    float2 tex : TEXCOORD0;
};

/**
 * 頂点シェーダーのエントリ関数
 */
PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput) 0;
    float4 position;
    position = float4(input.position, 1.0f);
    position = mul(world, position);
    position = mul(view, position);
    position = mul(project, position);
    o.position = position;
	// o.position = float4(input.position, 0.0f);
	//o.color = input.color;
    o.tex = input.tex;
    return o;
}

Texture2D colorTexture : register(t0); // カラーテクスチャー
SamplerState colorSamplerState : register(s0); // カラーテクスチャサンプラー
/**
 * ピクセルシェーダーのエントリ関数
 */
float4 PSMain(PSInput input) : SV_Target0
{
	//return input.color;
    return colorTexture.Sample(colorSamplerState, input.tex);
}

