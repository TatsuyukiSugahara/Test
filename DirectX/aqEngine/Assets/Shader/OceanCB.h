// b5: OceanCB — Gerstner 波・UV スクロール・Fresnel パラメータ
// C++ 側の OceanCBData (OceanData.h) と 1:1 で一致させること (192 bytes = 12 float4)
cbuffer OceanCB : register(b5)
{
    float  oceanTime;
    float  waveQ;                    // 水平変位係数 (0=正弦波, ~0.3=穏やか, 1=最大急峻)
    float2 _oceanPad0;               // row 0: 合計 16 bytes

    float4 normalParams1;            // row 1: x=スケール, y=方向X, z=方向Z, w=スクロール速度
    float4 normalParams2;            // row 2: x=スケール, y=方向X, z=方向Z, w=スクロール速度

    float4 deepColor;                // row 3: xyz=深い海の色, w=パッド
    float4 shallColor;               // row 4: xyz=浅い海の色, w=パッド

    float4 fresnelParams;            // row 5: x=バイアス, y=スケール, z=累乗, w=太陽の光沢度
    float4 sunSky;                   // row 6: x=太陽の反射強度, yzw=空の色

    float4 waveParams[4];            // row 7-10: xy=伝播方向(XZ), z=振幅, w=波長
    float4 waveSpeeds;               // row 11: xyzw = 波 0-3 の位相速度
};
