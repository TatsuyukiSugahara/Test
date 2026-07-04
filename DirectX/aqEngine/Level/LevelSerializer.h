#pragma once
#include "Level/LevelData.h"
#include "Util/SimpleJson.h"
#include <memory>


namespace aq
{
	namespace level
	{
		// JSON（.level.json）と LevelData の相互変換を担う JSON 層。
		// entity ノードの解決（"prefab" 参照展開・overrides 適用・循環検出）は
		// ecs::PrefabSerializer に委譲する（Prefab 資産の再利用・設計 §4）。
		class LevelSerializer
		{
		public:
			// .level.json ファイルを読み込んで LevelData を返す。失敗時は nullptr。
			// baseDir はファイルのディレクトリとなり、entities/subLevels の相対参照はそこから解決する。
			static std::shared_ptr<const LevelData> Load(const char* path);

			// 既にパース済みの JsonValue（Level ルート）から LevelData を構築する。
			// ファイルを介さずテスト・動的生成で使う。baseDir はネスト参照の相対解決基準。
			static std::shared_ptr<const LevelData> FromJson(const util::JsonValue& root, const char* baseDir = "");
		};
	}
}
