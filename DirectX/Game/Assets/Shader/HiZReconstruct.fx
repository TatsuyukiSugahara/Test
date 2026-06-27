// Hi-Z ピラミッド レベル0 生成
// GBuffer2 の worldPos から クリップ空間深度 [0,1] を再構成し、
// 2x2 ソース領域の最大深度 (最遠) を出力する (オクリュージョン用の保守的 Hi-Z)。
// 背景 (GBuffer クリア黒 = worldPos 0) は遠方 (depth=1) 扱い。

Texture2D<float4>  g_WorldPos : register(t0);  // GBuffer2: worldPos.xyz
RWTexture2D<float> g_HiZ      : register(u0);  // 出力 Hi-Z レベル0 (R32_Float)

cbuffer HiZCB : register(b0)
{
    float4x4 g_ViewProj;  // view * projection (列ベクトル規約: mul(M, v))
    float    g_Near;
    float    g_Far;
    float2   g_Pad;
};

// ビュー空間深度を [0,1] に線形正規化して返す (近=0 / 遠=1)。
// クリップ深度 (z/w) は遠方に張り付き可視化できないため線形深度を使う。
// 線形深度は距離に単調で、オクリュージョン比較にもそのまま使える。
float ReconstructDepth(int2 p, int2 srcSize)
{
    p = clamp(p, int2(0, 0), srcSize - int2(1, 1));
    float3 wp = g_WorldPos.Load(int3(p, 0)).xyz;

    // 背景 (クリア黒) は遠方扱いにして、空に対して手前の物体を誤って隠さない
    if (dot(wp, wp) < 1e-6f) return 1.0f;

    // クリップ空間 w = ビュー空間 z (LH パースペクティブ)
    float4 clip     = mul(g_ViewProj, float4(wp, 1.0f));
    float  viewZ    = clip.w;
    float  linear01 = saturate((viewZ - g_Near) / max(g_Far - g_Near, 1e-6f));
    return linear01;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint outW, outH;
    g_HiZ.GetDimensions(outW, outH);
    if (id.x >= outW || id.y >= outH) return;

    uint inW, inH;
    g_WorldPos.GetDimensions(inW, inH);
    int2 sz = int2(inW, inH);
    int2 s  = int2(id.xy) * 2;

    float d = ReconstructDepth(s,                sz);
    d = max(d, ReconstructDepth(s + int2(1, 0), sz));
    d = max(d, ReconstructDepth(s + int2(0, 1), sz));
    d = max(d, ReconstructDepth(s + int2(1, 1), sz));

    g_HiZ[id.xy] = d;
}
