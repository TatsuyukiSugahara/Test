#include "MaterialDef.h"

#define MAT_HAS_NORMAL   (1u << 0)
#define MAT_HAS_SPECULAR (1u << 1)
#define MAT_HAS_EMISSIVE (1u << 2)

cbuffer MaterialCB : register(b2)
{
    float  specularIntensity;
    float  gloss;
    float  emissiveScale;
    uint   materialFlags;
    float4 params[MATERIAL_PARAMETER_NUM];
};

bool HasNormalMap()   { return (materialFlags & MAT_HAS_NORMAL)   != 0; }
bool HasSpecularMap() { return (materialFlags & MAT_HAS_SPECULAR) != 0; }
bool HasEmissiveMap() { return (materialFlags & MAT_HAS_EMISSIVE) != 0; }
