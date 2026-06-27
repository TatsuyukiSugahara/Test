// クラスタ(メッシュレット)カリング compute。
// 各クラスタを 1 スレッドで判定し、可視クラスタの並べ替え済みインデックスを
// 出力IBへ compact しつつ、間接引数の IndexCountPerInstance を InterlockedAdd で集計する。
//
// Step 2: フラスタムのみ。Step 3 でバックフェース錐 + Hi-Z を追加予定。
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
RWByteAddressBuffer          g_OutIndices   : register(u0);  // compact 出力IB
RWByteAddressBuffer          g_IndirectArgs : register(u1);  // [0]=IndexCount を InterlockedAdd

cbuffer ClusterCullCB : register(b0)
{
    float4x4 g_World;       // メッシュのワールド行列 (列ベクトル規約: mul(M, v))
    float4   g_Planes[6];   // ワールド空間 視錐台 6 平面 (a,b,c,d), 法線側が内側
    float3   g_CamPos;
    uint     g_ClusterCount;
};

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

    // フラスタムカリング (世界 AABB × 6 平面)
    [unroll]
    for (int p = 0; p < 6; ++p)
    {
        float s = dot(g_Planes[p].xyz, wc) + g_Planes[p].w;
        float r = dot(abs(g_Planes[p].xyz), we);
        if (s + r < 0.0f) return;  // 完全に外 → カリング
    }

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
