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
		class UIImageComponent : public IUIComponent
		{
		public:
			std::shared_ptr<graphics::IShaderResourceView> texture;

			math::Vector4 color      = { 1.f, 1.f, 1.f, 1.f }; // RGBA tint (アニメ可)
			RectF         uvRect     = { 0.f, 0.f, 1.f, 1.f }; // UV 矩形 (0-1)
			float         fillAmount = 1.0f;                     // 0-1 フィル量 (アニメ可)
			FillDirection fillDir    = FillDirection::Right;
			bool          flipH      = false;
			bool          flipV      = false;
		};

	} // namespace ui
} // namespace aq
