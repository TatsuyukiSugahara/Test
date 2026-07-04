#pragma once
#include "ECS/IComponent.h"
#include "Level/LevelId.h"


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
	}
}
