// 輪郭線 (CS)。
// G-Buffer の法線 (GBuffer1.xyz、ワールド空間) を隣り合う画素と比べ、
//   - 片方だけジオメトリが無い (背景) = シルエット
//   - 法線が大きく向きを変える          = 折り目 / 物体の境目
// を拾って線色を乗せる。
//
// **深度 (worldPos との距離) は使わない。** worldPos を持つ GBuffer2 は R16G16B16A16_Float で、
// half float の刻み幅は座標の大きさに比例する (|座標| 2360m のコースでは 2m)。
// 距離の差分を見ると、この刻みの境目が平坦な路面や地形に等間隔の線として出てしまう。
// 法線は長さ 1 の値なので刻み幅が一定 (約 0.0005) で、この問題が起きない。
// OutlineCBData (OutlinePass.h) とレイアウトを一致させること。

cbuffer OutlineCB : register(b0)
{
	float3 g_Color;       // 線の色
	float  g_Intensity;   // 線の濃さ (0 で無効、1 で線色そのもの)
	float  g_Threshold;   // 折り目とみなす 1 - dot(n0, n1) (0.25 ≒ 41 度)
	uint   g_Width;
	uint   g_Height;
	int    g_Thickness;   // 隣接画素までの距離 (px)
};

Texture2D<float4>   g_Scene  : register(t0);   // 直前の色 (トーンマップ後の LDR)
Texture2D<float4>   g_Normal : register(t1);   // GBuffer1 (N.xyz + roughness)
RWTexture2D<float4> g_Output : register(u0);

// G-Buffer は 0 クリアなので、法線が長さ 0 の画素は「ジオメトリ無し」(背景・フォワード描画)。
static const float kEmptyEpsilon = 1e-6f;

// 画素の法線を返す。ジオメトリが無ければ hasGeometry = false。
float3 LoadNormal(int2 coord, out bool hasGeometry)
{
	const int2   clamped = clamp(coord, int2(0, 0), int2(g_Width - 1, g_Height - 1));
	const float3 n       = g_Normal.Load(int3(clamped, 0)).xyz;

	hasGeometry = dot(n, n) > kEmptyEpsilon;
	return hasGeometry ? normalize(n) : float3(0.0f, 0.0f, 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Width || id.y >= g_Height) {
		return;
	}

	const float4 color = g_Scene.Load(int3(id.xy, 0));
	const int2   coord = int2(id.xy);

	bool         centerHasGeometry;
	const float3 centerNormal = LoadNormal(coord, centerHasGeometry);

	// 十字 4 近傍と比べる。
	float edge = 0.0f;
	const int2 offsets[4] = {
		int2( g_Thickness, 0), int2(-g_Thickness, 0),
		int2(0,  g_Thickness), int2(0, -g_Thickness),
	};

	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		bool         neighborHasGeometry;
		const float3 neighborNormal = LoadNormal(coord + offsets[i], neighborHasGeometry);

		if (centerHasGeometry != neighborHasGeometry) {
			// 片方だけ背景 = シルエット。
			edge = 1.0f;
			continue;
		}
		if (!centerHasGeometry) {
			// どちらも背景。
			continue;
		}

		const float diff = 1.0f - dot(centerNormal, neighborNormal);
		edge = max(edge, saturate((diff - g_Threshold) / max(g_Threshold, 0.0001f)));
	}

	g_Output[id.xy] = float4(lerp(color.rgb, g_Color, edge * g_Intensity), color.a);
}
