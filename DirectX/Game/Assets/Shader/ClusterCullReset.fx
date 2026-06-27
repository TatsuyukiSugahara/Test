// クラスタカリング: 間接引数バッファをフレーム先頭でリセットする (1 スレッド)。
// レイアウト = D3D12_DRAW_INDEXED_ARGUMENTS:
//   [0] IndexCountPerInstance  ← cull シェーダが InterlockedAdd で集計する
//   [1] InstanceCount = 1
//   [2] StartIndexLocation = 0
//   [3] BaseVertexLocation = 0
//   [4] StartInstanceLocation = 0

RWByteAddressBuffer g_IndirectArgs : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    g_IndirectArgs.Store(0,  0u);  // IndexCountPerInstance
    g_IndirectArgs.Store(4,  1u);  // InstanceCount
    g_IndirectArgs.Store(8,  0u);  // StartIndexLocation
    g_IndirectArgs.Store(12, 0u);  // BaseVertexLocation
    g_IndirectArgs.Store(16, 0u);  // StartInstanceLocation
}
