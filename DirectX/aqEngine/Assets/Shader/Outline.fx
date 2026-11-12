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
//
// DoFCoC (DoF が書く符号付きぼけ半径) があれば (g_UseCoC != 0)、線をぼけ半径の円盤で平均して広げる
// (設計書/被写界深度設計.md §0.5)。半径は中心と十字 4 近傍の |ぼけ半径| の最小値にする。
// 線は物体側と背景側の両方の画素に描かれるので、最小にすればピント内の物に接する線はくっきり残り、
// ぼけた物どうしの線だけがぼける。円盤の各点は、その点の線のぼけ半径が中心まで届くときだけ取り込む
// (届かない点は 0 として数える)。これが無いと、ピント内の線がぼけた背景の画素に拾われて輪のように滲む。
// OutlineCBData (OutlinePass.h) とレイアウトを一致させること。

cbuffer OutlineCB : register(b0)
{
	float3 g_Color;       // 線の色
	float  g_Intensity;   // 線の濃さ (0 で無効、1 で線色そのもの)
	float  g_Threshold;   // 折り目とみなす 1 - dot(n0, n1) (0.25 ≒ 41 度)
	uint   g_Width;
	uint   g_Height;
	int    g_Thickness;   // 隣接画素までの距離 (px)
	uint   g_UseCoC;      // 1 なら g_CoC で線をぼかす
	uint   g_Pad0;
	uint   g_Pad1;
	uint   g_Pad2;
};

Texture2D<float4>   g_Scene  : register(t0);   // 直前の色 (トーンマップ後の LDR)
Texture2D<float4>   g_Normal : register(t1);   // GBuffer1 (N.xyz + roughness)
Texture2D<float>    g_CoC    : register(t2);   // DoFCoC (符号付きぼけ半径、画素)。g_UseCoC == 0 なら読まない
RWTexture2D<float4> g_Output : register(u0);

// G-Buffer は 0 クリアなので、法線が長さ 0 の画素は「ジオメトリ無し」(背景・フォワード描画)。
static const float kEmptyEpsilon = 1e-6f;

// ぼかしのサンプル数 (黄金角の螺旋で円盤に置く。中心は別に 1 点)。
static const int   BLUR_SAMPLE_COUNT = 48;
static const float GOLDEN_ANGLE      = 2.39996323f;   // π (3 − √5)

// 半径がこれ未満ならぼかさない (中心だけ)。
static const float MIN_BLUR_RADIUS = 0.5f;

// 画素の法線を返す。ジオメトリが無ければ hasGeometry = false。
float3 LoadNormal(int2 coord, out bool hasGeometry)
{
	const int2   clamped = clamp(coord, int2(0, 0), int2(g_Width - 1, g_Height - 1));
	const float3 n       = g_Normal.Load(int3(clamped, 0)).xyz;

	hasGeometry = dot(n, n) > kEmptyEpsilon;
	return hasGeometry ? normalize(n) : float3(0.0f, 0.0f, 0.0f);
}

// 画素の |ぼけ半径| (画素) を返す。座標は LoadNormal と同じく画面内に clamp する。
float LoadCoCRadius(int2 coord)
{
	const int2 clamped = clamp(coord, int2(0, 0), int2(g_Width - 1, g_Height - 1));
	return abs(g_CoC.Load(int3(clamped, 0)));
}

// 画素の線のぼけ半径 (画素)。中心と、ComputeEdge が比べる十字 4 近傍の |ぼけ半径| の最小値。
// 線は物体側と背景側の両方の画素に描かれるので、最小にすればピント内の物に接する線はくっきり残る。
float LineRadius(int2 coord)
{
	float radius = LoadCoCRadius(coord);
	radius = min(radius, LoadCoCRadius(coord + int2( g_Thickness, 0)));
	radius = min(radius, LoadCoCRadius(coord + int2(-g_Thickness, 0)));
	radius = min(radius, LoadCoCRadius(coord + int2(0,  g_Thickness)));
	radius = min(radius, LoadCoCRadius(coord + int2(0, -g_Thickness)));
	return radius;
}

// 画素の線の濃さ (0〜1)。十字 4 近傍と法線を比べる。
float ComputeEdge(int2 coord)
{
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
	return edge;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= g_Width || id.y >= g_Height) {
		return;
	}

	const float4 color = g_Scene.Load(int3(id.xy, 0));
	const int2   coord = int2(id.xy);

	float edge = 0.0f;
	if (g_UseCoC == 0) {
		edge = ComputeEdge(coord);
	} else {
		// 線のぼけ半径は中心と、ComputeEdge が比べる十字 4 近傍の |ぼけ半径| の最小値 (LineRadius)。
		const float radius = LineRadius(coord);

		if (radius < MIN_BLUR_RADIUS) {
			edge = ComputeEdge(coord);
		} else {
			// 中心 1 点 + 半径 radius の円盤に置いた点の線の濃さを平均する。
			const int2 screenSize = int2(g_Width, g_Height);
			float sum   = ComputeEdge(coord);
			float count = 1.0f;

			[loop]
			for (int i = 0; i < BLUR_SAMPLE_COUNT; ++i) {
				// 点 i の半径は radius × sqrt((i + 0.5) / N)。円盤に一様に並ぶ (DoF.fx と同じ置き方)。
				const float  dist   = radius * sqrt((i + 0.5f) / BLUR_SAMPLE_COUNT);
				const float  angle  = i * GOLDEN_ANGLE;
				const float2 offset = float2(cos(angle), sin(angle)) * dist;

				// 画素中心に丸めて読む。画面外は数えない。
				const int2 pos = coord + int2(floor(offset + 0.5f));
				if (any(pos < 0) || any(pos >= screenSize)) {
					continue;
				}
				count += 1.0f;

				// その点の線が中心まで広がるときだけ取り込む (DoF.fx の集め方式と同じ考え方)。
				// ピント内の物の線 (線のぼけ半径 0) は周りのぼけた背景へ滲み出さない。
				// 距離は丸めた後の画素で測る (丸め前の dist だと境目で採否がずれる)。
				if (LineRadius(pos) < length(float2(pos - coord))) {
					continue;
				}
				sum += ComputeEdge(pos);
			}
			edge = sum / count;
		}
	}

	g_Output[id.xy] = float4(lerp(color.rgb, g_Color, edge * g_Intensity), color.a);
}
