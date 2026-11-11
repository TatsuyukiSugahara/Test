// SSR のトレース（CS。設計書/SSR設計.md §0.3〜§0.5）。
//
// G-Buffer の各画素から反射レイを出し、画面空間の線形マーチ + 二分探索で当たりを探す。
// 出力 RT に rgb = 当たった画素の Scene の色、a = 確信度（0〜1）を書く。外れは (0,0,0,0)。
// G-Buffer の無い画素（空）・roughness がしきい値（既定 0.5）以上の画素も (0,0,0,0)。
//
// 当たり判定（§0.4）:
//   1. 出発点は P + N × bias（bias = 誤差幅 e の 2 倍）。自分自身に当たらないため。
//   2. 終点は P + R × 最大距離。ビュー空間で near 面の手前に出るなら near 面で切る。
//      両端を画面へ投影し、最大 g_MaxSteps ステップで等間隔に進む（1 ステップは最低 1 画素）。
//      レイの深度は 1/z を画面上で線形補間して求める（透視補正）。
//   3. 各ステップ: ビュー矩形の外 → 外れで打ち切り / 背景 → 進む / Δ = zRay − zScene が
//      Δ < 0 → 進む、0 ≤ Δ ≤ 厚み → 当たり候補、Δ > 厚み（物の裏を通過）→ 進む。
//   4. 直前のステップが「手前」（Δ < 0 か背景）だったときだけ、直前〜候補を二分探索で詰める。
//   5. 最終位置で 0 ≤ Δ ≤ 厚み を再判定し、当たった面の法線が R と同じ向き（裏面）なら外れ。
//
// 誤差幅と厚み（§0.5。worldPos は絶対座標の half 精度）:
//   e    = 2^-10 × max(|x|, |y|, |z|) × 2
//   厚み = max(厚み係数 × zScene, 4 × e_hit)
//
// 行列の規約は MotionBlur.fx と同じ（C++ 側の値をそのまま受け取り mul(M, v)。§0.6-3）。
// view は剛体の前提で、左上 3×3 の転置を逆回転として使う。
// 画面座標への変換も MotionBlur.fx と同じ（y は下向き）。ただしビュー矩形の原点と寸法を使う。
//
// スロット: b0 = SSRCB、t0 = Scene、t1 = GBuffer1、t2 = GBuffer2、t3 = GBuffer3、u0 = 出力
// SSRTraceCommand.h の SSRCBData と並びを一致させること（C++ 側は 192B）。

cbuffer SSRCB : register(b0)
{
    float4x4 g_ViewProj;            // view × projection（ワールド → クリップ）
    float4x4 g_View;                // ワールド → ビュー（z はカメラから前方へ正）
    float4   g_ViewRect;            // ビュー矩形 (x, y, w, h)（ピクセル）
    float2   g_ScreenSize;          // 画面全体の寸法（ピクセル）
    float    g_MaxDistance;         // レイの最大長（ワールド単位）
    float    g_ThicknessScale;      // 厚み係数
    uint     g_MaxSteps;            // 線形マーチの最大ステップ数
    uint     g_BinarySteps;         // 二分探索の回数
    float    g_EdgeFade;            // 画面端で確信度を下げる幅（ビュー矩形に対する割合）
    float    g_NearZ;               // near 面
    float    g_RoughnessFadeStart;  // この roughness から確信度を下げ始める
    float    g_RoughnessFadeEnd;    // この roughness 以上は SSR を使わない
    float2   g_Pad;
};

Texture2D<float4>   g_Scene    : register(t0);   // ライティング済みの HDR シーン
Texture2D<float4>   g_GBuffer1 : register(t1);   // N.xyz + roughness
Texture2D<float4>   g_GBuffer2 : register(t2);   // worldPos.xyz + specular
Texture2D<float4>   g_GBuffer3 : register(t3);   // emissive*scale + pixelTag
RWTexture2D<float4> g_Out      : register(u0);

// 1 サンプルの判定結果
static const uint SAMPLE_FRONT     = 0;  // レイが面の手前（Δ < 0）か背景
static const uint SAMPLE_CANDIDATE = 1;  // 0 ≤ Δ ≤ 厚み
static const uint SAMPLE_BEHIND    = 2;  // Δ > 厚み（物の裏側を通過）
static const uint SAMPLE_OUTSIDE   = 3;  // ビュー矩形の外

// 確信度を 0 にする「カメラ向きのレイ」の範囲（ビュー空間の R.z が -0.2 → 0 で 0 → 1。§0.3-5）
static const float FACING_FADE_RANGE = 0.2;

// 距離の確信度を下げ始める割合（最大距離の最後の 20%。§0.3-3）
static const float DISTANCE_FADE_RANGE = 0.2;

// 画面へ投影したレイ（両端の画素座標と 1/z）
struct ScreenRay
{
    float2 pixS;
    float2 pixE;
    float  invZS;
    float  invZE;
};

