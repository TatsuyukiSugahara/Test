// SSAO の AO の計算（CS。設計書/SSAO設計.md §0.3）。
//
// worldPos（GBuffer2）と法線（GBuffer1）を直接使う半球の積分（Alchemy AO。McGuire et al. 2011）。
// 出力 RT（R32_Float）に ao（遮蔽されない割合。1 = 遮蔽なし）を書く。背景（pixelTag < 0.5）は 1。
//
//   1. 画面上の半径: rPx = radius × projScaleY × (viewH / 2) / w、w = projW34 × z + projW44
//      （z はビュー空間の深度。透視投影なら w = z、正射影なら w = 1）。
//      rPx は maxScreenRadius で頭打ち。rPx < 1 なら ao = 1。
//   2. サンプル点: sampleCount 個を黄金角の螺旋（i 番目の半径 rPx × sqrt((i + 0.5) / sampleCount)）に置き、
//      画素ごとの回転角 2π × IGN で全体を回す。IGN に渡すのはビュー内の画素座標。サンプル画素はビュー矩形の中に丸める。
//   3. 各サンプルの寄与: 背景なら 0。それ以外は v = Q − P、
//      term = max(0, dot(v, N) − bias) / (dot(v, v) + ε) × falloff
//        bias    = max(biasScale × z, 4 × e)（e は P の half 精度の誤差幅。SSR設計 §0.5 と同じ式）
//        falloff = saturate(1 − dot(v, v) / radius²)
//        ε       = 0.01 × radius²
//   4. ao = saturate(1 − (2 × intensity / sampleCount) × Σ term) ^ power
//
// 行列の規約は MotionBlur.fx / SSRTrace.fx と同じ（C++ 側の値をそのまま受け取り mul(M, v)）。
// スレッド ID はビュー内の座標。RT 上の座標 = ビュー矩形の原点 + ビュー内の座標（SSRTrace.fx と同じ）。
//
// スロット: b0 = SSAOCB、t0 = GBuffer1、t1 = GBuffer2、t2 = GBuffer3、u0 = 生の AO
// （D3D12 の compute は t0〜t2 しか届かないので 3 枚に収める。設計書 §0.1）
// SSAOComputeCommand.h の SSAOCBData と並びを一致させること（C++ 側は 192B。SSAOBlur.fx と共用）。

cbuffer SSAOCB : register(b0)
{
    float4x4 g_View;             // ワールド → ビュー（z はカメラから前方へ正）
    float4   g_ViewRect;         // ビュー矩形 (x, y, w, h)（ピクセル）
    float    g_ProjScaleY;       // 射影行列の y の拡大率（_22）
    float    g_ProjW34;          // 射影行列の _34（クリップ空間の w = _34 × z + _44）
    float    g_ProjW44;          // 射影行列の _44
    float    g_Radius;           // サンプルの半径（ワールド単位）
    float    g_Intensity;        // 強度
    float    g_Power;            // 結果のべき乗
    float    g_BiasScale;        // 自己遮蔽を防ぐ bias の係数（ビュー空間の深度に掛ける）
    uint     g_SampleCount;      // 1 画素あたりのサンプル数
    float    g_MaxScreenRadius;  // 画面上の半径の上限（画素）
    float    g_PlaneTolerance;   // ぼかしの平面距離の許容（SSAOBlur.fx が使う）
    float2   g_Pad;
};

Texture2D<float4>  g_GBuffer1 : register(t0);   // N.xyz + roughness
Texture2D<float4>  g_GBuffer2 : register(t1);   // worldPos.xyz + specular
Texture2D<float4>  g_GBuffer3 : register(t2);   // emissive*scale + pixelTag
RWTexture2D<float> g_Out      : register(u0);   // 生の AO

static const float TWO_PI       = 6.28318531;
static const float GOLDEN_ANGLE = 2.39996323;   // π (3 − √5)

// worldPos（half）の誤差幅（SSR設計 §0.5）
float ErrorWidth(float3 p)
{
    float3 a = abs(p);
    return exp2(-10.0) * max(a.x, max(a.y, a.z)) * 2.0;
}

// interleaved gradient noise（Jimenez 2014）。p はビュー内の画素座標
float InterleavedGradientNoise(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

// ビュー矩形の最小・最大の画素（両端を含む。RT の寸法でも切る）
void ViewRectBounds(out int2 lo, out int2 hi)
{
    uint outW, outH;
    g_Out.GetDimensions(outW, outH);
    lo = int2(round(g_ViewRect.xy));
    hi = min(int2(round(g_ViewRect.xy + g_ViewRect.zw)), int2(outW, outH)) - 1;
}

float ComputeAO(int2 pix, int2 localPix, int2 lo, int2 hi)
{
    // 背景（空）は遮蔽なし
    if (g_GBuffer3.Load(int3(pix, 0)).a < 0.5) {
        return 1.0;
    }

    float3 N = normalize(g_GBuffer1.Load(int3(pix, 0)).xyz);
    float3 P = g_GBuffer2.Load(int3(pix, 0)).xyz;

    // ビュー空間の深度（SSRTrace.fx の ViewZ と同じ求め方）とクリップ空間の w
    float z = mul(g_View, float4(P, 1.0)).z;
    float w = g_ProjW34 * z + g_ProjW44;
    if (w <= 0.0) {
        return 1.0;
    }

    // 1. 画面上の半径（頭打ちしてから 1 画素未満を判定する）
    float rPx = g_Radius * g_ProjScaleY * (g_ViewRect.w * 0.5) / w;
    rPx = min(rPx, g_MaxScreenRadius);
    if (rPx < 1.0 || g_SampleCount == 0) {
        return 1.0;
    }

    float radius2   = g_Radius * g_Radius;
    float epsilon   = 0.01 * radius2;
    float bias      = max(g_BiasScale * z, 4.0 * ErrorWidth(P));
    float rotation  = TWO_PI * InterleavedGradientNoise(float2(localPix));
    float invCount  = 1.0 / (float)g_SampleCount;
    float2 center   = float2(pix) + 0.5;

    // 2〜3. 黄金角の螺旋に置いたサンプルの寄与を足す
    float sum = 0.0;
    [loop]
    for (uint i = 0; i < g_SampleCount; ++i)
    {
        float  dist   = rPx * sqrt(((float)i + 0.5) * invCount);
        float  angle  = (float)i * GOLDEN_ANGLE + rotation;
        float2 offset = float2(cos(angle), sin(angle)) * dist;
        int2   q      = clamp(int2(floor(center + offset)), lo, hi);

        // 背景のサンプルは寄与 0
        if (g_GBuffer3.Load(int3(q, 0)).a < 0.5) {
            continue;
        }

        float3 v       = g_GBuffer2.Load(int3(q, 0)).xyz - P;
        float  vv      = dot(v, v);
        float  falloff = saturate(1.0 - vv / radius2);
        sum += max(0.0, dot(v, N) - bias) / (vv + epsilon) * falloff;
    }

    // 4. 結果
    float ao = saturate(1.0 - (2.0 * g_Intensity * invCount) * sum);
    return pow(ao, g_Power);
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

    int2 localPix = int2(id.xy);
    int2 pix      = lo + localPix;
    if (any(pix > hi)) {
        return;
    }

    g_Out[uint2(pix)] = ComputeAO(pix, localPix, lo, hi);
}
