// インスタンシング版 テクスチャ付きモデル(FBX/TKM 等)。ワールド行列は per-instance 頂点
// ストリーム(slot1)から取る。CPU 側は転置したワールドを4行(I_WORLD0..3)で渡す(CB 経路と同姿勢)。
// ライティングは簡易(ハードコード方向光の Lambert + 環境項)で、CB 依存を持たず自己完結させる。

cbuffer VSPSCb : register(b0)
{
    float4x4 world;    // 未使用(per-instance の I_WORLD を使う)
    float4x4 view;
    float4x4 project;
};

Texture2D    albedoTex : register(t0);
SamplerState samp      : register(s0);

/**
 * 頂点シェーダーの入力(slot0 = VertexData 順: position/normal/uv/tangent、slot1 = per-instance)
 */
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    float4 iWorld0  : I_WORLD0;
    float4 iWorld1  : I_WORLD1;
    float4 iWorld2  : I_WORLD2;
    float4 iWorld3  : I_WORLD3;
};
struct PSInput
{
    float4 svPos : SV_POSITION;
    float3 N     : TEXCOORD0;
    float2 uv    : TEXCOORD1;
};

PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput) 0;
    float4x4 iworld = float4x4(input.iWorld0, input.iWorld1, input.iWorld2, input.iWorld3);
    float4 wpos = mul(iworld, float4(input.position, 1.0f));
    o.svPos = mul(project, mul(view, wpos));
    // 一様スケール前提で法線を world 空間へ(非一様スケールなら逆転置が必要)。
    o.N  = normalize(mul((float3x3) iworld, input.normal));
    o.uv = input.uv;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedo = albedoTex.Sample(samp, input.uv);
    const float3 lightDir = normalize(float3(0.3f, 1.0f, 0.4f));
    float ndl = saturate(dot(normalize(input.N), lightDir)) * 0.7f + 0.3f;   // Lambert + 環境項
    return float4(albedo.rgb * ndl, albedo.a);
}
