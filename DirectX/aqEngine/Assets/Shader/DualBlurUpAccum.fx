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

    float4 o;
    o  = g_Input.SampleLevel(g_Sampler, uv + float2(-texelSize.x,         0.0f), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2( texelSize.x,         0.0f), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2(        0.0f, -texelSize.y), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2(        0.0f,  texelSize.y), 0);
    o += g_Input.SampleLevel(g_Sampler, uv + float2(-texelSize.x, -texelSize.y) * 0.5f, 0) * 2.0f;
    o += g_Input.SampleLevel(g_Sampler, uv + float2(-texelSize.x,  texelSize.y) * 0.5f, 0) * 2.0f;
    o += g_Input.SampleLevel(g_Sampler, uv + float2( texelSize.x, -texelSize.y) * 0.5f, 0) * 2.0f;
    o += g_Input.SampleLevel(g_Sampler, uv + float2( texelSize.x,  texelSize.y) * 0.5f, 0) * 2.0f;

    g_Output[id.xy] = g_Output[id.xy] + o / 12.0f;
}
