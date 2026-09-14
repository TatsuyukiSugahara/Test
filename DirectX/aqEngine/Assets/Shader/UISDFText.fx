// UISDFText.fx -- MSDF フォントレンダリング
// outline / shadow / gradient に対応。
// VS は UISprite.fx と共通フォーマット。

struct VSIn
{
    float2 position : POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

// b0 は空き (CircleGauge と排他利用のため統一して b1 使用)
cbuffer SdfTextCB : register(b1)
{
    float4 outlineColor;      // rgba
    float4 shadowColor;       // rgba
    float2 shadowOffsetUV;    // atlas UV 空間のオフセット
    float  shadowSoftness;    // SDF 閾値のぼかし幅
    float  outlineWidth;      // SDF threshold からのオフセット (0 = outline なし)
    float  smoothing;         // エッジのアンチエイリアス幅 (= pxRange / (2 * fontSize))
    float3 _pad;
};

Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);  // linear clamp


PSIn VSMain(VSIn v)
{
    PSIn o;
    o.position = float4(v.position, 0.0f, 1.0f);
    o.uv       = v.uv;
    o.color    = v.color;
    return o;
}


// MSDF: 3 チャンネルの中央値をとることで角の歪みを補正
float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}


float4 PSMain(PSIn p) : SV_TARGET
{
    // --- MSDF サンプリング ---
    float3 msdf = gTexture.Sample(gSampler, p.uv).rgb;
    float  dist = median(msdf.r, msdf.g, msdf.b);

    // --- shadow ---
    float3 msdfSh = gTexture.Sample(gSampler, p.uv - shadowOffsetUV).rgb;
    float  distSh = median(msdfSh.r, msdfSh.g, msdfSh.b);
    float  shA    = shadowColor.a
                    * smoothstep(0.5 - shadowSoftness, 0.5 + shadowSoftness, distSh);

    // --- outline (fill より外側の SDF 閾値でエッジ) ---
    float edge0 = max(0.001, 0.5 - outlineWidth);
    float outA  = outlineColor.a
                  * smoothstep(edge0 - smoothing, edge0 + smoothing, dist);

    // --- fill (vertex color でグラデーション対応) ---
    float fillA = p.color.a
                  * smoothstep(0.5 - smoothing, 0.5 + smoothing, dist);

    // --- 前乗算アルファで合成 (shadow → outline → fill の順) ---
    float4 c = float4(shadowColor.rgb * shA, shA);
    float4 oPM = float4(outlineColor.rgb * outA, outA);
    float4 fPM = float4(p.color.rgb     * fillA, fillA);
    c = oPM + c * (1.0 - oPM.a);
    c = fPM + c * (1.0 - fPM.a);

    // AlphaBlend モード向けにストレートアルファへ戻す
    float4 result;
    result.a   = c.a;
    result.rgb = (c.a > 0.001) ? (c.rgb / c.a) : float3(0.0, 0.0, 0.0);
    return result;
}
