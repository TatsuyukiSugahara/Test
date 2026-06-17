#include "ShadowCB.h"

cbuffer PerDrawCB : register(b0)
{
    float4x4 world;
    float4x4 view;        // unused in depth pass
    float4x4 projection;  // unused in depth pass
};

float4 VSMain(float3 position : POSITION) : SV_POSITION
{
    float4 worldPos = mul(world, float4(position, 1.0));
    return mul(lightViewProj[0], worldPos);
}
