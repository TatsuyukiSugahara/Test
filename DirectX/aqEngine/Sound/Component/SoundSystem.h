#pragma once
#include "ECS/System.h"


namespace aq
{
	namespace sound
	{
		// ECS と SoundEngine を橋渡しする System（§9）。
		// 毎フレーム、AudioListenerComponent のワールド変換を SoundListener へ、
		// AudioSourceComponent のワールド変換を対応 SoundSource へ反映する。
		// HierarcicalTransformSystem に依存させて、ワールド変換確定後に実行すること。
		class SoundSystem : public ecs::SystemBase
		{
		public:
			void Update() override;

#ifdef AQ_DEBUG_IMGUI
			const char* GetDebugGroup()    const override { return "Sound"; }
			const char* GetDebugTabLabel() const override { return "SoundSystem"; }
#endif
		};
	}
}