// worldPos（half）の誤差幅（§0.5）
float ErrorWidth(float3 p)
{
    float3 a = abs(p);
    return exp2(-10.0) * max(a.x, max(a.y, a.z)) * 2.0;
}

// ワールド座標 → ビュー空間の深度
float ViewZ(float3 worldPos)
{
    return mul(g_View, float4(worldPos, 1.0)).z;
}

// クリップ座標 → 画素座標（ビュー矩形の中。MotionBlur.fx と同じ y 下向き）
float2 ToPixel(float4 clipPos)
{
    float2 ndc = clipPos.xy / clipPos.w;
    return g_ViewRect.xy + float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * g_ViewRect.zw;
}

int2 RayPixel(ScreenRay ray, float t)
{
    return int2(floor(lerp(ray.pixS, ray.pixE, t)));
}

// 透視補正: 1/z を画面上で線形補間する
float RayZ(ScreenRay ray, float t)
{
    return 1.0 / lerp(ray.invZS, ray.invZE, t);
}

bool InViewRect(int2 p)
{
    int2 lo = int2(round(g_ViewRect.xy));
    int2 hi = min(int2(round(g_ViewRect.xy + g_ViewRect.zw)), int2(g_ScreenSize));
    return all(p >= lo) && all(p < hi);
}

// レイ上の位置 t（画面上の 0〜1）の判定（§0.4-3）
uint Classify(ScreenRay ray, float t)
{
    int2 p = RayPixel(ray, t);
    if (!InViewRect(p)) {
        return SAMPLE_OUTSIDE;
    }

    // 背景（空）は遮る物が無いので「手前」と同じ扱い
    if (g_GBuffer3.Load(int3(p, 0)).a < 0.5) {
        return SAMPLE_FRONT;
    }

    float3 hitWorldPos = g_GBuffer2.Load(int3(p, 0)).xyz;
    float  zScene      = ViewZ(hitWorldPos);
    float  delta       = RayZ(ray, t) - zScene;
    if (delta < 0.0) {
        return SAMPLE_FRONT;
    }

    float thickness = max(g_ThicknessScale * zScene, 4.0 * ErrorWidth(hitWorldPos));
    return (delta <= thickness) ? SAMPLE_CANDIDATE : SAMPLE_BEHIND;
}

