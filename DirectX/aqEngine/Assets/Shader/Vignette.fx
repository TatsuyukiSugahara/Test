// 画面の縁を暗くするビネット (CS)。
// 画面中心からの正規化距離 d (0 = 中心, 1 = 四隅) に smoothstep(radius, 1.0, d) を効かせ、
// 縁へ向かうほど色を減衰させる。Sample の「パスを 1 つ足す」歩 (VignettePass) から呼ばれる。
// VignetteCBData (VignettePass.h) とレイアウトを一致させること。

cbuffer VignetteCB : register(b0)
{
	float g_Strength;   // 縁の暗さ (0 で無効、1 で縁が黒)
	float g_Radius;     // 減衰が始まる距離 (0..1)
	uint  g_Width;
	uint  g_Height;
};

Texture2D<float4>   g_Input  : register(t0);
RWTexture2D<float4> g_Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Width || id.y >= g_Height) {
		return;
	}

	const float4 color = g_Input.Load(int3(id.xy, 0));

	// 画面中心からの距離を対角の半分で正規化する (0 = 中心, 1 = 四隅)。
	const float2 center  = float2(g_Width, g_Height) * 0.5f;
	const float  maxDist = length(center);
	const float  dist    = (maxDist > 0.0001f) ? length(float2(id.xy) - center) / maxDist : 0.0f;

	const float falloff = smoothstep(g_Radius, 1.0f, dist);
	const float factor  = 1.0f - g_Strength * falloff;

	g_Output[id.xy] = float4(color.rgb * factor, color.a);
}
