// 5-tap 分離可能ガウスブラー。g_IsVertical で水平/垂直を切り替える。
Texture2D<float4>   g_Input  : register(t0);
RWTexture2D<float4> g_Output : register(u0);

cbuffer BloomCB : register(b0)
{
    float g_Threshold;
    float g_Intensity;
    uint  g_Width;
    uint  g_Height;
    uint  g_IsVertical;
    uint3 g_Pad;
};

static const float kWeights[5] = { 0.0625, 0.25, 0.375, 0.25, 0.0625 };

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Width || id.y >= g_Height) return;

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    for (int i = -2; i <= 2; ++i)
    {
        int2 offset = g_IsVertical ? int2(0, i) : int2(i, 0);
        int2 coord  = clamp(int2(id.xy) + offset, int2(0, 0), int2(g_Width - 1, g_Height - 1));
        result += g_Input[coord] * kWeights[i + 2];
    }
    g_Output[id.xy] = result;
}
