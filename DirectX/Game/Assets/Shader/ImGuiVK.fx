// ImGui 描画用シェーダ (Vulkan 自前バックエンド VulkanImGui)。
// 頂点: pos(float2), uv(float2), col(RGBA8 → 正規化 float4)。
// b0=scale/translate(正射影), t0=フォント/ユーザーテクスチャ, s0=サンプラ。
// scale/translate で imgui 座標(Y下) → Vulkan NDC(Y下) へ直接写像 (ビューポートは正・非Y-flip)。

cbuffer ProjCB : register(b0)
{
    float4 uScaleTranslate;  // xy = scale, zw = translate
};

struct VSIn
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos * uScaleTranslate.xy + uScaleTranslate.zw, 0.0, 1.0);
    o.col = i.col;
    o.uv  = i.uv;
    return o;
}

Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

float4 PSMain(VSOut i) : SV_TARGET
{
    return i.col * tex.Sample(samp, i.uv);
}
