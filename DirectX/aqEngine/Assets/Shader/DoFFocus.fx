// 被写界深度のオートフォーカス (CS)。設計書/被写界深度設計.md §0.2・§0.4 の 5。
// 画面中央 5×5 画素の worldPos (GBuffer2) からビュー空間の z の最小値 (最も手前) を目標にし、
// 1×1 のフォーカス RT (R32_Float) を lerp(前, 目標, speed) で更新する。DoF.fx がこの RT を t2 で読む。
// 1 スレッド ([numthreads(1,1,1)] を 1 グループだけ dispatch) が 25 画素を読み、1 回だけ書く。
// reset (RT を作った直後・リサイズ直後・オートフォーカスを有効にした直後) なら、目標があれば目標を、
// 無ければ手動のピント位置を書く。reset でなく目標も無ければ書かない (前の値を保つ)。これで RT は常に有効な値を持つ。
// DoFFocusCBData (Rendering/PostProcess/Commands/DoFFocusCommand.h) とレイアウトを一致させること。

cbuffer DoFFocusCB : register(b0)
{
	float4x4 g_View;            // ワールド → ビュー (z はカメラから前方へ正。列ベクトル規約: mul(M, v))
	float2   g_ScreenSize;
	float    g_FocusDistance;   // 手動のピント位置 (ビュー空間の z)。reset で目標が無いときに書く
	float    g_Speed;           // 目標へ寄せる割合 (0〜1。1 なら即座に目標)
	uint     g_Reset;           // 1 なら前の値を使わずに書き直す
	uint     g_Pad0;
	uint     g_Pad1;
	uint     g_Pad2;
};

Texture2D<float4>  g_WorldPos : register(t0);   // GBuffer2 (worldPos)
RWTexture2D<float> g_Focus    : register(u0);   // フォーカス RT (1×1。ピント位置 = ビュー空間の z)

// 中央から片側に読む画素数 (5×5 = 25 画素)。
static const int SAMPLE_EXTENT = 2;


[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	const int2 screenSize = int2(g_ScreenSize);
	const int2 center     = screenSize / 2;
	const int2 maxCoord   = max(screenSize - 1, int2(0, 0));

	// 中央 5×5 画素のうち、worldPos が 0 でない (背景でない) 画素の z の最小値を目標にする。
	// 背景の判定は DoF.fx と同じ。
	bool  hasTarget = false;
	float target    = 0.0f;

	[unroll]
	for (int dy = -SAMPLE_EXTENT; dy <= SAMPLE_EXTENT; ++dy) {
		[unroll]
		for (int dx = -SAMPLE_EXTENT; dx <= SAMPLE_EXTENT; ++dx) {
			// 画面が 5 画素より小さいときも画面外を読まないよう clamp する。
			const int2   pos = clamp(center + int2(dx, dy), int2(0, 0), maxCoord);
			const float3 wp  = g_WorldPos.Load(int3(pos, 0)).xyz;
			if (dot(wp, wp) < 1e-6f) {
				continue;
			}

			const float z = mul(g_View, float4(wp, 1.0f)).z;
			target    = hasTarget ? min(target, z) : z;
			hasTarget = true;
		}
	}

	if (g_Reset != 0) {
		g_Focus[int2(0, 0)] = hasTarget ? target : g_FocusDistance;
		return;
	}

	// 目標が無ければ (中央が全部背景なら) 前の値を保つ。
	if (hasTarget) {
		g_Focus[int2(0, 0)] = lerp(g_Focus[int2(0, 0)], target, g_Speed);
	}
}
