// UICircleGauge.fx -- 円形ゲージ専用シェーダー
// fillAmount / startAngle / clockwise を CB で受け取り、ピクセルを角度でクリップする

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

cbuffer CircleGaugeCB : register(b0)
{
    float g_fillAmount;   // 0=空, 1=満タン
    float g_startAngle;   // ラジアン (0=上, 正方向は g_clockwise に依存)
    float g_clockwise;    // 1.0=時計回り, -1.0=反時計回り
    float g_pad;
};

// === 頂点シェーダー ===
PSIn VSMain(VSIn v)
{
    PSIn o;
    o.position = float4(v.position, 0.0f, 1.0f);
    o.uv       = v.uv;
    o.color    = v.color;
    return o;
}

// === ピクセルシェーダー ===
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

static const float TAU = 6.28318530717959f;

float4 PSMain(PSIn p) : SV_TARGET
{
    // UV 中心を原点に
    float2 c = p.uv - 0.5f;

    // 中心点 (ゼロ除算回避) は常に描画
    if (dot(c, c) < 1e-8f)
        return gTexture.Sample(gSampler, p.uv) * p.color;

    // atan2 で角度 [-π, π] を計算。上=0, CW 方向が正。
    float angle = atan2(c.x, -c.y);

    // startAngle を基点に [0, τ) へ正規化
    float diff = angle - g_startAngle;
    diff -= TAU * floor(diff / TAU);

    // CW / CCW で比較する角度を決定
    float cmp = (g_clockwise > 0.0f) ? diff : (TAU - diff);

    // fillAmount * τ を超えたらクリップ
    if (cmp > g_fillAmount * TAU)
        discard;

    return gTexture.Sample(gSampler, p.uv) * p.color;
}
