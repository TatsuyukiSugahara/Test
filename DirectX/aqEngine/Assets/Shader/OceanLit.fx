#include "Lighting.fx"
#include "OceanCB.h"

// 頂点レイアウト (VertexData: position/normal/uv/tangent)
struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 svPos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 N        : TEXCOORD1;  // ワールド空間の法線 (Gerstner 解析的)
    float3 T        : TEXCOORD2;  // ワールド空間のタンジェント
    float3 B        : TEXCOORD3;  // ワールド空間のバイノーマル
    float2 worldXZ  : TEXCOORD4;  // UV スクロール用ワールド XZ
};

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 projection;
};

// t0: 法線マップ1 (オプション — MAT_HAS_NORMAL が立っている場合のみサンプル)
// t1: 法線マップ2 (同上)
Texture2D  normalMap1 : register(t0);
Texture2D  normalMap2 : register(t1);
SamplerState samp     : register(s0);

// ------------------------------------------------------------
// Gerstner 波: 水平 cos / 垂直 sin の慣例
// φ = k * dot(D, worldXZ) - k*v * t
// 変位:   Δxz = A * D * cos(φ),  Δy = A * sin(φ)
// 法線:   Nx = -Σ kA*Dx*cos(φ),  Ny = 1 - Σ kA*sin(φ),  Nz = -Σ kA*Dz*cos(φ)
// タンジェント(X方向): Tx = 1 - Σ kA*Dx²*sin(φ),  Ty = Σ kA*Dx*cos(φ),  Tz = -Σ kA*Dx*Dz*sin(φ)
// ------------------------------------------------------------
void GerstnerWave(float2 worldXZ,
                  out float3 disp,
                  out float3 N,
                  out float3 T)
{
    disp = float3(0.0, 0.0, 0.0);
    float Nx = 0.0, Ny = 1.0, Nz = 0.0;
    float Tx = 1.0, Ty = 0.0, Tz = 0.0;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 rawD = waveParams[i].xy;     // 伝播方向 (正規化前)
        float  A    = waveParams[i].z;      // 振幅
        float  Lw   = waveParams[i].w;      // 波長
        float  v    = waveSpeeds[i];        // 位相速度
        float  lenSq = dot(rawD, rawD);

        // [unroll] との相性のため continue の代わりに if ブロックを使う
        // また ImGui での任意入力に備えて方向を正規化する
        if (Lw >= 0.001 && lenSq > 0.0001)
        {
            float2 D  = rawD * rsqrt(lenSq);    // 単位ベクトルに正規化
            float k   = 6.2831853 / Lw;         // 波数 2π/L
            float phi = k * dot(D, worldXZ) - k * v * oceanTime;
            float c   = cos(phi);
            float s   = sin(phi);
            float kA  = k * A;

            // 変位 (水平成分は waveQ でスケール: 0=正弦波, 1=最大急峻)
            disp.x += waveQ * D.x * A * c;
            disp.y += A * s;
            disp.z += waveQ * D.y * A * c;

            // 法線
            Nx -= D.x * kA * c;
            Ny -= kA * s;
            Nz -= D.y * kA * c;

            // タンジェント
            Tx -= D.x * D.x * kA * s;
            Ty += D.x * kA * c;
            Tz -= D.x * D.y * kA * s;
        }
    }

    N = normalize(float3(Nx, Ny, Nz));
    T = normalize(float3(Tx, Ty, Tz));
}

// ------------------------------------------------------------
// 頂点シェーダー
// ------------------------------------------------------------
PSInput VSMain(VSInput input)
{
    PSInput o = (PSInput)0;

    float4 worldPos4 = mul(world, float4(input.position, 1.0));

    // Gerstner 変位・解析的法線/タンジェントを世界空間で計算
    float3 disp, N, T;
    GerstnerWave(worldPos4.xz, disp, N, T);

    worldPos4.xyz += disp;

    o.svPos    = mul(projection, mul(view, worldPos4));
    o.worldPos = worldPos4.xyz;
    o.worldXZ  = worldPos4.xz;
    o.N        = N;
    o.T        = T;
    o.B        = cross(N, T);  // ModelLit.fx と同じ規約: B = N × T

    return o;
}

// ------------------------------------------------------------
// ピクセルシェーダー
// Stage1: UV スクロール法線マップ
// Stage2: Gerstner 解析的法線 (VS から)
// Stage3: Fresnel + 太陽ハイライト
// ------------------------------------------------------------
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 N;

    if (HasNormalMap())
    {
        // 2 枚の法線マップを異なる方向・速度でスクロールしてブレンド
        float2 scroll1 = normalParams1.yz * (oceanTime * normalParams1.w);
        float2 scroll2 = normalParams2.yz * (oceanTime * normalParams2.w);
        float2 uv1 = input.worldXZ * normalParams1.x + scroll1;
        float2 uv2 = input.worldXZ * normalParams2.x + scroll2;

        float3 n1 = DecodeNormalMap(normalMap1.Sample(samp, uv1).rg);
        float3 n2 = DecodeNormalMap(normalMap2.Sample(samp, uv2).rg);
        float3 blended = normalize(n1 + n2);

        // タンジェント空間 → ワールド空間変換 (TBN は行ベクトル)
        float3 T  = normalize(input.T);
        float3 B  = normalize(input.B);
        float3 Ng = normalize(input.N);
        N = normalize(blended.x * T + blended.y * B + blended.z * Ng);
    }
    else
    {
        // 法線マップなし: Gerstner 解析的法線をそのまま使用
        N = normalize(input.N);
    }

    float3 V    = normalize(cameraPosition - input.worldPos);
    float  NdotV = saturate(dot(N, V));

    // --- Fresnel ---
    // 視線が水面に近いほど反射率が上がる
    float fresnel = fresnelParams.x
                  + fresnelParams.y * pow(max(1.0 - NdotV, 0.0), fresnelParams.z);
    fresnel = saturate(fresnel);

    // --- 海の色 (深さの近似: 真上から見るほど浅い色) ---
    float3 waterColor = lerp(deepColor.rgb, shallColor.rgb, NdotV * 0.5);

    // --- 空の反射色 ---
    float3 skyCol = sunSky.yzw;

    // Fresnel で水色と空色をブレンド
    float3 color = lerp(waterColor, skyCol, fresnel);

    // --- 拡散光 (Fresnel の弱い角度で海面が明るくなる) ---
    float3 L       = normalize(-directionals[0].direction);
    float  diffuse = saturate(dot(N, L));
    color += waterColor * directionals[0].color * directionals[0].intensity
           * diffuse * (1.0 - fresnel) * 0.3;

    // --- 太陽のハイライト (Blinn-Phong) ---
    float3 H     = normalize(L + V);
    float  NdotH = saturate(dot(N, H));
    float  spec  = pow(NdotH, fresnelParams.w) * sunSky.x;
    color += directionals[0].color * spec;

    return float4(color, 1.0);
}
