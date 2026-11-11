// 被写界深度 (CS)。設計書/被写界深度設計.md。
// worldPos (GBuffer2) からビュー空間の z を作り、画素ごとの符号付き CoC
// (-1 = 手前に最大、0 = ピント内、+1 = 奥に最大) を求める。
// P1: ぼかしは持たない。デバッグ表示なら CoC を色で出し、そうでなければ入力をそのまま書く。
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
	uint     g_BlurEnabled;      // 0 ならぼかさない (P1 は常に 0)
	uint     g_Pad0;
	uint     g_Pad1;
	uint     g_Pad2;
};

Texture2D<float4>   g_Input    : register(t0);   // HDR (PostInput か Scene)
Texture2D<float4>   g_WorldPos : register(t1);   // GBuffer2 (worldPos)
RWTexture2D<float4> g_Out      : register(u0);

// 遷移幅が 0 のときの割り算を避ける (0 なら段差で切り替わる)。
static const float MIN_TRANSITION = 1e-4f;


// 画素の符号付き CoC。worldPos が 0 の画素 (背景) は無限遠として扱う。
float ComputeCoC(int3 coord)
{
	const float3 wp = g_WorldPos.Load(coord).xyz;
	if (dot(wp, wp) < 1e-6f) {
		// 無限遠は常に focus + range より奥。奥側の最大にする。
		return 1.0f;
	}

	const float z     = mul(g_View, float4(wp, 1.0f)).z;
	const float nearZ = g_FocusDistance - g_FocusRange;
	const float farZ  = g_FocusDistance + g_FocusRange;
	if (z < nearZ) {
		return -saturate((nearZ - z) / max(g_NearTransition, MIN_TRANSITION));
	}
	if (z > farZ) {
		return saturate((z - farZ) / max(g_FarTransition, MIN_TRANSITION));
	}
	return 0.0f;
}


[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= (uint)g_ScreenSize.x || id.y >= (uint)g_ScreenSize.y) {
		return;
	}

	const int3   coord = int3(id.xy, 0);
	const float4 color = g_Input.Load(coord);

	// デバッグ表示: 手前 = 赤、奥 = 青、ピント内 = 黒。
	if (g_DebugView != 0) {
		const float coc = ComputeCoC(coord);
		g_Out[id.xy] = (coc < 0.0f)
			? float4(-coc, 0.0f, 0.0f, color.a)
			: float4(0.0f, 0.0f, coc, color.a);
		return;
	}

	// ぼかしなし (P1)。入力をそのまま書く。
	if (g_BlurEnabled == 0 || g_MaxRadius <= 0.0f) {
		g_Out[id.xy] = color;
		return;
	}

	// ぼかしは P2 で入れる。それまでは入力をそのまま書く。
	g_Out[id.xy] = color;
}
