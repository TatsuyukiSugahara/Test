// クラスタ(メッシュレット)カリング compute。
// 各クラスタを 1 スレッドで判定し、可視クラスタの並べ替え済みインデックスを
// 出力IBへ compact しつつ、間接引数の IndexCountPerInstance を InterlockedAdd で集計する。
//
// Step 3b: フラスタム + バックフェース錐 + Hi-Z オクリュージョン。
//   Hi-Z は前フレームのピラミッド (Pass 1.5 はピラミッド構築前) を SRV で供給する 1 フレーム遅延方式。
//   投影/サンプル式は CPU HiZRenderer::IsOccluded と一致 (覆う矩形の最遠 max 深度 vs 物体最近接線形深度)。
//
// CPU 側 graphics::MeshCluster と完全一致のレイアウト (48 bytes):
//   float3 center; float3 extent; float3 coneAxis; float coneCutoff; uint triOffset; uint triCount;
struct ClusterGPU
{
    float3 center;
    float3 extent;
    float3 coneAxis;
    float  coneCutoff;
    uint   triOffset;
    uint   triCount;
};

StructuredBuffer<ClusterGPU> g_Clusters    : register(t0);  // クラスタ記述子
ByteAddressBuffer            g_SrcIndices   : register(t1);  // 並べ替え済みインデックス (uint 列)
Texture2D<float>            g_HiZ          : register(t2);  // 前フレーム Hi-Z (R32, max-reduction)
RWByteAddressBuffer          g_OutIndices   : register(u0);  // compact 出力IB
RWByteAddressBuffer          g_IndirectArgs : register(u1);  // [0]=IndexCount を InterlockedAdd

// 単一 CBV (compute ルートシグネチャは b0 のみ) に全パラメータを集約。
// フラスタム平面は viewProj から導出するため別途渡さない (b1 不要)。
cbuffer ClusterCullCB : register(b0)
{
    float4x4 g_World;       // メッシュのワールド行列 (列ベクトル規約: mul(M, v))
    float4x4 g_ViewProj;    // clip空間 view*projection (mul(M, v).x = clip.x, .w = clip.w)
    float3   g_CamPos;
    uint     g_ClusterCount;
    float    g_HiZW;        // バインドした Hi-Z レベル幅 [テクセル]
    float    g_HiZH;        // 同 高さ
    float    g_NearZ;
    float    g_FarZ;
    uint     g_HiZValid;    // 0 = Hi-Z 無効 (オクリュージョン判定スキップ)
};

