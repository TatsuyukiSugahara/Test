#include "aq.h"
#include "TextLayout.h"
#include "FontAsset.h"
#include <cmath>

namespace aq
{
	namespace ui
	{
		// =========================================================================
		// UTF-8 デコード
		// =========================================================================

		std::vector<char32_t> TextLayout::DecodeUTF8(const std::string& utf8)
		{
			std::vector<char32_t> out;
			out.reserve(utf8.size());
			size_t i = 0;
			while (i < utf8.size())
			{
				const uint8_t c = static_cast<uint8_t>(utf8[i]);
				char32_t cp;
				if (c < 0x80)          { cp = c;                                                          i += 1; }
				else if (c < 0xE0)     { cp = ((c & 0x1F) << 6)  | (utf8[i+1] & 0x3F);                  i += 2; }
				else if (c < 0xF0)     { cp = ((c & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6)
				                              | (utf8[i+2] & 0x3F);                                       i += 3; }
				else                   { cp = ((c & 0x07) << 18) | ((utf8[i+1] & 0x3F) << 12)
				                              | ((utf8[i+2] & 0x3F) << 6) | (utf8[i+3] & 0x3F);          i += 4; }
				out.push_back(cp);
			}
			return out;
		}


		// =========================================================================
		// 1 行レイアウト
		// =========================================================================

		void TextLayout::LayoutLine(
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
			float&                        outLineWidth)
		{
			// em → px スケール
			const float scale = fontSize;

			float xCursor = xStart;
			for (size_t k = begin; k < end; ++k)
			{
				const char32_t cp = codepoints[k];
				const GlyphInfo* g = font.GetGlyph(cp);
				if (!g) { xCursor += fontSize * 0.5f; continue; }  // missing glyph

				// 描画矩形 (planeTop/Bottom は baseline からの em 距離; Y = 下方向が正)
				const float gx = xCursor + g->planeLeft   * scale;
				const float gw = (g->planeRight - g->planeLeft)   * scale;
				const float gh = (g->planeTop   - g->planeBottom) * scale;
				const float gy = lineBaselineY  - g->planeTop * scale;

				if (gw > 0.f && gh > 0.f)
				{
					// atlas UV (D3D のテクスチャ座標は Y 上向き)
					const float uvX = g->uvLeft;
					const float uvY = 1.f - g->uvTop;     // D3D は V=0 が上
					const float uvW = g->uvRight  - g->uvLeft;
					const float uvH = g->uvTop    - g->uvBottom;

					// グラデーション: 行内の上下位置で top/bottom 色を補間
					const float t0 = std::max(0.f, std::min(1.f, (gy        - lineTop) / lineH));
					const float t1 = std::max(0.f, std::min(1.f, (gy + gh  - lineTop) / lineH));
					const auto lerp4 = [](const math::Vector4& a, const math::Vector4& b, float t)
					{
						return math::Vector4{ a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t };
					};

					GlyphQuad q;
					q.x  = gx;  q.y  = gy;
					q.w  = gw;  q.h  = gh;
					q.uvX = uvX; q.uvY = uvY;
					q.uvW = uvW; q.uvH = uvH;
					q.colorTop    = lerp4(colorTop, colorBottom, t0);
					q.colorBottom = lerp4(colorTop, colorBottom, t1);
					outQuads.push_back(q);
				}

				xCursor += g->advance * scale;
			}
			outLineWidth = xCursor - xStart;
		}


		// =========================================================================
		// Layout (メインエントリ)
		// =========================================================================

		TextLayoutResult TextLayout::Layout(
			const std::string&      text,
			const TextLayoutParams& p,
			float                   originX,
			float                   originY)
		{
			TextLayoutResult result;
			if (!p.font || !p.font->IsLoaded() || text.empty()) return result;

			const FontAsset& font = *p.font;
			const float scale     = p.fontSize;
			const float lineH     = font.GetLineHeight()  * scale;
			const float ascender  = font.GetAscender()    * scale;

			std::vector<char32_t> cps = DecodeUTF8(text);
			if (p.visibleCharCount >= 0 && static_cast<size_t>(p.visibleCharCount) < cps.size())
				cps.resize(static_cast<size_t>(p.visibleCharCount));

			// ---- 行分割 --------------------------------------------------------
			// 改行文字 '\n' と wordWrap による折り返しを処理し、行の [begin, end) を収集。
			struct LineSpan { size_t begin, end; };
			std::vector<LineSpan> lines;

			size_t lineBegin = 0;
			for (size_t i = 0; i <= cps.size(); ++i)
			{
				const bool isEnd = (i == cps.size());
				const bool isNL  = !isEnd && cps[i] == U'\n';

				if (p.wordWrap && p.boxW > 0.f && !isEnd && !isNL)
				{
					// 現在位置までの幅を計算
					float w = 0.f;
					for (size_t k = lineBegin; k <= i; ++k)
					{
						const GlyphInfo* g = font.GetGlyph(cps[k]);
						w += g ? g->advance * scale : scale * 0.5f;
					}
					if (w > p.boxW)
					{
						// 直前のスペースで折り返し
						size_t wrap = i;
						for (size_t k = i; k > lineBegin; --k)
						{
							if (cps[k] == U' ' || cps[k] == U'　') { wrap = k + 1; break; }
						}
						lines.push_back({ lineBegin, wrap });
						lineBegin = wrap;
						continue;
					}
				}

				if (isNL || isEnd)
				{
					lines.push_back({ lineBegin, i });
					lineBegin = i + 1;
				}
			}

			if (lines.empty()) return result;

			// ---- 各行の幅を計算 ------------------------------------------------
			std::vector<float> lineWidths(lines.size(), 0.f);
			for (size_t li = 0; li < lines.size(); ++li)
			{
				float lw;
				std::vector<GlyphQuad> dummy;
				LayoutLine(cps, lines[li].begin, lines[li].end, font,
					scale, 0.f, 0.f, lineH, 0.f,
					p.colorTop, p.colorBottom, dummy, lw);
				lineWidths[li] = lw;
				result.totalWidth = std::max(result.totalWidth, lw);
			}
			result.totalHeight = lineH * static_cast<float>(lines.size());

			// ---- 垂直整列オフセット --------------------------------------------
			float yOffset = originY;
			if (p.boxH > 0.f)
			{
				switch (p.alignV)
				{
					case TextAlignV::Middle:
						yOffset = originY + (p.boxH - result.totalHeight) * 0.5f;
						break;
					case TextAlignV::Bottom:
						yOffset = originY + p.boxH - result.totalHeight;
						break;
					default: break;
				}
			}

			// ---- 各行のクワッドを生成 ------------------------------------------
			for (size_t li = 0; li < lines.size(); ++li)
			{
				float xStart = originX;
				if (p.boxW > 0.f)
				{
					switch (p.alignH)
					{
						case TextAlignH::Center:
							xStart = originX + (p.boxW - lineWidths[li]) * 0.5f;
							break;
						case TextAlignH::Right:
							xStart = originX + p.boxW - lineWidths[li];
							break;
						default: break;
					}
				}

				const float lineTop      = yOffset + static_cast<float>(li) * lineH;
				const float lineBaseline = lineTop + ascender;
				float dummy;
				LayoutLine(cps, lines[li].begin, lines[li].end, font,
					scale, lineTop, lineBaseline, lineH,
					xStart, p.colorTop, p.colorBottom,
					result.quads, dummy);
			}

			return result;
		}

	} // namespace ui
} // namespace aq
