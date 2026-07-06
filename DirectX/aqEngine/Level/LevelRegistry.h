#pragma once
#include "Level/LevelData.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>


namespace aq
{
	namespace level
	{
		// path（または GUID 文字列）を正本に LevelData を解決・キャッシュするレジストリ。
		// ecs::PrefabRegistry と対になる（Prefab は PrefabId キャッシュキーを持つが、Level は当面
		// 正規化パス文字列を直接キーにする）。多重ロードはキャッシュから返す。
		class LevelRegistry
		{
		// ── メンバ変数 ──
		private:
			std::unordered_map<std::string, std::shared_ptr<const LevelData>> cache_;

		// ── メンバ関数 ──
		public:
			static LevelRegistry& Get();

			// path を解決して LevelData を返す。未ロードなら LevelSerializer::Load で読み込みキャッシュする。
			// 失敗時は nullptr。
			std::shared_ptr<const LevelData> Load(std::string_view pathOrId);

			// 構築済み LevelData を文字列キーで登録する（ファイルを介さない動的生成・テスト用）。
			// 既存キーがあればそれを返す（再登録しない）。
			std::shared_ptr<const LevelData> Register(std::string_view key, std::shared_ptr<const LevelData> data);

			// キャッシュを全消去する。
			void Clear();

			// path 正規化（バックスラッシュ→スラッシュ）。大文字小文字は変更しない。
			static std::string Normalize(std::string_view pathOrId);

		private:
			LevelRegistry() = default;
		};
	}
}
