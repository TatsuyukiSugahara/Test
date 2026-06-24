#pragma once
#include <string>
#include <unordered_map>
#include "TextStyle.h"

namespace aq
{
	namespace ui
	{
		// TextStyle アセット (.textstyle.json) のシングルトンキャッシュ。
		// パスごとに 1 回だけ JSON を読み込み、以降は参照を返す。
		class TextStyleCache
		{
		public:
			static TextStyleCache& Get()
			{
				static TextStyleCache instance;
				return instance;
			}

			// path をロードして const 参照を返す。失敗時はデフォルトスタイルを返す。
			const TextStyle& Load(const std::string& path)
			{
				auto it = cache_.find(path);
				if (it != cache_.end()) return it->second;

				TextStyle& s = cache_[path];
				if (!s.LoadFromJson(path.c_str()))
					s = TextStyle::MakeDefault();
				return s;
			}

			// キャッシュを無効化して次回ロード時に再読み込みさせる
			void Invalidate(const std::string& path) { cache_.erase(path); }
			void Clear() { cache_.clear(); }

		private:
			std::unordered_map<std::string, TextStyle> cache_;
		};

	} // namespace ui
} // namespace aq
