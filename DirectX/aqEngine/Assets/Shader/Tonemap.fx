// Tonemap.fx
// HDR(リニア相当)の色を LDR [0,1] へ圧縮するトーンマップ演算子群。
// BloomComposite.fx から include して使用する。
// TonemapMode の番号は C++ 側 BloomRenderer::TonemapMode と一致させること。
//
// なぜ必要か:
//   PBR ではライト/反射/発光が物理スケールで計算され、明るさは平気で 1.0 を超える(HDR)。
//   ディスプレイは 0..1(LDR) しか出せないため、何もしないと 1.0 超はハードクランプされて
//   ハイライトが「のっぺりした真っ白」に潰れる。トーンマップは広い HDR の階調を保ったまま
//   LDR へ滑らかに圧縮する変換で、ハイライトのディテールを残す。
//
//       出力(LDR)
//   1.0 ┤            ___________  ← クランプ: 1.0 で急に頭打ち(潰れる)
//       │          /
//       │        /  ___............  ← トーンマップ: 滑らかに 1.0 へ漸近(階調が残る)
//       │      / /
//   0.0 +----------------------- 入力(HDR)
//       0.0   1.0    2.0    4.0  ...→∞
//
// 演算子の違い:
//   None       : クランプのみ。従来通りでハイライトが潰れる。
//   Reinhard   : c/(1+c)。最も単純。全体的に淡くなりがち。
//   ReinhardExt: ホワイトポイント付き。指定輝度以上を白に。Reinhard より締まる。
//   ACES       : 映画業界標準の近似カーブ。暗部が締まり高輝度が滑らか。コントラスト高め(既定)。
//   Uncharted2 : フィルミックな S 字カーブ(Hable)。ゲーム向けの定番。柔らかい階調。

// ---- Reinhard: 最も単純。c / (1+c) で漸近的に 1.0 へ圧縮 ----
// 全体的に淡く(コントラスト低め)なりがち。
float3 Tonemap_Reinhard(float3 c)
{
    return c / (1.0 + c);
}

// ---- Reinhard Extended: ホワイトポイント white 以上を純白に飛ばす ----
// white を小さくするとコントラストが上がり、大きくすると階調が伸びる。
float3 Tonemap_ReinhardExtended(float3 c, float white)
{
    float w2 = max(white * white, 1e-4);
    return (c * (1.0 + c / w2)) / (1.0 + c);
}

// ---- ACES Filmic (Narkowicz 2015 の近似) ----
// (x(2.51x+0.03)) / (x(2.43x+0.59)+0.14) という有理関数。
// 暗部を少し持ち上げ(toe)、明部を滑らかに丸める(shoulder) S 字を作る。
// これが「フィルミック」と呼ばれ、写真・映画的な見栄えになる。コントラスト高め。
float3 Tonemap_ACES(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ---- Uncharted2 (Hable) Filmic ----
float3 Uncharted2TonemapPartial(float3 x)
{
    const float A = 0.15; // Shoulder Strength
    const float B = 0.50; // Linear Strength
    const float C = 0.10; // Linear Angle
    const float D = 0.20; // Toe Strength
    const float E = 0.02; // Toe Numerator
    const float F = 0.30; // Toe Denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Tonemap_Uncharted2(float3 c)
{
    const float W = 11.2; // Linear White Point Value
    float3 curr  = Uncharted2TonemapPartial(c * 2.0);
    float3 whiteScale = 1.0 / Uncharted2TonemapPartial(W.xxx);
    return curr * whiteScale;
}

// mode に応じてトーンマップを適用するディスパッチャ。
//   mode: 0=None(クランプ) 1=Reinhard 2=ReinhardExt 3=ACES 4=Uncharted2
float3 ApplyTonemap(float3 color, uint mode, float exposure, float white)
{
    color *= exposure;

    float3 mapped;
    if (mode == 1)      mapped = Tonemap_Reinhard(color);
    else if (mode == 2) mapped = Tonemap_ReinhardExtended(color, white);
    else if (mode == 3) mapped = Tonemap_ACES(color);
    else if (mode == 4) mapped = Tonemap_Uncharted2(color);
    else                mapped = color; // None: 後段の saturate でクランプ

    return mapped;
}

// リニア → sRGB(ガンマ) エンコード。ガンマ空間で動くパイプラインでは不要 (既定 off)。
float3 LinearToGamma(float3 c)
{
    return pow(max(c, 0.0), 1.0 / 2.2);
}
