#include "ShadowCB.h"

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;        // unused in depth pass
    float4x4 projection;  // unused in depth pass
};

cbuffer ShadowLightCB : register(b2)
{
    uint lightSlice;
    uint _slicePad0;
    uint _slicePad1;
    uint _slicePad2;
};

float4 VSMain(float3 position : POSITION) : SV_POSITION
{
    float4 worldPos = mul(world, float4(position, 1.0));
    float4x4 lvp;
    if      (lightSlice == 1) lvp = lightViewProj[1];
    else if (lightSlice == 2) lvp = lightViewProj[2];
    else if (lightSlice == 3) lvp = lightViewProj[3];
    else                      lvp = lightViewProj[0];
    return mul(lvp, worldPos);
}