// ワールド AABB (中心 wc, 半サイズ we) を Hi-Z で遮蔽判定する。true=完全に背後=カリング可。
// CPU HiZRenderer::IsOccluded と同じく、覆うスクリーン矩形の最遠 (max) 深度より
// 物体の最近接線形深度が奥なら遮蔽とみなす。判定不能時は保守的に false (可視)。
bool IsClusterOccluded(float3 wc, float3 we)
{
    if (g_HiZValid == 0) return false;

    float2 mn = float2( 1e30f,  1e30f);
    float2 mx = float2(-1e30f, -1e30f);
    float  nearestLin = 1e30f;

    [unroll]
    for (int k = 0; k < 8; ++k)
    {
        float3 corner = wc + float3((k & 1) ? we.x : -we.x,
                                    (k & 2) ? we.y : -we.y,
                                    (k & 4) ? we.z : -we.z);
        float4 clip = mul(g_ViewProj, float4(corner, 1.0f));
        if (clip.w <= 1e-4f) return false;  // near 平面跨ぎ → 可視扱い (保守的)

        float2 ndc = clip.xy / clip.w;
        float  sx  = (ndc.x * 0.5f + 0.5f) * g_HiZW;
        float  sy  = (0.5f - ndc.y * 0.5f) * g_HiZH;  // NDC y 上 → テクスチャ y 下
        mn = min(mn, float2(sx, sy));
        mx = max(mx, float2(sx, sy));
        nearestLin = min(nearestLin, (clip.w - g_NearZ) / max(g_FarZ - g_NearZ, 1e-6f));
    }

    // 画面外なら判定しない (可視扱い)
    if (mx.x < 0.0f || mx.y < 0.0f || mn.x >= g_HiZW || mn.y >= g_HiZH) return false;

    int ix0 = (int)floor(max(mn.x, 0.0f));
    int iy0 = (int)floor(max(mn.y, 0.0f));
    int ix1 = (int)floor(min(mx.x, g_HiZW - 1.0f));
    int iy1 = (int)floor(min(mx.y, g_HiZH - 1.0f));

    // ループ上限。矩形が大きすぎるクラスタは保守的に可視扱い (粗い readback レベルでは稀)。
    const int kMaxSpan = 16;
    if (ix1 - ix0 >= kMaxSpan || iy1 - iy0 >= kMaxSpan) return false;

    float maxHiZ = 0.0f;
    for (int ty = iy0; ty <= iy1; ++ty)
        for (int tx = ix0; tx <= ix1; ++tx)
            maxHiZ = max(maxHiZ, g_HiZ.Load(int3(tx, ty, 0)));

    return nearestLin > maxHiZ + 0.002f;  // 最近点が最遠オクルーダーより奥 → 遮蔽
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint ci = id.x;
    if (ci >= g_ClusterCount) return;

    ClusterGPU c = g_Clusters[ci];

    // ローカル AABB → ワールド AABB (CPU AABB::Transformed と一致)
    float3 wc = mul(g_World, float4(c.center, 1.0f)).xyz;
    float3 we;
    we.x = dot(abs(g_World[0].xyz), c.extent);
    we.y = dot(abs(g_World[1].xyz), c.extent);
    we.z = dot(abs(g_World[2].xyz), c.extent);

    // フラスタムカリング (世界 AABB × 6 平面)。平面は viewProj の行から導出する。
    // clip = mul(g_ViewProj, p4) より、内側条件: -w<=x<=w, -w<=y<=w, 0<=z<=w。
    //   left = row3+row0, right = row3-row0, bottom = row3+row1, top = row3-row1,
    //   near = row2, far = row3-row2。各平面で dot(P.xyz,wc)+P.w + r >= 0 を要求。
    float4 r0 = g_ViewProj[0], r1 = g_ViewProj[1], r2 = g_ViewProj[2], r3 = g_ViewProj[3];
    float4 planes[6] = { r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2 };
    [unroll]
    for (int p = 0; p < 6; ++p)
    {
        float s = dot(planes[p].xyz, wc) + planes[p].w;
        float rr = dot(abs(planes[p].xyz), we);
        if (s + rr < 0.0f) return;  // 完全に外 → カリング
    }

    // バックフェースカリング (法線錐 × 視線)。CPU IsClusterVisible と一致。
    // coneAxis をワールドへ法線変換 (上3x3) → 正規化。coneCutoff=2.0 は判定不能=常に可視。
    float3 axis    = normalize(mul((float3x3)g_World, c.coneAxis));
    float3 viewDir = normalize(wc - g_CamPos);
    if (dot(viewDir, axis) >= c.coneCutoff) return;  // クラスタ全体が裏向き → カリング

    // Hi-Z オクリュージョン (前フレームピラミッド・1 フレーム遅延)
    if (IsClusterOccluded(wc, we)) return;

    // 可視: 出力IBへ範囲を予約してコピー
    const uint count   = c.triCount * 3u;
    const uint srcBase = c.triOffset * 3u;
    uint dstOffset;
    g_IndirectArgs.InterlockedAdd(0, count, dstOffset);  // dstOffset = 旧 IndexCount

    for (uint i = 0; i < count; ++i)
    {
        uint idx = g_SrcIndices.Load((srcBase + i) * 4u);
        g_OutIndices.Store((dstOffset + i) * 4u, idx);
    }
}
