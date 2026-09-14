// インスタンシング版 SimpleBox。ワールド行列は per-instance 頂点ストリーム(slot1)から取る。
// CPU 側は「転置したワールド行列」を4行(I_WORLD0..3)として渡す。これにより
// float4x4(iWorld0..3) が、CB 経由(既定の列優先読みで暗黙転置される)と同じ姿勢になり、
// 既存 SimpleBox と同一の mul(world, position) で正しく描画される。

cbuffer VSPSCb : register(b0)
{
    float4x4 world;    // 未使用(per-instance の I_WORLD を使う)
    float4x4 view;
    float4x4 project;
};

/**
 * 頂点シェーダーの入力
 */
struct VSInput
{
    float3 position : SV_Position;   // slot0(共有ジオメトリ)
    float3 normal   : NORMAL0;
    float2 tex      : TEXCOORD0;
    float4 iWorld0  : I_WORLD0;      // slot1(per-instance・転置済みワールドの各行)
    float4 iWorld1  : I_WORLD1;
    float4 iWorld2  : I_WORLD2;
    float4 iWorld3  : I_WORLD3;
    float4 iColor   : I_COLOR;       // slot1(per-instance・RGBA)
};
/**
 * ピクセルシェーダーの入力
 */
struct PSInput
{
    float4 position : SV_Position;
    float3 normal   : NORMAL0;
    float2 tex      : TEXCOORD0;
    float4 color    : COLOR0;
};

/**
 * 頂点シェーダーのエントリ関数
 */
PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput) 0;
    float4x4 iworld = float4x4(input.iWorld0, input.iWorld1, input.iWorld2, input.iWorld3);
    float4 position = float4(input.position, 1.0f);
    position = mul(iworld, position);
    position = mul(view, position);
    position = mul(project, position);
    o.position = position;
    o.normal = normalize(mul((float3x3) iworld, input.normal));
    o.tex = input.tex;
    o.color = input.iColor;
    return o;
}

/**
 * ピクセルシェーダーのエントリ関数
 */
float4 PSMain(PSInput input) : SV_Target0
{
    // ライトは InstancedModelTex と同じハードコード方向。環境項 + 拡散で立体感だけを付ける。
    float3 n = normalize(input.normal);
    float3 lightDir = normalize(float3(0.3f, 1.0f, 0.4f));
    float shade = 0.6f + 0.4f * saturate(dot(n, lightDir));
    return float4(input.color.rgb * shade, input.color.a);
}