float4 Trace(int2 pix)
{
    float4 g3 = g_GBuffer3.Load(int3(pix, 0));
    if (g3.a < 0.5) {
        return float4(0.0, 0.0, 0.0, 0.0);   // G-Buffer の無い画素（空）
    }

    float4 g1        = g_GBuffer1.Load(int3(pix, 0));
    float  roughness = g1.w;
    if (roughness >= g_RoughnessFadeEnd) {
        return float4(0.0, 0.0, 0.0, 0.0);   // 荒い面はキューブに任せる（§0.2）
    }

    float3 N = normalize(g1.xyz);
    float3 P = g_GBuffer2.Load(int3(pix, 0)).xyz;

    // 反射ベクトルはビュー空間で作る（カメラが原点なので視線 = 画素の位置）
    float3x3 viewRot = (float3x3)g_View;
    float3   Pv      = mul(g_View, float4(P, 1.0)).xyz;
    float3   Nv      = normalize(mul(viewRot, N));
    float3   Rv      = reflect(normalize(Pv), Nv);

    // カメラ向きのレイ（§0.3-5）
    float facingFade = saturate((Rv.z + FACING_FADE_RANGE) / FACING_FADE_RANGE);
    if (facingFade <= 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // ワールドの反射ベクトル（回転の逆 = 転置）
    float3 R = normalize(mul(Rv, viewRot));

    // 1. 出発点（§0.4-1）
    float  bias   = 2.0 * ErrorWidth(P);
    float3 startW = P + N * bias;
    float  zStart = ViewZ(startW);
    if (zStart <= g_NearZ) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // 2. 終点。near 面の手前に出るなら near 面で切る（ビュー空間の z はレイに沿って Rv.z で変わる）
    float rayLength = g_MaxDistance;
    float zEnd      = zStart + Rv.z * rayLength;
    float nearLimit = g_NearZ * 1.001;
    if (zEnd < nearLimit) {
        rayLength = (nearLimit - zStart) / Rv.z;   // Rv.z < 0
        if (rayLength <= 0.0) {
            return float4(0.0, 0.0, 0.0, 0.0);
        }
    }
    float3 endW = startW + R * rayLength;

    ScreenRay ray;
    ray.pixS  = ToPixel(mul(g_ViewProj, float4(startW, 1.0)));
    ray.pixE  = ToPixel(mul(g_ViewProj, float4(endW, 1.0)));
    ray.invZS = 1.0 / zStart;
    ray.invZE = 1.0 / ViewZ(endW);

    // 等間隔のステップ数（最大 g_MaxSteps。1 ステップは最低 1 画素）
    float2 deltaPix   = ray.pixE - ray.pixS;
    float  lengthPix  = max(abs(deltaPix.x), abs(deltaPix.y));
    uint   maxSteps   = max(g_MaxSteps, 1u);
    uint   stepCount  = (uint)clamp(ceil(lengthPix), 1.0, (float)maxSteps);
    float  dt         = 1.0 / (float)stepCount;

    // 3. 線形マーチ（§0.4-3〜5）
    //    直前が「手前」で今が「手前でない」（候補・厚み超過のどちらでも）なら、その間で面をまたいだとみなして
    //    二分探索で詰め、詰めた位置で 0 ≤ Δ ≤ 厚み と面の向きを再判定する。外れたら打ち切らずに進み続ける
    //    （1 ステップで薄い物を飛び越すと「厚み超過」になり、そこで外れにすると画素ごとに当たり外れが入れ替わって縞になる）。
    //    直前が「手前」でない（出発直後・厚み超過の直後）ときの候補は、詰めずにその位置で判定する。
    bool  prevFront = false;
    float prevT     = 0.0;
    float hiT       = -1.0;
    [loop]
    for (uint i = 1; i <= stepCount; ++i)
    {
        float t      = dt * (float)i;
        uint  result = Classify(ray, t);
        if (result == SAMPLE_OUTSIDE) {
            return float4(0.0, 0.0, 0.0, 0.0);   // ビュー矩形の外 → 外れで打ち切り
        }
        if (result != SAMPLE_FRONT && (prevFront || result == SAMPLE_CANDIDATE))
        {
            float loT  = prevT;
            float candT = t;
            if (prevFront) {
                // 4. 二分探索。中点が手前なら lo、それ以外（厚み超過を含む）なら hi を寄せる
                [loop]
                for (uint j = 0; j < g_BinarySteps; ++j)
                {
                    float midT = 0.5 * (loT + candT);
                    if (Classify(ray, midT) == SAMPLE_FRONT) {
                        loT = midT;
                    } else {
                        candT = midT;
                    }
                }
            }
            // 5. 候補の棄却: 最終位置で 0 ≤ Δ ≤ 厚み を再判定し、裏面に当たったものも外す（外れたら進み続ける）
            if (Classify(ray, candT) == SAMPLE_CANDIDATE)
            {
                int2   candPix = RayPixel(ray, candT);
                float3 candN   = normalize(g_GBuffer1.Load(int3(candPix, 0)).xyz);
                if (dot(candN, R) <= 0.0) {
                    hiT = candT;
                    break;
                }
            }
        }
        prevFront = (result == SAMPLE_FRONT);
        prevT     = t;
    }
    if (hiT < 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    int2   hitPix = RayPixel(ray, hiT);

    // 確信度（§0.3）
    // 画面端: 当たった画素がビュー矩形の端から g_EdgeFade の幅に入るほど 0 へ
    float2 hitUV    = (float2(hitPix) + 0.5 - g_ViewRect.xy) / g_ViewRect.zw;
    float  edgeDist = min(min(hitUV.x, 1.0 - hitUV.x), min(hitUV.y, 1.0 - hitUV.y));
    float  edgeFade = (g_EdgeFade > 0.0) ? saturate(edgeDist / g_EdgeFade) : 1.0;

    // 距離: 画面上の t をレイ上の割合 s へ戻す（s/z と 1/z が画面上で線形）
    float s           = hiT * ray.invZE / lerp(ray.invZS, ray.invZE, hiT);
    float rayDistance = s * rayLength;
    float distFade    = saturate((g_MaxDistance - rayDistance) / (DISTANCE_FADE_RANGE * g_MaxDistance));

    // 荒さ: g_RoughnessFadeStart → g_RoughnessFadeEnd で 1 → 0
    float roughFade = 1.0 - saturate((roughness - g_RoughnessFadeStart)
                                     / max(g_RoughnessFadeEnd - g_RoughnessFadeStart, 1e-4));

    float confidence = edgeFade * distFade * roughFade * facingFade;

    // 6. 当たった画素の Scene の色（強度は掛けない。§0.2）
    float3 hitColor = g_Scene.Load(int3(hitPix, 0)).rgb;
    return float4(hitColor, confidence);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // ビュー矩形の範囲だけを回す（dispatch は矩形の寸法。原点はここで足す。§0.6-4）
    if (id.x >= (uint)g_ViewRect.z || id.y >= (uint)g_ViewRect.w) {
        return;
    }
    int2 pix = int2(round(g_ViewRect.xy)) + int2(id.xy);
    if (pix.x >= (int)g_ScreenSize.x || pix.y >= (int)g_ScreenSize.y) {
        return;
    }

    g_Out[uint2(pix)] = Trace(pix);
}
