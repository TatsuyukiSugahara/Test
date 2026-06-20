Texture2D<float4>   g_Input   : register(t0);
RWTexture2D<float4> g_Output  : register(u0);
SamplerState        g_Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint outW, outH;
    g_Output.GetDimensions(outW, outH);
    if (id.x >= outW || id.y >= outH) return;

    uint inW, inH, inMips;
    g_Input.GetDimensions(0, inW, inH, inMips);

    float2 uv        = (float2(id.xy) + 0.5f) / float2(outW, outH);
    float2 texelSize = 1.0f / float2(inW, inH);

    float4 o = g_Input.SampleLevel(g_Sampler, uv, 0) * 4.0f;
    o += g_Input.SampleLevel(g_Sampler, uv + float2(-texelSize.x, -texelSize.y), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2(-texelSize.x,  texelSize.y), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2( texelSize.x, -texelSize.y), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2( texelSize.x,  texelSize.y), 0);
    g_Output[id.xy] = o / 8.0f;
}
