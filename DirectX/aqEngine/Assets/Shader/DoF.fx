// 被写界深度 (CS)。設計書/被写界深度設計.md。
// worldPos (GBuffer2) からビュー空間の z を作り、画素ごとの符号付き CoC
// (-1 = 手前に最大、0 = ピント内、+1 = 奥に最大) を求める。
// ぼかしは 1 パスの集め方式 (scatter-as-gather)。デバッグ表示なら、ぼかしの代わりに CoC を色で出す。
// DoFCBData (Rendering/PostProcess/Commands/DoFPassCommand.h) とレイアウトを一致させること。

cbuffer DoFCB : register(b0)
{
	float4x4 g_View;             // ワールド → ビュー (z はカメラから前方へ正。列ベクトル規約: mul(M, v))
	float2   g_ScreenSize;
	float    g_FocusDistance;    // ピント位置 (ビュー空間の z)
	float    g_FocusRange;       // ピント位置の前後でぼけない幅 (片側)
	float    g_NearTransition;   // 手前側で CoC が 0 → -1 になる距離
	float    g_FarTransition;    // 奥側で CoC が 0 → +1 になる距離
	float    g_MaxRadius;        // 最大のぼけ半径 (画素)
	uint     g_DebugView;        // 1 なら CoC を色で出す
	uint     g_Pad0;
	uint     g_Pad1;
	uint     g_Pad2;
	uint     g_Pad3;
};

Texture2D<float4>   g_Input    : register(t0);   // HDR (PostInput か Scene)
Texture2D<float4>   g_WorldPos : register(t1);   // GBuffer2 (worldPos)
RWTexture2D<float4> g_Out      : register(u0);

// 遷移幅が 0 のときの割り算を避ける (0 なら段差で切り替わる)。
static const float MIN_TRANSITION = 1e-4f;

// 周りのサンプル数 (黄金角の螺旋で円盤に置く)。
static const int   SAMPLE_COUNT = 48;
static const float GOLDEN_ANGLE = 2.39996323f;   // π (3 − √5)

// 奥側のサンプルを使ってよい深度の許容差 (中心の z に対する比)。
static const float DEPTH_TOLERANCE = 0.05f;


/** 画素の CoC と深度 */
struct CoCSample
{
	float coc;          // 符号付き CoC
	float z;            // ビュー空間の z (背景なら未使用)
	bool  background;   // worldPos が 0 (無限遠)
};


// 画素の符号付き CoC とビュー空間の z。worldPos が 0 の画素 (背景) は無限遠として扱う。
CoCSample ComputeCoC(int3 coord)
{
	CoCSample result;
	result.coc        = 0.0f;
	result.z          = 0.0f;
	result.background = false;

	const float3 wp = g_WorldPos.Load(coord).xyz;
	if (dot(wp, wp) < 1e-6f) {
		// 無限遠は常に focus + range より奥。奥側の最大にする。
		result.coc        = 1.0f;
		result.background = true;
		return result;
	}

	const float z     = mul(g_View, float4(wp, 1.0f)).z;
	const float nearZ = g_FocusDistance - g_FocusRange;
	const float farZ  = g_FocusDistance + g_FocusRange;
	result.z = z;
	if (z < nearZ) {
		result.coc = -saturate((nearZ - z) / max(g_NearTransition, MIN_TRANSITION));
	} else if (z > farZ) {
		result.coc = saturate((z - farZ) / max(g_FarTransition, MIN_TRANSITION));
	}
	return result;
}


// 奥側のサンプルが中心に被さってよいか。サンプルが中心より手前か同じくらいのときだけ可。
// 背景 (無限遠) どうしは常に可。中心が背景なら、有限の z のサンプルは常に手前なので可。
bool IsFarSampleAllowed(CoCSample center, CoCSample s)
{
	if (s.background) {
		return center.background;
	}
	if (center.background) {
		return true;
	}
	return s.z <= center.z + DEPTH_TOLERANCE * center.z;
}


[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const int2 screenSize = int2(g_ScreenSize);
	if (id.x >= (uint)screenSize.x || id.y >= (uint)screenSize.y) {
		return;
	}

	const int3   coord = int3(id.xy, 0);
	const float4 color = g_Input.Load(coord);

	// デバッグ表示: 手前 = 赤、奥 = 青、ピント内 = 黒。
	if (g_DebugView != 0) {
		const float coc = ComputeCoC(coord).coc;
		g_Out[id.xy] = (coc < 0.0f)
			? float4(-coc, 0.0f, 0.0f, color.a)
			: float4(0.0f, 0.0f, coc, color.a);
		return;
	}

	if (g_MaxRadius <= 0.0f) {
		g_Out[id.xy] = color;
		return;
	}

	const CoCSample center = ComputeCoC(coord);

	// 中心は必ず重み 1。総和は 1 以上なので割り算で 0 にならない。
	float3 sum       = color.rgb;
	float  weightSum = 1.0f;

	[loop]
	for (int i = 0; i < SAMPLE_COUNT; ++i) {
		// 点 i の半径は maxRadius × sqrt((i + 0.5) / N)。円盤に一様に並ぶ。
		const float dist  = g_MaxRadius * sqrt((i + 0.5f) / SAMPLE_COUNT);
		const float angle = i * GOLDEN_ANGLE;
		const float2 offset = float2(cos(angle), sin(angle)) * dist;

		// 画素中心に丸めて読む。画面外は使わない。
		const int2 pos = int2(id.xy) + int2(floor(offset + 0.5f));
		if (any(pos < 0) || any(pos >= screenSize)) {
			continue;
		}

		const int3      sampleCoord = int3(pos, 0);
		const CoCSample s           = ComputeCoC(sampleCoord);

		// ピント内のサンプルは自分の円が 0 なので広がらない。
		if (s.coc == 0.0f) {
			continue;
		}

		// 自分のぼけ半径が中心までの距離以上なら重み 1。
		// 奥のサンプルが中心より奥にあるときは、届く半径を中心自身のぼけ半径までに抑える
		// (ピント内の前景 = 半径 0 には奥のぼけが被さらない。中心もぼけていれば、その範囲で奥を取り込み、
		//  ぼけた物どうしの輪郭が両側に柔らかくなる。設計書/被写界深度設計.md §0.2 の P2 での変更)。
		// 手前のぼけ(CoC < 0)は深度に関係なく被さる。
		float radius = abs(s.coc) * g_MaxRadius;
		if (s.coc > 0.0f && !IsFarSampleAllowed(center, s)) {
			radius = min(radius, abs(center.coc) * g_MaxRadius);
		}

		// 境目の段差を和らげるため、半径が距離に 1 画素足りない範囲では線形に重みを落とす。
		const float weight = saturate(radius - dist + 1.0f);
		if (weight <= 0.0f) {
			continue;
		}

		sum       += g_Input.Load(sampleCoord).rgb * weight;
		weightSum += weight;
	}

	g_Out[id.xy] = float4(sum / weightSum, color.a);
}
