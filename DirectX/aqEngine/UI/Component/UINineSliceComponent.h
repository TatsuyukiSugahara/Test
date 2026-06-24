#pragma once
#include <memory>
#include "IUIComponent.h"
#include "UI/UITypes.h"
#include "Math/Vector.h"

namespace aq
{
	namespace graphics { class IShaderResourceView; }

	namespace ui
	{
		class UINineSliceComponent : public IUIComponent
		{
		public:
			std::shared_ptr<graphics::IShaderResourceView> texture;

			NineSliceBorder border;                                // テクセル単位 (アニメ可)
			math::Vector2   textureSize = { 64.f, 64.f };         // テクスチャのピクセルサイズ (border UV 正規化に使用)
			math::Vector4   color       = { 1.f, 1.f, 1.f, 1.f }; // RGBA (アニメ可)
			float           fillAmount  = 1.f;                     // 右端クリップ量 (アニメ可)
			FillDirection   fillDir     = FillDirection::Right;
		};

	} // namespace ui
} // namespace aq
