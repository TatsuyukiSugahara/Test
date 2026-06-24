#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "Math/Vector.h"
#include "Graphics/IShaderResourceView.h"

namespace aq
{
	namespace ui
	{
		// msdf-atlas-gen が出力するグリフ 1 文字分のメトリクス
		struct GlyphInfo
		{
			float uvLeft   = 0.f;   // atlas UV 座標 (0-1)
			float uvBottom = 0.f;
			float uvRight  = 0.f;
			float uvTop    = 0.f;

			float planeLeft   = 0.f;  // em 単位のクワッド境界
			float planeBottom = 0.f;
			float planeRight  = 0.f;
			float planeTop    = 0.f;

			float advance = 0.f;  // em 単位の送り幅
		};

		// フォントアトラス + グリフ DB。
		// msdf-atlas-gen が生成した atlas.json / atlas.png のペアから読み込む。
		class FontAsset
		{
		public:
			// atlas.json のパスを受け取り、同ディレクトリの PNG テクスチャも読み込む。
			bool LoadFromJson(const char* jsonPath);
			bool IsLoaded() const { return loaded_; }

			const GlyphInfo* GetGlyph(char32_t cp) const;

			float GetLineHeight()  const { return lineHeight_; }
			float GetAscender()    const { return ascender_; }
			float GetDescender()   const { return descender_; }
			float GetAtlasWidth()  const { return atlasW_; }
			float GetAtlasHeight() const { return atlasH_; }
			float GetBaseSize()    const { return baseSize_; }  // atlas px / em
			float GetPxRange()     const { return pxRange_; }   // SDF 距離範囲 (atlas px)

			std::shared_ptr<graphics::IShaderResourceView> GetAtlasSRV() const { return atlasSrv_; }

		private:
			std::unordered_map<char32_t, GlyphInfo> glyphs_;
			std::shared_ptr<graphics::IShaderResourceView> atlasSrv_;
			float lineHeight_ = 1.2f;
			float ascender_   = 0.8f;
			float descender_  = -0.2f;
			float atlasW_     = 1024.f;
			float atlasH_     = 1024.f;
			float baseSize_   = 48.f;
			float pxRange_    = 4.f;
			bool  loaded_     = false;
		};

	} // namespace ui
} // namespace aq
