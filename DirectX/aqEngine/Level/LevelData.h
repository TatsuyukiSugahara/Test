#pragma once
#include "ECS/Prefab.h"   // ecs::PrefabNodeData
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		// 別 .level.json への参照 + ロードタイミング。
		// loadOnStart=true は親 Level のロード時に一緒に読む。false は休眠（後から手動/自動ストリーム）。
		struct SubLevelRef
		{
			std::string path;
			bool        loadOnStart = true;
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
