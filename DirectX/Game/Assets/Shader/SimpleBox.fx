
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
    o.tex = input.tex;
    return o;
}

/**
 * ピクセルシェーダーのエントリ関数
 */
float4 PSMain(PSInput input) : SV_Target0
{
	return float4(1.0f, 0.0f, 0.0f, 1.0f);
}