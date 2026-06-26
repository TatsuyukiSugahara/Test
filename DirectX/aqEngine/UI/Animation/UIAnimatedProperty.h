#pragma once
#include <cstdint>

namespace aq
{
	namespace ui
	{
		// アニメーション可能なプロパティの種別。
		// UIAnimationTrack が操作対象を識別するために使用する。
		enum class UIAnimatedProperty : uint8_t
		{
			// UITransformComponent
			PositionX,
			PositionY,
			PositionZ,
			ScaleX,
			ScaleY,
			Rotation,
			SizeDeltaX,
			SizeDeltaY,

			// UIImageComponent / UINineSliceComponent / UICircleGaugeComponent (共通色)
			ColorR,
			ColorG,
			ColorB,
			ColorA,

			// UIImageComponent / UICircleGaugeComponent / UINineSliceComponent
			FillAmount,

			// UITransformComponent::active
			Active,   // 0.0 = false, > 0.5 = true

			// UINineSliceComponent::border
			NineSliceBorderLeft,
			NineSliceBorderRight,
			NineSliceBorderTop,
			NineSliceBorderBottom,

			// UITextComponent
			TextCharCount,  // 表示文字数 (タイプライター演出用); -1 = 全表示
		};

	} // namespace ui
} // namespace aq
