#pragma once
#include <cstdint>
#include "Math/Vector.h"

namespace aq
{
	namespace ui
	{
		// ---- 矩形 (スクリーン座標、ピクセル単位) ----

		struct RectF
		{
			float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

			bool Contains(float px, float py) const
			{
				return px >= x && px < x + w && py >= y && py < y + h;
			}
			bool Contains(math::Vector2 p) const { return Contains(p.x, p.y); }
		};


		// ---- Anchor (親に対する基点, 0-1 正規化) ----

		struct UIAnchor
		{
			math::Vector2 min = { 0.5f, 0.5f }; // (0,0)=左上  (1,1)=右下
			math::Vector2 max = { 0.5f, 0.5f }; // min==max=点アンカー / min!=max=ストレッチ
		};


		// ---- Pivot (自身の回転・スケール原点, 0-1 正規化) ----

		struct UIPivot
		{
			math::Vector2 pivot = { 0.5f, 0.5f }; // (0.5,0.5)=中心
		};


		// ---- NineSlice 境界 (テクセル単位) ----

		struct NineSliceBorder
		{
			float left   = 0.f;
			float right  = 0.f;
			float top    = 0.f;
			float bottom = 0.f;
		};


		// ---- フィル方向 ----

		enum class FillDirection : uint8_t { Right, Left, Up, Down };


		// ---- イージング種別 ----

		enum class EaseType : uint8_t
		{
			Linear,
			EaseIn,
			EaseOut,
			EaseInOut,
			Bezier,
		};


		// ---- テキスト整列 ----

		enum class TextAlignH : uint8_t { Left, Center, Right };
		enum class TextAlignV : uint8_t { Top, Middle, Bottom };


		// ---- UI シェーダー種別 (バッチグループの区切り) ----

		enum class UIShaderType : uint8_t
		{
			Standard,     // 通常スプライト / NineSlice
			CircleGauge,  // 円形ゲージ専用 PS
			SdfText,      // MSDF フォントレンダリング
		};


		// ---- SdfText シェーダー用 CB パラメータ ----

		struct SdfTextParams
		{
			math::Vector4 outlineColor    = { 0.f, 0.f, 0.f, 0.f };
			math::Vector4 shadowColor     = { 0.f, 0.f, 0.f, 0.f };
			math::Vector2 shadowOffsetUV  = { 0.f, 0.f };
			float         shadowSoftness  = 0.05f;
			float         outlineWidth    = 0.f;
			float         smoothing       = 0.08f;
		};


		// ---- UIObject ID / Handle ----

		using UIObjectID = uint32_t;
		static constexpr UIObjectID INVALID_UI_OBJECT_ID = 0u;

		struct UIObjectHandle
		{
			UIObjectID id         = INVALID_UI_OBJECT_ID;
			uint32_t   generation = 0u;

			bool operator==(const UIObjectHandle& o) const
			{
				return id == o.id && generation == o.generation;
			}
			bool operator!=(const UIObjectHandle& o) const { return !(*this == o); }

			bool IsValid() const { return id != INVALID_UI_OBJECT_ID; }
			static UIObjectHandle Invalid() { return {}; }
		};

	} // namespace ui
} // namespace aq
