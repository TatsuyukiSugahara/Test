#pragma once
#include "IUIComponent.h"
#include "UI/UITypes.h"
#include "Math/Vector.h"

namespace aq
{
	namespace ui
	{
		class UITransformComponent : public IUIComponent
		{
		public:
			// ---- 設定値 (毎フレーム変更可) ----

			math::Vector3 localPosition = { 0.f, 0.f, 0.f }; // z = depth sort
			math::Vector2 localScale    = { 1.f, 1.f };
			float         rotation      = 0.f;                 // degree, Z 軸回転
			math::Vector2 sizeDelta     = { 100.f, 100.f };   // ローカルサイズ (px)
			UIAnchor      anchor;
			UIPivot       pivot;
			bool          active        = true;

			// ---- 計算済みスクリーン座標 (UIContext::Update() で更新) ----

			RectF worldRect;  // スクリーンピクセル座標 (HitTest・描画に使用)
		};

	} // namespace ui
} // namespace aq
