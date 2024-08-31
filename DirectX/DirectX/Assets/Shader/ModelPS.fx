#include "Common.fx"

/**
 * ピクセルシェーダーのエントリ関数
 */
float4 PSMain(PSInput input) : SV_TARGET
{
	return input.color;
}

