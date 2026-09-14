// Hi-Z ピラミッド ダウンサンプル (max-reduction)
// 親レベルの 2x2 の最大深度 (最遠) を子レベルへ出力する。

Texture2D<float>   g_Input  : register(t0);  // 親レベル (R32_Float)
RWTexture2D<float> g_Output : register(u0);  // 子レベル  (R32_Float)

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint outW, outH;
    g_Output.GetDimensions(outW, outH);
    if (id.x >= outW || id.y >= outH) return;

    uint inW, inH;
    g_Input.GetDimensions(inW, inH);
    int2 mx = int2(inW, inH) - int2(1, 1);
    int2 s  = int2(id.xy) * 2;

    float d0 = g_Input.Load(int3(min(s,              mx), 0));
    float d1 = g_Input.Load(int3(min(s + int2(1, 0), mx), 0));
    float d2 = g_Input.Load(int3(min(s + int2(0, 1), mx), 0));
    float d3 = g_Input.Load(int3(min(s + int2(1, 1), mx), 0));

    g_Output[id.xy] = max(max(d0, d1), max(d2, d3));
}
