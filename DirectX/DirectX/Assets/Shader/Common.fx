/**
 * 頂点シェーダーの入力
 */
struct VSInput
{
	float3 position : POSITION;
	float4 color    : COLOR0;
};
/**
 * ピクセルシェーダーの入力
 */
struct PSInput
{
	float4 position 	: POSITION;
	float4 color		: COLOR0;
};