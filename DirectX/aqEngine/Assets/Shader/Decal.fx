// 投影デカール (Deferred projected decal)。
//
// デカール毎にフルスクリーントライアングルを描画し、GBuffer2 の worldPos を
// デカールローカル空間 ([-0.5,0.5]^3) へ逆変換、箱の外を clip して
// デカールテクスチャを GBuffer0 (albedo) へ書き戻す。
//
// ブレンド: DecalColor (RGB のみ src.a アルファ合成・アルファ ch は書き込みマスク)
//           → GBuffer0.a = metallic を破壊しない。
// 呼び出し: Draw(3, 0)（頂点バッファ不要）。

Texture2D    decalTex : register(t0);   // デカールテクスチャ (rgb + a=カバレッジ)
Texture2D    gbuffer1 : register(t9);   // N.xyz + roughness
Texture2D    gbuffer2 : register(t10);  // worldPos.xyz + specular
Texture2D    gbuffer3 : register(t11);  // emissive + pixelTag
SamplerState decalSamp : register(s0);

cbuffer DecalCB : register(b0)
{
    float4x4 worldToDecal;   // ワールド → デカールローカル
    float4   decalColor;     // rgb=色ティント, a=不透明度
    float4   decalForward;   // xyz=投影軸(ワールド), w=角度フェード下限 (cosθ)
    float4   decalPad[6];
};

struct VSOutput
{
    float4 svPos : SV_POSITION;
    float2 uv    : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    o.uv    = float2((vertexID << 1) & 2, vertexID & 2);
    o.svPos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    int2 coord = int2(input.svPos.xy);

    // 空背景ピクセル (pixelTag < 0.5) はスキップ
    float pixelTag = gbuffer3.Load(int3(coord, 0)).a;
    clip(pixelTag - 0.5);

    // pixelTag >= 4.5 は「デカール非対象」マーカー → このピクセルには貼らない
    clip(4.5 - pixelTag);

    // worldPos → デカールローカル ([-0.5, 0.5]^3)
    float3 worldPos = gbuffer2.Load(int3(coord, 0)).xyz;
    float3 local    = mul(worldToDecal, float4(worldPos, 1.0)).xyz;

    // 箱の外を破棄
    float3 inside = 0.5 - abs(local);
    clip(min(inside.x, min(inside.y, inside.z)));

    // local.xz → UV (ローカル -Y 方向へ投影 = 無回転で地面に水平に貼る)
    float2 uv  = float2(local.x + 0.5, 0.5 - local.z);
    float4 tex = decalTex.Sample(decalSamp, uv);

    // 角度フェード: 面法線が投影軸に正対するほど不透明
    float3 N     = normalize(gbuffer1.Load(int3(coord, 0)).xyz);
    float  ndotf = dot(N, -decalForward.xyz);
    float  fade  = saturate((ndotf - decalForward.w) / max(1.0 - decalForward.w, 1e-4));

    float alpha = tex.a * decalColor.a * fade;
    return float4(tex.rgb * decalColor.rgb, alpha);
}
