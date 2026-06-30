#pragma once
#include "Prefab.h"
#include "Util/SimpleJson.h"

namespace aq
{
	namespace ecs
	{
		// JSON（.prefab.json）と PrefabData の相互変換を担う JSON 層（設計 §3 の 2 層分離）。
		// ネスト参照（"prefab"）の展開・overrides 意味論・循環検出・最大深度に対応（設計 §7）。
		// 参照展開は Load 時に完了し、PrefabData には参照を残さない（ランタイムは自己完結）。
		class PrefabSerializer
		{
		public:
			// .prefab.json ファイルを読み込んで Prefab を返す。失敗時は無効な Prefab。
			// baseDir はファイルのディレクトリとなり、ネスト参照はそこからの相対で解決する。
			static Prefab Load(const char* path);

			// 既にパース済みの JsonValue（プレハブのルートノード）から Prefab を構築する。
			// ファイルを介さずテスト・動的生成で使う。baseDir はネスト参照の相対解決基準。
			static Prefab FromJson(const util::JsonValue& root, const char* baseDir = "");
		};
	}
}
