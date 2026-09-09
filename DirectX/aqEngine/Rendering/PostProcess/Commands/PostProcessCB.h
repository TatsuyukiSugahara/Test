#pragma once
#include <cstdint>
#include "Math/Matrix.h"


namespace aq
{
	namespace rendering
	{
		// BloomComposite.fx / BloomBrightExtract.fx の cbuffer レイアウトと一致させること。
		// 先頭 5 フィールド (offset 0..16) は輝度抽出が読み、トーンマップ用フィールドは
		// 合成のみが読む。1 つのレイアウトを Bloom / Tonemap の両パスで共有する。
		struct BloomCBData
		{
			float    threshold;
			float    intensity;
			uint32_t width;
			uint32_t height;
			uint32_t isVertical;
			float    exposure;     // 露出倍率 (トーンマップ前に乗算)
			uint32_t tonemapMode;  // 0=None 1=Reinhard 2=ReinhardExt 3=ACES 4=Uncharted2
			float    whitePoint;   // ReinhardExt 用
			uint32_t applyGamma;   // 1 なら sRGB エンコード
			uint32_t pad[3];
		};




		// MotionBlur.fx の cbuffer レイアウトと一致させること。
		struct MotionBlurCBData
		{
			math::Matrix4x4 prevViewProj;   // 前フレームの view * projection (列ベクトル規約)
			float           screenWidth;
			float           screenHeight;
			float           strength;       // ブラー長スケール
			float           padding;
		};
		static_assert(sizeof(MotionBlurCBData) == 80, "MotionBlurCBData must be 80 bytes (16B aligned)");
	}
}
