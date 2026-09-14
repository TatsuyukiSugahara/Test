#define MAX_SHADOW_CASCADES 4

cbuffer ShadowCB : register(b3)
{
    float4x4 lightViewProj[MAX_SHADOW_CASCADES];
    float4   cascadeSplits;
    uint     cascadeCount;
    float    depthBias;
    float    softness;
    uint     shadowPad;
};
