#pragma once
#include "ECS/System.h"


namespace aq
{
	namespace level
	{
		// LevelStreamComponent を走査し、loadWhenActive の状態に応じて参照 Level を動的に Load/Unload する
		// 常設 System（設計 §8）。トリガー/ゲームロジックが loadWhenActive を切り替えることで
		// 「近づいたら読む/離れたら捨てる」といったストリーミングを実現する。
		//
		// Load/Unload は内部で別の Foreach を回すため、走査中には呼ばず対象を収集してから実行する。
		class LevelStreamSystem : public ecs::SystemBase
		{
		public:
			void Update() override;

#ifdef AQ_DEBUG_IMGUI
			const char* GetDebugGroup()    const override { return "Level"; }
			const char* GetDebugTabLabel() const override { return "Stream"; }
			void        RenderContent()          override;
#endif
		};
	}
}
