#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "UI/Font/FontResource.h"
#include "Resource/Resource.h"

namespace aq
{
	namespace ui
	{
		// FontResource のシングルトンキャッシュ。
		// パスごとに 1 回だけ ResourceManager へ非同期ロードを依頼し、以降は shared_ptr を返す。
		// 実体の atlas.json パースは worker スレッド上で行われる (FontLoader::Loading)。
		class FontAssetCache
		{
		public:
			static FontAssetCache& Get()
			{
				static FontAssetCache instance;
				return instance;
			}

			std::shared_ptr<FontResource> Load(const std::string& path)
			{
				auto it = cache_.find(path);
				if (it != cache_.end()) return it->second;

				auto res = res::ResourceManager::Get().Load<FontResource>(path.c_str());
				cache_[path] = res;
				return res;
			}

			void Clear() { cache_.clear(); }

		private:
			std::unordered_map<std::string, std::shared_ptr<FontResource>> cache_;
		};

	} // namespace ui
} // namespace aq
