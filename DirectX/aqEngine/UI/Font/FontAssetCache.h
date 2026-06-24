#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "FontAsset.h"

namespace aq
{
	namespace ui
	{
		// FontAsset のシングルトンキャッシュ。
		// パスごとに 1 回だけ atlas.json を読み込み、以降は shared_ptr を返す。
		class FontAssetCache
		{
		public:
			static FontAssetCache& Get()
			{
				static FontAssetCache instance;
				return instance;
			}

			std::shared_ptr<FontAsset> Load(const std::string& path)
			{
				auto it = cache_.find(path);
				if (it != cache_.end()) return it->second;

				auto asset = std::make_shared<FontAsset>();
				asset->LoadFromJson(path.c_str());
				cache_[path] = asset;
				return asset;
			}

			void Clear() { cache_.clear(); }

		private:
			std::unordered_map<std::string, std::shared_ptr<FontAsset>> cache_;
		};

	} // namespace ui
} // namespace aq
