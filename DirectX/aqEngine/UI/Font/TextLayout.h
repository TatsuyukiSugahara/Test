#pragma once
#include <string>
#include <vector>
#include "Math/Vector.h"
#include "UI/UITypes.h"

namespace aq
{
	namespace ui
	{
		class  FontAsset;
		struct TextStyle;

		// レイアウト後のグリフ 1 文字分の描画情報
		struct GlyphQuad
		{
			float x, y, w, h;          // スクリーン矩形 (canvas px)
			float uvX, uvY, uvW, uvH;  // atlas UV (0-1, Y は上が小さい)
			math::Vector4 colorTop;    // グラデーション上色 (fill)
			math::Vector4 colorBottom; // グラデーション下色 (fill)
		};

		struct TextLayoutParams
		{
			const FontAsset*  font       = nullptr;
			float             fontSize   = 24.f;     // 実際の描画サイズ (px)
			math::Vector4     colorTop    = { 1,1,1,1 };
			math::Vector4     colorBottom = { 1,1,1,1 };
			float             boxW       = 0.f;      // 折り返し幅 (0=折り返しなし)
			float             boxH       = 0.f;      // 縦クリップ幅 (0=なし)
			TextAlignH        alignH     = TextAlignH::Left;
			TextAlignV        alignV     = TextAlignV::Top;
			bool              wordWrap   = false;
		};

		struct TextLayoutResult
		{
			std::vector<GlyphQuad> quads;
			float totalWidth  = 0.f;
			float totalHeight = 0.f;
		};

		class TextLayout
		{
		public:
			// text: UTF-8 文字列。originX/Y はテキスト領域の左上 (canvas px)。
			static TextLayoutResult Layout(
				const std::string&      text,
				const TextLayoutParams& params,
				float                   originX,
				float                   originY);

		private:
			static std::vector<char32_t> DecodeUTF8(const std::string& utf8);

			// 1 行分の文字をレイアウトして quads に追加
			// xCursor: 行の開始 X 座標 (canvas px)
			// lineTop: ベースライン Y - ascender (canvas px)
			// lineBaselineY: ベースライン Y (canvas px)
			static void LayoutLine(
				const std::vector<char32_t>& codepoints,
				size_t                        begin,
				size_t                        end,
				const FontAsset&              font,
				float                         fontSize,
				float                         lineTop,
				float                         lineBaselineY,
				float                         lineH,
				float                         xStart,
				const math::Vector4&          colorTop,
				const math::Vector4&          colorBottom,
				std::vector<GlyphQuad>&       outQuads,
				float&                        outLineWidth);
		};

	} // namespace ui
} // namespace aq
