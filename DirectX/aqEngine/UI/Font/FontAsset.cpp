#include "aq.h"
#include "FontAsset.h"
#include "Util/SimpleJson.h"
#include "Resource/Resource.h"
#include <string>
#include <algorithm>

namespace aq
{
	namespace ui
	{
		namespace
		{
			// atlas.json があるディレクトリを取得
			std::string DirOf(const std::string& path)
			{
				const size_t sep = path.find_last_of("/\\");
				return (sep != std::string::npos) ? path.substr(0, sep + 1) : "./";
			}

			// DeferredSRV: ResourceManager の非同期ロードを IShaderResourceView でラップ
			class DeferredSRV final : public graphics::IShaderResourceView
			{
			public:
				explicit DeferredSRV(std::shared_ptr<res::GPUResource> res)
					: res_(std::move(res)) {}

				void  Release() override {}
				void* GetNativeHandle() const override
				{
					if (!res_) return nullptr;
					const auto* srv = res_->GetShaderResourceView();
					return srv ? srv->GetNativeHandle() : nullptr;
				}
			private:
				std::shared_ptr<res::GPUResource> res_;
			};

		} // anonymous namespace


		bool FontAsset::LoadFromJson(const char* jsonPath)
		{
			util::JsonValue root = util::JsonParser::ParseFile(jsonPath);
			if (root.IsNull()) return false;

			// --- atlas セクション ---
			const auto& atlasJ = root["atlas"];
			if (!atlasJ.IsNull())
			{
				atlasW_   = atlasJ["width"].AsFloat(1024.f);
				atlasH_   = atlasJ["height"].AsFloat(1024.f);
				baseSize_ = atlasJ["size"].AsFloat(48.f);
				pxRange_  = atlasJ["distanceRange"].AsFloat(4.f);
			}

			// --- metrics セクション ---
			const auto& metJ = root["metrics"];
			if (!metJ.IsNull())
			{
				lineHeight_ = metJ["lineHeight"].AsFloat(1.2f);
				ascender_   = metJ["ascender"].AsFloat(0.8f);
				descender_  = metJ["descender"].AsFloat(-0.2f);
			}

			// --- glyphs 配列 ---
			const auto& glyphsJ = root["glyphs"];
			if (glyphsJ.IsArray())
			{
				for (size_t i = 0; i < glyphsJ.Size(); ++i)
				{
					const auto& gJ = glyphsJ[i];
					if (gJ["unicode"].IsNull()) continue;

					const char32_t cp = static_cast<char32_t>(gJ["unicode"].AsInt());
					GlyphInfo g;
					g.advance = gJ["advance"].AsFloat();

					const auto& pb = gJ["planeBounds"];
					if (!pb.IsNull())
					{
						g.planeLeft   = pb["left"].AsFloat();
						g.planeBottom = pb["bottom"].AsFloat();
						g.planeRight  = pb["right"].AsFloat();
						g.planeTop    = pb["top"].AsFloat();
					}

					const auto& ab = gJ["atlasBounds"];
					if (!ab.IsNull())
					{
						g.uvLeft   = ab["left"].AsFloat()   / atlasW_;
						g.uvBottom = ab["bottom"].AsFloat() / atlasH_;
						g.uvRight  = ab["right"].AsFloat()  / atlasW_;
						g.uvTop    = ab["top"].AsFloat()    / atlasH_;
					}

					glyphs_[cp] = g;
				}
			}

			// --- テクスチャをロード (atlas.json と同ディレクトリの atlas.png) ---
			{
				const std::string dir     = DirOf(jsonPath);
				const std::string texPath = dir + "atlas.png";

				auto gpuRes = res::ResourceManager::Get().Load<res::GPUResource>(texPath.c_str());
				if (gpuRes)
					atlasSrv_ = std::make_shared<DeferredSRV>(std::move(gpuRes));
			}

			loaded_ = !glyphs_.empty();
			return loaded_;
		}


		const GlyphInfo* FontAsset::GetGlyph(char32_t cp) const
		{
			auto it = glyphs_.find(cp);
			if (it != glyphs_.end()) return &it->second;

			// フォールバック: 'tofu' ブロック ('?' → U+003F)
			it = glyphs_.find(U'?');
			if (it != glyphs_.end()) return &it->second;

			return nullptr;
		}

	} // namespace ui
} // namespace aq
