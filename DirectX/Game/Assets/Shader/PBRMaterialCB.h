// PBR 専用マテリアル CB。MaterialCB.h と排他（同一 register b2 を使用）。
// forward シェーダーは MaterialCB.h を include すること。
#include "MaterialDef.h"

// PBR フラグ（MaterialFlags と同じビット位置を保持する）
#define PBR_HAS_NORMAL             (1u << 0)
#define PBR_HAS_METALLIC_ROUGHNESS (1u << 1)  // t2: R=metallic, G=roughness
#define PBR_HAS_EMISSIVE           (1u << 2)
#define PBR_RECEIVE_SHADOW         (1u << 3)
#define PBR_HAS_SPLATMAP           (1u << 4)  // Terrain 専用

cbuffer PBRMaterialCB : register(b2)
{
    float  metallic;         // [0,1]: 0=誘電体, 1=金属
    float  roughness;        // [0,1]: 0=鏡面, 1=粗面（旧 gloss とは逆）
    float  emissiveScale;
    uint   pbrMaterialFlags;
    float  specular;         // 誘電体 F0 スケール（0.5 → F0=0.04）
    float  dither;           // [0,1]: 1=不透明, 0=透明（スクリーンスペース ディザで擬似半透明）
    float2 _pad0;
    float4 _extra[7];        // Phase 2: subsurface, anisotropic, sheen, clearcoat...
};
// sizeof = 16 + 16 + 7*16 = 144 bytes（MaterialCBData と同サイズ）

bool PBR_HasNormalMap()         { return (pbrMaterialFlags & PBR_HAS_NORMAL)             != 0; }
bool PBR_HasMetallicRoughness() { return (pbrMaterialFlags & PBR_HAS_METALLIC_ROUGHNESS)  != 0; }
bool PBR_HasEmissiveMap()       { return (pbrMaterialFlags & PBR_HAS_EMISSIVE)            != 0; }
bool PBR_ReceivesShadow()       { return (pbrMaterialFlags & PBR_RECEIVE_SHADOW)          != 0; }
bool PBR_HasSplatMap()          { return (pbrMaterialFlags & PBR_HAS_SPLATMAP)            != 0; }

// ディファードでは半透明を直接書けないため、dither(=不透明度) に応じてピクセルを
// 確率的に discard し擬似半透明を表現する（4x4 Bayer スクリーンスペース ディザ）。
// svPos には SV_POSITION.xy（ピクセル座標）を渡すこと。
// dither>=1.0 で全ピクセル残し、dither<=0.0 で全ピクセル破棄。
void PBR_DitherClip(float2 svPos)
{
    if (dither >= 1.0)
        return;

    const float thresholds[16] = {
         0.5 / 16.0,  8.5 / 16.0,  2.5 / 16.0, 10.5 / 16.0,
        12.5 / 16.0,  4.5 / 16.0, 14.5 / 16.0,  6.5 / 16.0,
         3.5 / 16.0, 11.5 / 16.0,  1.5 / 16.0,  9.5 / 16.0,
        15.5 / 16.0,  7.5 / 16.0, 13.5 / 16.0,  5.5 / 16.0
    };

    uint ix = ((uint)svPos.x) & 3u;
    uint iy = ((uint)svPos.y) & 3u;
    clip(dither - thresholds[iy * 4u + ix]);
}
