// 輪郭線 (CS)。
// GBuffer2 の worldPos からカメラまでの距離を作り、隣り合う画素との相対差が大きい場所を
// エッジとみなして線色を乗せる。深度バッファを直接読めない (Depth キーの実体は GBuffer0 の
// albedo SRV) ため、Hi-Z の深度再構成と同じく worldPos を使う。
// OutlineCBData (OutlinePass.h) とレイアウトを一致させること。

cbuffer OutlineCB : register(b0)
{
	float3 g_CameraPos;   // ワールド空間のカメラ位置
	float  g_Threshold;   // エッジとみなす相対深度差 (0.01 = 1%)
	float3 g_Color;       // 線の色
	float  g_Intensity;   // 線の濃さ (0 で無効、1 で線色そのもの)
	uint   g_Width;
	uint   g_Height;
	int    g_Thickness;   // 隣接画素までの距離 (px)
	int    g_Padding;
};

Texture2D<float4>   g_Scene    : register(t0);   // 直前の色 (トーンマップ後の LDR)
Texture2D<float4>   g_WorldPos : register(t1);   // GBuffer2 (worldPos)
RWTexture2D<float4> g_Output   : register(u0);

// worldPos が未書き込み (背景・フォワード描画) の画素は「ジオメトリ無し」とみなす。
static const float kEmptyEpsilon = 1e-6f;

// カメラからの距離を返す。ジオメトリが無ければ負値を返す。
float LoadViewDepth(int2 coord)
{
	const int2 clamped = clamp(coord, int2(0, 0), int2(g_Width - 1, g_Height - 1));
	const float3 wp = g_WorldPos.Load(int3(clamped, 0)).xyz;
	if (dot(wp, wp) < kEmptyEpsilon) {
		return -1.0f;
	}
	return length(wp - g_CameraPos);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Width || id.y >= g_Height) {
		return;
	}

	const float4 color  = g_Scene.Load(int3(id.xy, 0));
	const int2   coord  = int2(id.xy);
	const float  center = LoadViewDepth(coord);

	// 十字 4 近傍との差を見る。ジオメトリの有無が切り替わる箇所 (シルエット) と、
	// 距離が相対的に大きく飛ぶ箇所 (折り目・物体の重なり) をエッジとして拾う。
	float edge = 0.0f;
	const int2 offsets[4] = {
		int2( g_Thickness, 0), int2(-g_Thickness, 0),
		int2(0,  g_Thickness), int2(0, -g_Thickness),
	};

	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		const float neighbor = LoadViewDepth(coord + offsets[i]);

		if ((center < 0.0f) != (neighbor < 0.0f)) {
			// 片方だけ背景 = シルエット。
			edge = 1.0f;
			continue;
		}
		if (center < 0.0f) {
			// どちらも背景。
			continue;
		}

		// 遠くのものほど許容差を大きくし、距離によらず同じ見え方にする。
		const float diff = abs(center - neighbor) / max(center, 0.0001f);
		edge = max(edge, saturate((diff - g_Threshold) / max(g_Threshold, 0.0001f)));
	}

	g_Output[id.xy] = float4(lerp(color.rgb, g_Color, edge * g_Intensity), color.a);
}
