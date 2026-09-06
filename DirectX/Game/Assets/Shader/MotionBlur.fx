// カメラモーションブラー (CS)。
// GBuffer2 の worldPos を前フレームの viewProj で再投影し、現在ピクセルとの差分
// (スクリーン空間速度) の方向にシーンをぼかす。カメラ移動由来のブラーのみを表現する
// (オブジェクト毎の速度バッファは持たない)。
// MotionBlurCBData (BloomPassCommand.h) とレイアウトを一致させること。

cbuffer MotionBlurCB : register(b0)
{
	float4x4 g_PrevViewProj;   // 前フレームの view * projection (列ベクトル規約: mul(M, v))
	float2   g_ScreenSize;
	float    g_Strength;       // ブラー長スケール (0 で無効。呼び出し側が 0 のときはパス自体を積まない)
	float    g_Padding;
};

Texture2D<float4>   g_Scene    : register(t0);   // HDR シーン
Texture2D<float4>   g_WorldPos : register(t1);   // GBuffer2 (worldPos)
RWTexture2D<float4> g_Out      : register(u0);

static const int SAMPLE_COUNT = 8;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)g_ScreenSize.x || id.y >= (uint)g_ScreenSize.y) {
		return;
	}

	const int3   coord = int3(id.xy, 0);
	const float4 color = g_Scene.Load(coord);

	// worldPos 未書き込み (背景/フォワード描画) はブラーしない。
	const float3 wp = g_WorldPos.Load(coord).xyz;
	if (dot(wp, wp) < 1e-6f) {
		g_Out[id.xy] = color;
		return;
	}

	// 前フレームのスクリーン位置を再構成する。
	const float4 prevClip = mul(g_PrevViewProj, float4(wp, 1.0f));
	if (prevClip.w <= 0.0001f) {
		g_Out[id.xy] = color;
		return;
	}
	const float2 prevNdc = prevClip.xy / prevClip.w;
	const float2 prevPix = float2((prevNdc.x * 0.5f + 0.5f) * g_ScreenSize.x,
	                              (0.5f - prevNdc.y * 0.5f) * g_ScreenSize.y);

	float2 velocity = (float2(id.xy) - prevPix) * g_Strength;

	// 過剰なブラー長を制限する (リスポーン等の瞬間移動でのスミア防止)。
	const float len    = length(velocity);
	const float maxLen = g_ScreenSize.y * 0.05f;
	if (len > maxLen) {
		velocity *= maxLen / len;
	}

	// 速度方向 (過去側) にサンプルを積分する。
	float4 acc = color;
	for (int i = 1; i < SAMPLE_COUNT; ++i)
	{
		const float t  = (float)i / (float)(SAMPLE_COUNT - 1);
		int2 samplePix = int2(round(float2(id.xy) - velocity * t));
		samplePix = clamp(samplePix, int2(0, 0), int2(g_ScreenSize) - int2(1, 1));
		acc += g_Scene.Load(int3(samplePix, 0));
	}
	g_Out[id.xy] = acc / SAMPLE_COUNT;
}
