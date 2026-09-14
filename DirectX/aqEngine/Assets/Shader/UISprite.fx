// UISprite.fx -- UI スプライト / NineSlice 共通シェーダー

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

// === 頂点シェーダー ===
// 頂点はすでに NDC 座標 (-1〜+1) で渡される
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

float4 PSMain(PSIn p) : SV_TARGET
{
    return gTexture.Sample(gSampler, p.uv) * p.color;
}
