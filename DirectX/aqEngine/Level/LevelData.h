#pragma once
#include "ECS/Prefab.h"   // ecs::PrefabNodeData
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		struct LevelData;   // 前方宣言（SubLevelRef のインライン定義参照用）


		// サブLevel エントリ。外部 .level.json 参照（path）か、インライン定義（inlineData）のいずれか。
		// loadOnStart=true は親 Level のロード時に一緒に読む。false は休眠（後から手動/自動ストリーム）。
		struct SubLevelRef
		{
			std::string                      path;                 // 外部ファイル参照の正本（inlineData が無い場合）
			bool                             loadOnStart = true;
			std::shared_ptr<const LevelData> inlineData;           // 非 null = インライン定義（path より優先）
		};


		// 解決済み・不変の Level プラン（ecs::PrefabData と同じ流儀）。
		// entities は overrides 適用後・ネスト参照展開後の最終形（各要素が 1 ツリーの root）。
		// shared_ptr<const LevelData> として共有し、遅延ロードコマンドが値捕獲する（設計 §7 寿命ルール）。
		struct LevelData
		{
			std::string                      name;
			std::vector<ecs::PrefabNodeData> entities;
			std::vector<SubLevelRef>         subLevels;
		};
	}
}
