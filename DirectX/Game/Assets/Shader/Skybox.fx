// スカイボックス (スカイキューブ)。
//
// フルスクリーン三角形 1 枚でキューブマップを描く。Draw(3, 0) で呼ぶ (頂点バッファ不要)。
// シーン RT (色) + GBuffer0 (深度) がバインド済みの状態で、ディファードライティングの
// 直後・フォワードの直前に描く。深度は ReadOnly (テスト有効・書き込み無し)。
//
// ★深度★ 深度比較は全バックエンドで LESS 固定で、G-Buffer の深度クリア値は 1.0。
//   つまり z = 1.0 で出すと 1.0 < 1.0 が偽になり全画素が落ちて何も映らない。
//   そのため SKY_DEPTH (1.0 未満) で出す。PBRLighting.fx / Decal.fx の VSMain は
//   z = 0.0 (深度無効パス専用) なので、そのまま流用すると空が最前面に来てしまう。
//
// ★視線方向★ invViewProjection は「view の平行移動を 0 にしてから projection と
//   合成した行列」の逆行列。平行移動を残すとカメラの移動で空がずれる (無限遠にならない)。

TextureCube  gSkyCube : register(t5);   // t0-t3=マテリアル / t4=シャドウ / t8-t11=GBuffer
SamplerState gSampler : register(s0);

cbuffer SkyCB : register(b0)
{
    float4x4 invViewProjection;   // 平行移動を抜いた view * projection の逆行列
    float4   skyTint;             // rgb = 色調, a = 強度
    float4   skyPad[7];           // perDrawCBPool のスロット (192B) に合わせる
};

// 深度クリア値 (1.0) より僅かに手前。LESS 比較を通しつつ最遠へ置く。
static const float SKY_DEPTH = 0.999999;

struct VSOutput
{
    float4 svPos : SV_POSITION;
    float3 dir   : TEXCOORD0;   // カメラ位置からの視線方向 (ワールド空間)
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    // フルスクリーン三角形の作り方は PBRLighting.fx / Decal.fx と同じ
    float2 uv  = float2((vertexID << 1) & 2, vertexID & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    VSOutput o;
    o.svPos = float4(ndc, SKY_DEPTH, 1.0);

    // 遠クリップ面 (z = 1) 上の点をワールドへ戻す。平行移動を抜いてあるので
    // 結果はそのままカメラ原点からの方向ベクトルになる。
    // 透視投影では farPoint.w が画面全体で一定 (= 1/far) なので、
    // VS で w 除算しても線形補間のままで誤差は出ない。
    float4 farPoint = mul(invViewProjection, float4(ndc, 1.0, 1.0));
    o.dir = farPoint.xyz / farPoint.w;

    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 dir = normalize(input.dir);
    return gSkyCube.Sample(gSampler, dir) * skyTint;
}
