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
    float3 _pad0;
    float4 _extra[7];        // Phase 2: subsurface, anisotropic, sheen, clearcoat...
};
// sizeof = 16 + 16 + 7*16 = 144 bytes（MaterialCBData と同サイズ）

bool PBR_HasNormalMap()         { return (pbrMaterialFlags & PBR_HAS_NORMAL)             != 0; }
bool PBR_HasMetallicRoughness() { return (pbrMaterialFlags & PBR_HAS_METALLIC_ROUGHNESS)  != 0; }
bool PBR_HasEmissiveMap()       { return (pbrMaterialFlags & PBR_HAS_EMISSIVE)            != 0; }
bool PBR_ReceivesShadow()       { return (pbrMaterialFlags & PBR_RECEIVE_SHADOW)          != 0; }
bool PBR_HasSplatMap()          { return (pbrMaterialFlags & PBR_HAS_SPLATMAP)            != 0; }
