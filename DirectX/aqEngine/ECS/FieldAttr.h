#pragma once
#include <cstdint>


namespace aq
{
	namespace ecs
	{
		/**
		 * パスのフィールドが指すアセットの種類
		 *
		 * FieldPath の属性に付け、コンテンツブラウザのドロップ先の判定に使う(設計 6.4 節 1)。
		 */
		enum class AssetKind : uint8_t
		{
			None,
			Model,
			Texture,
			Prefab,
			Level,
			Particle,
			Audio,
		};




		/**
		 * フィールド属性
		 *
		 * Reflect の Field / FieldPath に末尾の省略可能な引数として渡す小さな値型。
		 * ランタイムでもコンパイルする(ifdef で消さない)。JSON の Visitor は無視し、
		 * ImGui の Visitor だけが効かせる。最小・最大は UI だけで効かせ、読み込んだ値は丸めない。
		 *
		 * 例: FieldAttr{}.Min(0.001f).Tooltip("0以下にしない")
		 */
		struct FieldAttr
		{
			/** 範囲(hasMin / hasMax のときだけ有効) */
			float       min      = 0.0f;
			float       max      = 0.0f;
			/** ドラッグの速さ。0 なら型ごとの既定 */
			float       speed    = 0.0f;
			/** ホバーで出す説明 */
			const char* tooltip  = nullptr;
			/** パスが指すアセットの種類 */
			AssetKind   asset    = AssetKind::None;
			bool        hasMin   = false;
			bool        hasMax   = false;
			/** 表示するが編集できない。保存はする */
			bool        readOnly = false;
			/** インスペクターに出さない。保存はする */
			bool        hidden   = false;


		public:
			/** 以下、自分の写しを返すビルダー */
			constexpr FieldAttr Min(const float v) const
			{
				FieldAttr attr = *this;
				attr.min    = v;
				attr.hasMin = true;
				return attr;
			}

			constexpr FieldAttr Max(const float v) const
			{
				FieldAttr attr = *this;
				attr.max    = v;
				attr.hasMax = true;
				return attr;
			}

			constexpr FieldAttr Range(const float lo, const float hi) const
			{
				return Min(lo).Max(hi);
			}

			constexpr FieldAttr Speed(const float v) const
			{
				FieldAttr attr = *this;
				attr.speed = v;
				return attr;
			}

			constexpr FieldAttr Tooltip(const char* text) const
			{
				FieldAttr attr = *this;
				attr.tooltip = text;
				return attr;
			}

			constexpr FieldAttr Asset(const AssetKind kind) const
			{
				FieldAttr attr = *this;
				attr.asset = kind;
				return attr;
			}

			constexpr FieldAttr ReadOnly() const
			{
				FieldAttr attr = *this;
				attr.readOnly = true;
				return attr;
			}

			constexpr FieldAttr Hidden() const
			{
				FieldAttr attr = *this;
				attr.hidden = true;
				return attr;
			}
		};
	}
}
