#pragma once
#include "IUIComponent.h"
#include "Math/Vector.h"
#include <memory>

namespace aq
{
	namespace graphics { class IShaderResourceView; }

	namespace ui
	{
		// 円形ゲージコンポーネント。
		// atan2 ベースのピクセルシェーダー (UICircleGauge.fx) で fillAmount 分だけ扇状に描画する。
		class UICircleGaugeComponent : public IUIComponent
		{
		public:
			std::shared_ptr<graphics::IShaderResourceView> texture;
			math::Vector4 color      = { 1.f, 1.f, 1.f, 1.f };
			float         fillAmount = 1.f;   // 0=空, 1=満タン
			float         startAngle = 0.f;   // ラジアン (0=上)
			float         clockwise  = 1.f;   // 1=時計回り, -1=反時計回り
		};

	} // namespace ui
} // namespace aq
