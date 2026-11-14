// SSAO のぼかし（CS。設計書/SSAO設計.md §0.4）。
//
// 中心画素 c の周り 5×5（c を含む 25 画素。ビュー矩形の中だけ）で、生の AO を重み付き平均する。
//   wN = saturate(dot(N_c, N_q)) ^ 8                                   法線が大きく違う面をまたがない
//   wP = saturate(1 − |dot(N_c, P_q − P_c)| / (planeTolerance × z_c + 4 × e_c))   中心の面の平面から離れた画素を使わない
//   結果 = Σ(ao_q × wN × wP) / Σ(wN × wP)（中心は wN = wP = 1 なので分母は 1 以上）
//
// 背景の判定: pixelTag は読まない（t0〜t2 に収めるため）。worldPos が全成分 0 の画素を背景とみなす。
//   中心が背景なら、生の AO を読む前に 1 を書いて終わる。周りの背景の画素は使わない。
//   （原点ちょうどの G-Buffer の画素と区別できない点は設計書 §5 課題 4）
//
// z はビュー空間の深度（SSAO.fx と同じ求め方）。e は half 精度の誤差幅（SSR設計 §0.5 と同じ式）。
// スレッド ID はビュー内の座標。RT 上の座標 = ビュー矩形の原点 + ビュー内の座標。
//
// スロット: b0 = SSAOCB（SSAO.fx と同じ CB）、t0 = 生の AO、t1 = GBuffer1、t2 = GBuffer2、u0 = ぼかした AO
// SSAOComputeCommand.h の SSAOCBData と並びを一致させること（C++ 側は 192B。SSAO.fx と共用）。

cbuffer SSAOCB : register(b0)
{
    float4x4 g_View;             // ワールド → ビュー（z はカメラから前方へ正）
    float4   g_ViewRect;         // ビュー矩形 (x, y, w, h)（ピクセル）
    float    g_ProjScaleY;       // 以下 7 つは SSAO.fx が使う（並びを揃えるために置く）
    float    g_ProjW34;
    float    g_ProjW44;
    float    g_Radius;
    float    g_Intensity;
    float    g_Power;
    float    g_BiasScale;
    uint     g_SampleCount;
    float    g_MaxScreenRadius;
    float    g_PlaneTolerance;   // 平面距離の許容（ビュー空間の深度に掛ける）
    float2   g_Pad;
};

Texture2D<float>   g_RawAO    : register(t0);   // 生の AO（SSAO.fx の出力）
Texture2D<float4>  g_GBuffer1 : register(t1);   // N.xyz + roughness
Texture2D<float4>  g_GBuffer2 : register(t2);   // worldPos.xyz + specular
RWTexture2D<float> g_Out      : register(u0);   // ぼかした AO

static const int   BLUR_RADIUS   = 2;     // 5×5
static const float NORMAL_POWER  = 8.0;   // wN のべき
static const float MIN_TOLERANCE = 1e-6;  // 許容幅が 0 のときの割り算を避ける

// worldPos（half）の誤差幅（SSR設計 §0.5）
float ErrorWidth(float3 p)
{
    float3 a = abs(p);
    return exp2(-10.0) * max(a.x, max(a.y, a.z)) * 2.0;
}

// worldPos が全成分 0 なら背景
bool IsBackground(float3 worldPos)
{
    return all(worldPos == 0.0);
}

// ビュー矩形の最小・最大の画素（両端を含む。RT の寸法でも切る）
void ViewRectBounds(out int2 lo, out int2 hi)
{
    uint outW, outH;
    g_Out.GetDimensions(outW, outH);
    lo = int2(round(g_ViewRect.xy));
    hi = min(int2(round(g_ViewRect.xy + g_ViewRect.zw)), int2(outW, outH)) - 1;
}

float Blur(int2 pix, int2 lo, int2 hi)
{
    // 中心が背景なら、生の AO を読む前に 1 を書く
    float3 Pc = g_GBuffer2.Load(int3(pix, 0)).xyz;
    if (IsBackground(Pc)) {
        return 1.0;
    }

    float3 Nc        = normalize(g_GBuffer1.Load(int3(pix, 0)).xyz);
    float  zc        = mul(g_View, float4(Pc, 1.0)).z;
    float  tolerance = max(g_PlaneTolerance * zc + 4.0 * ErrorWidth(Pc), MIN_TOLERANCE);

    float sum       = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (int dy = -BLUR_RADIUS; dy <= BLUR_RADIUS; ++dy)
    {
        [unroll]
        for (int dx = -BLUR_RADIUS; dx <= BLUR_RADIUS; ++dx)
        {
            int2 q = pix + int2(dx, dy);
            if (any(q < lo) || any(q > hi)) {
                continue;   // ビュー矩形の外は使わない
            }

            float3 Pq = g_GBuffer2.Load(int3(q, 0)).xyz;
            if (IsBackground(Pq)) {
                continue;   // 背景の画素は使わない
            }

            float3 Nq     = normalize(g_GBuffer1.Load(int3(q, 0)).xyz);
            float  wN     = pow(saturate(dot(Nc, Nq)), NORMAL_POWER);
            float  wP     = saturate(1.0 - abs(dot(Nc, Pq - Pc)) / tolerance);
            float  weight = wN * wP;

            sum       += g_RawAO.Load(int3(q, 0)) * weight;
            weightSum += weight;
        }
    }

    // 中心は wN = wP = 1 なので通常は 1 以上。念のため 0 割りを避ける
    return (weightSum > 0.0) ? sum / weightSum : g_RawAO.Load(int3(pix, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // ビュー矩形の範囲だけを回す（dispatch は矩形の寸法。原点はここで足す）
    if (id.x >= (uint)g_ViewRect.z || id.y >= (uint)g_ViewRect.w) {
        return;
    }

    int2 lo, hi;
    ViewRectBounds(lo, hi);

    int2 pix = lo + int2(id.xy);
    if (any(pix > hi)) {
        return;
    }

    g_Out[uint2(pix)] = Blur(pix, lo, hi);
}
