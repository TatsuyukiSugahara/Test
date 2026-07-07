// パーティクル ビルボード シェーダ。
// 頂点は CPU 側でカメラ right/up によりワールド空間へ展開済み。
// テクスチャは使わず、uv 距離で柔らかく減衰させた丸い粒を頂点カラーで着色する。

cbuffer VSPSCb : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 project;
};

struct VSInput
{
    float3 position : POSITION;    // ワールド座標
    float2 uv       : TEXCOORD0;   // クアッド内 [0,1]
    float4 color    : COLOR0;      // RGBA
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output = (PSInput) 0;
    float4 p = float4(input.position, 1.0f);
    p = mul(world, p);
    p = mul(view, p);
    p = mul(project, p);
    output.position = p;
    output.uv       = input.uv;
    output.color    = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_Target0
{
    float2 d = input.uv * 2.0f - 1.0f;          // 中心基準 [-1,1]
    float falloff = saturate(1.0f - dot(d, d)); // 中心 1・周縁 0 の柔らかい円
    return float4(input.color.rgb * falloff, input.color.a * falloff);
}
