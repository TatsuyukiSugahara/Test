#pragma once
#include "ECS/IComponent.h"
#include "Level/LevelId.h"
#include <string>


namespace aq
{
	namespace level
	{
		// Level 所属を示すタグコンポーネント。Level ロード時に配下の全 Entity へ付与し、
		// Unload 時は Foreach<LevelMemberComponent> で levelId 一致（子孫含む）を破棄する。
		// levelId はランタイム所有情報のため serialize しない（HTC の親子ハンドルと同じ扱い）。
		struct LevelMemberComponent : public ecs::IComponent
		{
			ecsComponent(aq::level::LevelMemberComponent);

			LevelId levelId;
		};


		// 参照 Level をフラグ駆動で動的にストリーム（Load/Unload）するコンポーネント（設計 §8）。
		// levelPath を正本に持ち、LevelStreamSystem が loadWhenActive の変化に応じて Load/Unload する。
		// トリガーボリューム等が loadWhenActive を切り替えることで「近づいたら読む/離れたら捨てる」を実現する。
		// SpawnerComponent と同じく、専用の参照機構ではなく path 文字列を正本にするだけ。
		struct LevelStreamComponent : public ecs::IComponent
		{
			ecsComponent(aq::level::LevelStreamComponent);

			std::string levelPath;              // 正本（serialize される）。例 "Assets/Levels/Town.level.json"
			LevelId     loaded;                 // ランタイム解決結果（serialize しない）
			bool        loadWhenActive = true;  // true→未ロードなら Load / false→ロード済みなら Unload

			template <typename V>
			void Reflect(V& visitor)
			{
				visitor.FieldPath("level", levelPath, "Level");
				visitor.Field("loadWhenActive", loadWhenActive, "Load When Active");
			}
		};
	}
}
