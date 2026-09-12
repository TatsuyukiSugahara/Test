// 草の房 (MeshPrimitives::BuildGrassTuftMesh) と花 (同 BuildFlowerMesh) 用のインスタンシングシェーダ。
// 花は茎を持たない花冠だけのメッシュにしてあるので、per-instance 色 1 色のこのシェーダで塗れる。
// 入力レイアウトは InstancedSimple.fx と同じ (slot0 = 位置/法線/UV、slot1 = I_WORLD0..3 + I_COLOR)。
// 違いは VS の風変位と PS の縦グラデーションの 2 点だけ。
// uv.y は「根元 0 / 先端 1」の葉の高さで、風の振幅と色グラデーションの両方がこれを使う。

cbuffer VSPSCb : register(b0)
{
    float4x4 world;    // 未使用(per-instance の I_WORLD を使う)
    float4x4 view;
    float4x4 project;
};

// 風パラメータ。インスタンスメッシュ経路はマテリアル CB を bind しないので b2 を借りている。
cbuffer WindCB : register(b2)
{
    float4 windParams;      // x=経過時間[s], y=揺れ幅[m], z=周波数, w=未使用
    float4 windDirection;   // xyz=風向(正規化済み), w=未使用
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
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

/**
 * 頂点シェーダーのエントリ関数
 */
PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput) 0;
    float4x4 iworld = float4x4(input.iWorld0, input.iWorld1, input.iWorld2, input.iWorld3);
    float4 worldPos = mul(iworld, float4(input.position, 1.0f));

    // 風の変位。位相に株のワールド位置を混ぜることで、per-instance データを増やさずに
    // 株ごとの揺れをずらす。重みは uv.y の2乗なので根元(uv.y=0)は動かず、先端ほど大きく揺れる。
    float phase = windParams.x * windParams.z + worldPos.x * 0.15f + worldPos.z * 0.13f;
    float sway  = sin(phase) * windParams.y * input.tex.y * input.tex.y;
    // 低周波の突風で振幅を 0.55〜1.0 倍する。位相は時間 0.23rad/s + 波長 800m 前後のワールド位置なので、
    // 揺れ本体よりずっとゆっくり広く動き、野原を波が渡って見える。
    float gust = 0.55f + 0.45f * sin(windParams.x * 0.23f + worldPos.x * 0.008f + worldPos.z * 0.006f);
    worldPos.xyz += windDirection.xyz * (sway * gust);

    float4 position = mul(view, worldPos);
    position = mul(project, position);
    o.position = position;
    o.normal = normalize(mul((float3x3) iworld, input.normal));
    o.uv = input.tex;
    o.color = input.iColor;
    return o;
}

/**
 * ピクセルシェーダーのエントリ関数
 */
float4 PSMain(PSInput input) : SV_Target0
{
    // ライトは InstancedSimple と同じハードコード方向。環境項 + 拡散で立体感だけを付ける。
    float3 n = normalize(input.normal);
    float3 lightDir = normalize(float3(0.3f, 1.0f, 0.4f));
    float shade = 0.6f + 0.4f * saturate(dot(n, lightDir));
    // 縦グラデーション。根元を暗くして地面との接地感を出す (根元を 0.32 まで落として影を強めている)。
    float gradient = lerp(0.32f, 1.0f, input.uv.y);
    return float4(input.color.rgb * shade * gradient, input.color.a);
}
