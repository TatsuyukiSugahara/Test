#pragma once
#include <string>
#include "IUIComponent.h"
#include "UI/UITypes.h"
#include "Math/Vector.h"

namespace aq
{
	namespace ui
	{

		// テキスト表示コンポーネント。MSDF SDF レンダリングで描画される。
		//
		// テキスト表示スタイル (フォント・エフェクト等) は textStylePath で指定した
		// TextStyle アセットで管理する。コンポーネントはインスタンス固有の
		// 位置・スケール・カラーオーバーライドとレイアウト設定のみを持つ。
		//
		// オーバーライド優先順位: コンポーネント値 > TextStyle
		//   fontSize == 0.f  → TextStyle の値を使用
		//   color.a  == 0.f  → TextStyle の fillColor を使用
		class UITextComponent : public IUIComponent
		{
		public:
			std::string   content;                               // 表示文字列
			std::string   textStylePath;                         // TextStyle アセットパス (.textstyle.json)

			// --- インスタンスオーバーライド ---
			float         fontSize  = 0.f;                       // 0 = TextStyle から取得
			float         scale     = 1.f;                       // fontSize へのスケール倍率
			math::Vector4 color     = { 0.f, 0.f, 0.f, 0.f };  // a=0 = TextStyle fillColor を使用
			math::Vector2 offset    = { 0.f, 0.f };             // Transform 矩形内の追加オフセット (px)

			// --- レイアウト ---
			TextAlignH    alignH         = TextAlignH::Center;
			TextAlignV    alignV         = TextAlignV::Middle;
			bool          wordWrap       = false;

			// --- タイプライター ---
			int           visibleCharCount = -1;  // -1 = 全文字表示
		};

	} // namespace ui
} // namespace aq
